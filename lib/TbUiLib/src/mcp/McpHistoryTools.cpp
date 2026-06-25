/*
 Copyright (C) 2026

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

#include <QJsonArray>
#include <QJsonObject>

#include "McpBridgeServerTools.h"
#include "mcp/McpError.h"
#include "mdl/EditorContext.h"
#include "mdl/Map.h"
#include "mdl/Map_Selection.h"
#include "mdl/Node.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"

#include <algorithm>
#include <vector>

namespace tb::ui
{
namespace mcp = tb::mcp;

namespace
{

QJsonObject operationRecordJson(const McpOperationRecord& operation)
{
  auto result = QJsonObject{};
  result.insert("operationId", operation.operationId);
  result.insert("toolName", operation.toolName);
  result.insert("transactionName", operation.transactionName);
  result.insert("changedObjectCount", operation.changedObjectIds.size());
  result.insert(
    "resourceUri", QString{"tbmcp://operation/%1"}.arg(operation.operationId));
  result.insert("undone", operation.undone);
  return result;
}

QJsonObject operationRecordDetailJson(
  const McpOperationRecord& operation, const QString& detail)
{
  auto result = operationRecordJson(operation);
  result.insert("detail", detail);
  if (detail == "ids" || detail == "full")
  {
    result.insert("changedObjectIds", operation.changedObjectIdsJson());
  }
  if (detail == "full")
  {
    result.insert("summary", operation.summary());
    result.insert("operationDetail", operation.detail());
  }
  return result;
}

QJsonArray operationHistoryJson(const std::vector<McpOperationRecord>& history)
{
  auto result = QJsonArray{};
  for (const auto& operation : history)
  {
    result.push_back(operationRecordJson(operation));
  }
  return result;
}

} // namespace

McpBridgeToolResult historyListResult(const std::vector<McpOperationRecord>& history)
{
  return McpBridgeToolResult::success(QJsonObject{
    {"operations", operationHistoryJson(history)},
    {"count", static_cast<int>(history.size())},
  });
}

std::optional<std::size_t> findOperationIndex(
  const std::vector<McpOperationRecord>& history, const QString& operationId)
{
  const auto it = std::ranges::find_if(
    history, [&](const auto& operation) { return operation.operationId == operationId; });
  if (it == history.end())
  {
    return std::nullopt;
  }
  return static_cast<std::size_t>(std::distance(history.begin(), it));
}

std::optional<McpOperationRecord> findOperationCopy(
  const std::vector<McpOperationRecord>& history, const QString& operationId)
{
  const auto index = findOperationIndex(history, operationId);
  if (!index)
  {
    return std::nullopt;
  }
  return history[*index];
}

std::optional<mdl::NodePath> parseNodePathId(const QString& id)
{
  static const auto Prefix = QString{"node:"};
  if (id == "node:world")
  {
    return mdl::NodePath{};
  }
  if (!id.startsWith(Prefix))
  {
    return std::nullopt;
  }

  auto path = mdl::NodePath{};
  for (const auto& part : id.mid(Prefix.size()).split('/', Qt::SkipEmptyParts))
  {
    auto ok = false;
    const auto index = part.toULongLong(&ok);
    if (!ok)
    {
      return std::nullopt;
    }
    path.indices.push_back(static_cast<std::size_t>(index));
  }
  return path;
}

McpBridgeToolResult operationInspectResult(
  const std::vector<McpOperationRecord>& history, const QJsonObject& params)
{
  const auto operationId = params.value("operationId").toString();
  if (operationId.isEmpty())
  {
    return invalidParamsFailure("operation_inspect requires operationId");
  }

  const auto operation = findOperationCopy(history, operationId);
  if (!operation)
  {
    return invalidParamsFailure(QString{"Unknown MCP operation id: %1"}.arg(operationId));
  }

  const auto detail = params.value("detail").toString("summary").toLower();
  return McpBridgeToolResult::success(operationRecordDetailJson(
    *operation,
    detail == "full"  ? "full"
    : detail == "ids" ? "ids"
                      : "summary"));
}

McpBridgeToolResult operationSelectResult(
  AppController& appController,
  const std::vector<McpOperationRecord>& history,
  const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  const auto operationId = params.value("operationId").toString();
  if (operationId.isEmpty())
  {
    return invalidParamsFailure("operation_select requires operationId");
  }

  const auto operation = findOperationCopy(history, operationId);
  if (!operation)
  {
    return invalidParamsFailure(QString{"Unknown MCP operation id: %1"}.arg(operationId));
  }

  auto& map = mapWindow->document().map();
  auto nodes = std::vector<mdl::Node*>{};
  for (const auto& value : operation->changedObjectIds)
  {
    const auto path = parseNodePathId(value);
    if (!path)
    {
      continue;
    }
    if (auto* node = map.worldNode().resolvePath(*path))
    {
      if (map.editorContext().selectable(*node))
      {
        nodes.push_back(node);
      }
    }
  }

  mdl::deselectAll(map);
  if (!nodes.empty())
  {
    mdl::selectNodes(map, nodes);
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"operationId", operationId},
    {"selectedCount", static_cast<int>(nodes.size())},
  });
}

McpBridgeToolResult operationValidateResult(
  AppController& appController,
  const std::vector<McpOperationRecord>& history,
  const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  const auto operationId = params.value("operationId").toString();
  if (operationId.isEmpty())
  {
    return invalidParamsFailure("operation_validate requires operationId");
  }

  const auto operation = findOperationCopy(history, operationId);
  if (!operation)
  {
    return invalidParamsFailure(QString{"Unknown MCP operation id: %1"}.arg(operationId));
  }

  auto liveObjectCount = 0;
  auto staleObjectCount = 0;
  auto& map = mapWindow->document().map();
  for (const auto& value : operation->changedObjectIds)
  {
    const auto path = parseNodePathId(value);
    if (!path || map.worldNode().resolvePath(*path) == nullptr)
    {
      ++staleObjectCount;
      continue;
    }
    ++liveObjectCount;
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"operationId", operationId},
    {"valid", !operation->undone && staleObjectCount == 0},
    {"undone", operation->undone},
    {"changedObjectCount", operation->changedObjectIds.size()},
    {"liveObjectCount", liveObjectCount},
    {"staleObjectCount", staleObjectCount},
  });
}

McpBridgeToolResult historyUndoResult(
  AppController& appController, std::vector<McpOperationRecord>& history)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto it = std::find_if(history.rbegin(), history.rend(), [](const auto& operation) {
    return !operation.undone;
  });
  if (it == history.rend())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden, "No MCP operation is available to undo");
  }

  auto& map = mapWindow->document().map();
  const auto* undoName = map.undoCommandName();
  if (!undoName || QString::fromStdString(*undoName) != it->transactionName)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden,
      "The latest MCP operation is no longer on top of the undo stack");
  }

  map.undoCommand();
  it->undone = true;
  return McpBridgeToolResult::success(QJsonObject{
    {"operation", operationRecordJson(*it)},
    {"undone", true},
  });
}

McpBridgeToolResult historyRedoResult(
  AppController& appController, std::vector<McpOperationRecord>& history)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto it = std::find_if(history.begin(), history.end(), [](const auto& operation) {
    return operation.undone;
  });
  if (it == history.end())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden, "No MCP operation is available to redo");
  }

  auto& map = mapWindow->document().map();
  const auto* redoName = map.redoCommandName();
  if (!redoName || QString::fromStdString(*redoName) != it->transactionName)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden,
      "The MCP operation is no longer on top of the redo stack");
  }

  map.redoCommand();
  it->undone = false;
  return McpBridgeToolResult::success(QJsonObject{
    {"operation", operationRecordJson(*it)},
    {"redone", true},
  });
}

} // namespace tb::ui
