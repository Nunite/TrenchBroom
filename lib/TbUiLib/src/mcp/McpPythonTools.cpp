/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextStream>

#include "McpBridgeServerTools.h"
#include "mcp/McpError.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"

#include <algorithm>

namespace tb::ui
{
namespace mcp = tb::mcp;

namespace
{

constexpr auto DefaultTimeoutMs = 5000;
constexpr auto MaxTimeoutMs = 30000;
constexpr auto MaxOutputBytes = 1024 * 1024;

QString pythonExecutablePath()
{
  const auto appDir = QCoreApplication::applicationDirPath();
#ifdef _WIN32
  const auto bundledPython = appDir + "/python.exe";
#else
  const auto bundledPython = appDir + "/python3";
#endif
  if (QFileInfo::exists(bundledPython))
  {
    return bundledPython;
  }
#ifdef TB_PYTHON3_EXECUTABLE
  const auto cmakePython = QString::fromUtf8(TB_PYTHON3_EXECUTABLE);
  if (QFileInfo::exists(cmakePython))
  {
    return cmakePython;
  }
#endif
  return "python";
}

int timeoutFromParams(const QJsonObject& params)
{
  const auto timeout = params.value("timeoutMs").toInt(DefaultTimeoutMs);
  return std::clamp(timeout, 1, MaxTimeoutMs);
}

McpBridgeToolResult processFailure(
  const mcp::McpErrorCode code,
  const QString& message,
  const QByteArray& stdoutBytes = {},
  const QByteArray& stderrBytes = {})
{
  auto result = QJsonObject{
    {"message", message},
    {"stdoutBytes", stdoutBytes.size()},
    {"stderrBytes", stderrBytes.size()},
  };
  if (!stderrBytes.isEmpty())
  {
    result.insert("stderr", QString::fromUtf8(stderrBytes.left(4096)));
  }
  if (!stdoutBytes.isEmpty())
  {
    result.insert("stdout", QString::fromUtf8(stdoutBytes.left(4096)));
  }
  result.insert("valid", false);
  result.insert("mutatedDocument", false);
  result.insert("retrySafe", true);
  result.insert("recoveryAction", "fix_python_blockout_script_then_retry");

  auto failure = McpBridgeToolResult::failure(code, message);
  failure.error.details = std::move(result);
  return failure;
}

} // namespace

McpBridgeToolResult pythonGenerateBlockoutResult(
  AppController& appController,
  const QString&,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return pythonGenerateBlockoutForMapResult(
    mapWindow->document().map(),
    "python_generate_blockout",
    params,
    history,
    nextOperationIndex,
    metadataStore);
}

McpBridgeToolResult pythonGenerateBlockoutForMapResult(
  mdl::Map& map,
  const QString&,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  const auto script = params.value("script").toString();
  if (script.trimmed().isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "python_generate_blockout requires script",
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"recoveryAction", "provide_python_blockout_script_then_retry"},
      });
  }

  auto tempDir = QTemporaryDir{};
  if (!tempDir.isValid())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not create temporary Python directory");
  }

  auto scriptFile = QTemporaryFile{tempDir.path() + "/tbmcp-python-XXXXXX.py"};
  if (!scriptFile.open())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not create temporary Python script");
  }
  {
    auto stream = QTextStream{&scriptFile};
    stream << script;
  }
  scriptFile.close();

  auto process = QProcess{};
  process.setProgram(pythonExecutablePath());
  process.setArguments({scriptFile.fileName()});
  process.setWorkingDirectory(tempDir.path());
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start();
  if (!process.waitForStarted(3000))
  {
    return processFailure(
      mcp::McpErrorCode::InternalError,
      QString{"Could not start Python process: %1"}.arg(process.errorString()));
  }

  const auto timeoutMs = timeoutFromParams(params);
  if (!process.waitForFinished(timeoutMs))
  {
    process.kill();
    process.waitForFinished(1000);
    return processFailure(
      mcp::McpErrorCode::InternalError,
      QString{"Python process timed out after %1 ms"}.arg(timeoutMs),
      process.readAllStandardOutput(),
      process.readAllStandardError());
  }

  const auto stdoutBytes = process.readAllStandardOutput();
  const auto stderrBytes = process.readAllStandardError();
  if (stdoutBytes.size() > MaxOutputBytes || stderrBytes.size() > MaxOutputBytes)
  {
    return processFailure(
      mcp::McpErrorCode::InvalidParams,
      "Python output exceeded the 1MB limit",
      stdoutBytes,
      stderrBytes);
  }

  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
  {
    return processFailure(
      mcp::McpErrorCode::InvalidParams,
      QString{"Python process exited with code %1"}.arg(process.exitCode()),
      stdoutBytes,
      stderrBytes);
  }

  auto parseError = QJsonParseError{};
  const auto document = QJsonDocument::fromJson(stdoutBytes, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
  {
    return processFailure(
      mcp::McpErrorCode::InvalidParams,
      QString{"Python stdout must be a JSON object: %1"}.arg(parseError.errorString()),
      stdoutBytes,
      stderrBytes);
  }

  const auto pythonResult = document.object();
  const auto operationsValue = pythonResult.value("operations");
  if (!operationsValue.isArray())
  {
    return processFailure(
      mcp::McpErrorCode::InvalidParams,
      "Python JSON must contain an operations array",
      stdoutBytes,
      stderrBytes);
  }

  auto batchParams = QJsonObject{
    {"operations", operationsValue.toArray()},
    {"name", params.value("name").toString("MCP: Python blockout")},
    {"grid", params.value("grid").toDouble(1.0)},
    {"select", params.value("select").toBool(true)},
    {"detail", params.value("detail").toString("summary")},
  };

  if (pythonResult.contains("material") && !params.contains("material"))
  {
    batchParams.insert("material", pythonResult.value("material"));
  }
  if (params.contains("material"))
  {
    batchParams.insert("material", params.value("material"));
  }

  auto result = blockoutCreateBatchForMapResult(
    map, "blockout_create_batch", batchParams, history, nextOperationIndex);
  if (result.ok)
  {
    const auto changedObjectIds =
      !history.empty()
          && history.back().operationId == result.result.value("operationId").toString()
        ? history.back().changedObjectIds
        : QStringList{};
    const auto metadataCount = storeBatchOperationMetadata(
      operationsValue.toArray(), changedObjectIds, metadataStore);
    if (metadataCount > 0)
    {
      result.result.insert("metadataCount", metadataCount);
      if (
        !history.empty()
        && history.back().operationId == result.result.value("operationId").toString())
      {
        history.back().setSummary(result.result);
      }
    }
    result.result.insert(
      "python",
      QJsonObject{
        {"stdoutBytes", stdoutBytes.size()},
        {"stderrBytes", stderrBytes.size()},
      });
  }
  return result;
}

} // namespace tb::ui
