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

#include "McpBridgeServerTools.h"
#include "mdl/CompilationProfile.h"
#include "mdl/CompilationTask.h"
#include "mdl/GameInfo.h"
#include "mdl/Map.h"
#include "mdl/PointTrace.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"

#include "kd/overload.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ranges>

namespace tb::ui
{
namespace
{

std::string optionalString(
  const QJsonObject& params, const QString& key, const std::string& defaultValue = {})
{
  const auto value = params.value(key);
  return value.isString() ? value.toString().toStdString() : defaultValue;
}

size_t optionalSize(
  const QJsonObject& params, const QString& key, const size_t defaultValue)
{
  const auto value = params.value(key);
  if (!value.isDouble())
  {
    return defaultValue;
  }
  return static_cast<size_t>(std::max(0, value.toInt()));
}

QString pathToQString(const std::filesystem::path& path)
{
  return path.empty() ? QString{} : pathAsQString(path);
}

QString compilationTaskTypeName(const mdl::CompilationTask& task)
{
  return std::visit(
    kdl::overload(
      [](const mdl::CompilationExportMap&) { return QString{"export_map"}; },
      [](const mdl::CompilationCopyFiles&) { return QString{"copy_files"}; },
      [](const mdl::CompilationRenameFile&) { return QString{"rename_file"}; },
      [](const mdl::CompilationDeleteFiles&) { return QString{"delete_files"}; },
      [](const mdl::CompilationRunTool&) { return QString{"run_tool"}; },
      [](const mdl::CompilationLaunchEngine&) { return QString{"launch_engine"}; }),
    task);
}

bool compilationTaskEnabled(const mdl::CompilationTask& task)
{
  return std::visit([](const auto& value) { return value.enabled; }, task);
}

QJsonObject compilationTaskJson(const mdl::CompilationTask& task, const int index)
{
  auto result = QJsonObject{
    {"index", index},
    {"type", compilationTaskTypeName(task)},
    {"enabled", compilationTaskEnabled(task)},
  };

  std::visit(
    kdl::overload(
      [&](const mdl::CompilationExportMap& value) {
        result.insert("target", QString::fromStdString(value.targetSpec));
        result.insert("stripTbProperties", value.stripTbProperties);
      },
      [&](const mdl::CompilationCopyFiles& value) {
        result.insert("source", QString::fromStdString(value.sourceSpec));
        result.insert("target", QString::fromStdString(value.targetSpec));
      },
      [&](const mdl::CompilationRenameFile& value) {
        result.insert("source", QString::fromStdString(value.sourceSpec));
        result.insert("target", QString::fromStdString(value.targetSpec));
      },
      [&](const mdl::CompilationDeleteFiles& value) {
        result.insert("target", QString::fromStdString(value.targetSpec));
      },
      [&](const mdl::CompilationRunTool& value) {
        result.insert("tool", QString::fromStdString(value.toolSpec));
        result.insert("parameters", QString::fromStdString(value.parameterSpec));
        result.insert(
          "treatNonZeroResultCodeAsError", value.treatNonZeroResultCodeAsError);
      },
      [&](const mdl::CompilationLaunchEngine& value) {
        result.insert("engineProfileId", QString::fromStdString(value.engineProfileId));
        result.insert("treatLaunchFailureAsError", value.treatLaunchFailureAsError);
      }),
    task);

  return result;
}

QJsonObject compilationProfileJson(
  const mdl::CompilationProfile& profile, const int index)
{
  auto tasks = QJsonArray{};
  for (size_t i = 0; i < profile.tasks.size(); ++i)
  {
    tasks.push_back(compilationTaskJson(profile.tasks[i], static_cast<int>(i)));
  }

  return QJsonObject{
    {"index", index},
    {"name", QString::fromStdString(profile.name)},
    {"workDir", QString::fromStdString(profile.workDirSpec)},
    {"taskCount", static_cast<int>(profile.tasks.size())},
    {"tasks", tasks},
  };
}

} // namespace

McpBridgeToolResult compileProfilesListResult(AppController& appController)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  const auto& profiles =
    mapWindow->document().map().gameInfo().compilationConfig.profiles;
  auto profilesJson = QJsonArray{};
  for (size_t i = 0; i < profiles.size(); ++i)
  {
    profilesJson.push_back(compilationProfileJson(profiles[i], static_cast<int>(i)));
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"profiles", profilesJson},
    {"count", profilesJson.size()},
  });
}

McpBridgeToolResult compileRunResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  const auto& profiles =
    mapWindow->document().map().gameInfo().compilationConfig.profiles;
  if (profiles.empty())
  {
    return invalidParamsFailure("Active document has no compilation profiles");
  }

  auto profileName = optionalString(params, "profile");
  if (profileName.empty())
  {
    profileName = profiles.front().name;
  }

  const auto profileIt = std::ranges::find_if(
    profiles, [&](const auto& profile) { return profile.name == profileName; });
  if (profileIt == std::end(profiles))
  {
    return invalidParamsFailure(QString{"Unknown compilation profile: %1"}.arg(
      QString::fromStdString(profileName)));
  }
  if (profileIt->tasks.empty())
  {
    return invalidParamsFailure(QString{"Compilation profile has no tasks: %1"}.arg(
      QString::fromStdString(profileName)));
  }

  if (!mapWindow->runCompilationProfile(profileName))
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError,
      QString{"Could not start compilation profile: %1"}.arg(
        QString::fromStdString(profileName)));
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"profile", QString::fromStdString(profileName)},
    {"started", true},
    {"running", mapWindow->compilationRunning()},
  });
}

McpBridgeToolResult compileLogTailResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto maxLines = optionalSize(params, "maxLines", 80);
  if (maxLines == 0)
  {
    maxLines = 80;
  }

  const auto lines = mapWindow->compilationOutputText().split('\n');
  const auto start =
    std::max<qsizetype>(0, lines.size() - static_cast<qsizetype>(maxLines));

  auto tail = QJsonArray{};
  for (auto i = start; i < lines.size(); ++i)
  {
    tail.push_back(lines[i]);
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"lines", tail},
    {"lineCount", tail.size()},
    {"running", mapWindow->compilationRunning()},
  });
}

McpBridgeToolResult leaksLoadPointfileResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  const auto pathString = params.value("path").toString().trimmed();
  if (pathString.isEmpty())
  {
    return invalidParamsFailure("path is required");
  }

  const auto path = pathFromQString(pathString).lexically_normal();
  if (path.empty() || !path.is_absolute())
  {
    return invalidParamsFailure("path must be an absolute path");
  }
  if (!std::filesystem::is_regular_file(path))
  {
    return invalidParamsFailure(
      QString{"Pointfile does not exist: %1"}.arg(pathToQString(path)));
  }

  auto stream = std::ifstream{path};
  if (!stream)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError,
      QString{"Could not open pointfile: %1"}.arg(pathToQString(path)));
  }

  auto trace = mdl::loadPointFile(stream);
  if (trace.is_error())
  {
    return invalidParamsFailure(resultErrorMessage(trace));
  }
  const auto pointCount = static_cast<int>(trace.value().points().size());

  mapWindow->document().loadPointFile(path);

  return McpBridgeToolResult::success(QJsonObject{
    {"loaded", mapWindow->document().isPointFileLoaded()},
    {"path", pathToQString(path)},
    {"pointCount", pointCount},
  });
}

} // namespace tb::ui
