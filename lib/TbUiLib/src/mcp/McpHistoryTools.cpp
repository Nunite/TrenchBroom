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
#include "ui/mcp/McpObjectRegistry.h"
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

bool isSelectionCommandName(const QString& commandName)
{
  return commandName == "Select None" || commandName == "Select All Objects"
         || commandName == "Select All Brush Faces"
         || commandName == "Convert to Brush Face Selection"
         || commandName.startsWith("Select ") || commandName.startsWith("Deselect ");
}

QJsonObject operationRecordJson(const McpOperationRecord& operation)
{
  auto result = QJsonObject{};
  result.insert("operationId", operation.operationId);
  result.insert("toolName", operation.toolName);
  result.insert("transactionName", operation.transactionName);
  result.insert("createdAt", operation.createdAt);
  result.insert("createdAtMs", operation.createdAtMs);
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

QJsonObject operationStaleDiagnosticJson(
  const McpOperationRecord& operation,
  const int liveObjectCount,
  const int staleObjectCount)
{
  auto result = QJsonObject{
    {"liveObjectCount", liveObjectCount},
    {"staleObjectCount", staleObjectCount},
    {"mismatchCount", 0},
    {"valid", !operation.undone && staleObjectCount == 0},
  };
  if (operation.undone)
  {
    result.insert("staleReason", "operation was undone");
    result.insert(
      "suggestedAction",
      "Redo the MCP operation if available, or recreate/re-identify the objects in the "
      "current map.");
  }
  else if (staleObjectCount > 0)
  {
    result.insert(
      "staleReason",
      "one or more operation object ids no longer resolve in the active document");
    result.insert(
      "suggestedAction",
      "The objects may have been deleted, the map may have been reloaded, or node ids "
      "may have changed. Use selection_filter/selection_by_bounds or operation details "
      "to re-identify current objects.");
  }
  return result;
}

QJsonObject operationLiveStateJson(mdl::Map& map, const McpOperationRecord& operation)
{
  auto liveObjectCount = 0;
  auto staleObjectCount = 0;
  for (const auto& value : operation.changedObjectIds)
  {
    const auto path = parseNodePathId(value);
    if (!path || map.worldNode().resolvePath(*path) == nullptr)
    {
      ++staleObjectCount;
      continue;
    }
    ++liveObjectCount;
  }
  return operationStaleDiagnosticJson(operation, liveObjectCount, staleObjectCount);
}

QJsonObject operationRecordJson(
  mdl::Map& map,
  const McpOperationRecord& operation,
  const bool includeLiveState)
{
  auto result = operationRecordJson(operation);
  if (includeLiveState)
  {
    const auto liveState = operationLiveStateJson(map, operation);
    for (auto it = liveState.begin(); it != liveState.end(); ++it)
    {
      result.insert(it.key(), it.value());
    }
  }
  return result;
}

QJsonObject operationRecordJson(
  mdl::Map& map,
  const McpOperationRecord& operation,
  const McpObjectRegistry& objectRegistry,
  const bool includeLiveState)
{
  auto result = operationRecordJson(operation);
  if (includeLiveState)
  {
    const auto liveState =
      objectRegistry.liveStateJson(map, operation.changedObjectIds, operation.undone);
    for (auto it = liveState.begin(); it != liveState.end(); ++it)
    {
      result.insert(it.key(), it.value());
    }
  }
  return result;
}

QJsonArray operationHistoryJson(
  mdl::Map& map,
  const std::vector<McpOperationRecord>& history,
  const bool includeLiveState)
{
  auto result = QJsonArray{};
  for (const auto& operation : history)
  {
    result.push_back(operationRecordJson(map, operation, includeLiveState));
  }
  return result;
}

QJsonArray operationHistoryJson(
  mdl::Map& map,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry& objectRegistry,
  const bool includeLiveState)
{
  auto result = QJsonArray{};
  for (const auto& operation : history)
  {
    result.push_back(operationRecordJson(map, operation, objectRegistry, includeLiveState));
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

McpBridgeToolResult historyListResult(
  AppController& appController,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return historyListResult(history);
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"operations",
     operationHistoryJson(mapWindow->document().map(), history, objectRegistry, true)},
    {"count", static_cast<int>(history.size())},
    {"liveStateIncluded", true},
  });
}

McpBridgeToolResult historyListResult(
  AppController& appController, const std::vector<McpOperationRecord>& history)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return historyListResult(history);
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"operations", operationHistoryJson(mapWindow->document().map(), history, true)},
    {"count", static_cast<int>(history.size())},
    {"liveStateIncluded", true},
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

McpBridgeToolResult operationInspectResult(
  AppController& appController,
  const std::vector<McpOperationRecord>& history,
  const QJsonObject& params,
  const McpObjectRegistry& objectRegistry)
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
  auto result = operationRecordDetailJson(
    *operation,
    detail == "full"  ? "full"
    : detail == "ids" ? "ids"
                      : "summary");
  if (auto* mapWindow = appController.mapWindowManager().topMapWindow())
  {
    const auto liveState =
      objectRegistry.liveStateJson(
        mapWindow->document().map(), operation->changedObjectIds, operation->undone);
    for (auto it = liveState.begin(); it != liveState.end(); ++it)
    {
      result.insert(it.key(), it.value());
    }
  }
  return McpBridgeToolResult::success(result);
}

McpBridgeToolResult operationInspectResult(
  AppController& appController,
  const std::vector<McpOperationRecord>& history,
  const QJsonObject& params)
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
  auto result = operationRecordDetailJson(
    *operation,
    detail == "full"  ? "full"
    : detail == "ids" ? "ids"
                      : "summary");
  if (auto* mapWindow = appController.mapWindowManager().topMapWindow())
  {
    const auto liveState =
      operationLiveStateJson(mapWindow->document().map(), *operation);
    for (auto it = liveState.begin(); it != liveState.end(); ++it)
    {
      result.insert(it.key(), it.value());
    }
  }
  return McpBridgeToolResult::success(result);
}

McpBridgeToolResult operationSelectResult(
  AppController& appController,
  const std::vector<McpOperationRecord>& history,
  const QJsonObject& params,
  const McpObjectRegistry& objectRegistry)
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
  auto diagnostic =
    objectRegistry.liveStateJson(map, operation->changedObjectIds, operation->undone);
  auto nodes = std::vector<mdl::Node*>{};
  for (const auto& value : operation->changedObjectIds)
  {
    const auto resolved = objectRegistry.resolveExternalId(map, value);
    if (!resolved.ok)
    {
      continue;
    }
    const auto path = parseNodePathId(resolved.legacyPathId);
    if (path)
    {
      if (auto* node = map.worldNode().resolvePath(*path))
      {
        if (map.editorContext().selectable(*node))
        {
          nodes.push_back(node);
        }
      }
    }
  }

  mdl::deselectAll(map);
  if (!nodes.empty())
  {
    mdl::selectNodes(map, nodes);
  }

  auto result = QJsonObject{
    {"operationId", operationId},
    {"selectedCount", static_cast<int>(nodes.size())},
  };
  for (auto it = diagnostic.begin(); it != diagnostic.end(); ++it)
  {
    result.insert(it.key(), it.value());
  }
  if (nodes.empty() && !operation->changedObjectIds.empty())
  {
    result.insert(
      "diagnostic",
      "No live selectable objects were found for this operation in the active document.");
    if (!result.contains("suggestedAction"))
    {
      result.insert(
        "suggestedAction",
        "Use selection_filter/selection_by_bounds or operation_inspect(detail=full) to "
        "re-identify current objects.");
    }
  }
  return McpBridgeToolResult::success(result);
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
  auto diagnostic = operationLiveStateJson(map, *operation);
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

  auto result = QJsonObject{
    {"operationId", operationId},
    {"selectedCount", static_cast<int>(nodes.size())},
  };
  for (auto it = diagnostic.begin(); it != diagnostic.end(); ++it)
  {
    result.insert(it.key(), it.value());
  }
  if (nodes.empty() && !operation->changedObjectIds.empty())
  {
    result.insert(
      "diagnostic",
      "No live selectable objects were found for this operation in the active document.");
    if (!result.contains("suggestedAction"))
    {
      result.insert(
        "suggestedAction",
        "Use selection_filter/selection_by_bounds or operation_inspect(detail=full) to "
        "re-identify current objects.");
    }
  }
  return McpBridgeToolResult::success(result);
}

McpBridgeToolResult operationValidateResult(
  AppController& appController,
  const std::vector<McpOperationRecord>& history,
  const QJsonObject& params,
  const McpObjectRegistry& objectRegistry)
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

  const auto liveState = objectRegistry.liveStateJson(
    mapWindow->document().map(), operation->changedObjectIds, operation->undone);
  const auto liveObjectCount = liveState.value("liveObjectCount").toInt();
  const auto staleObjectCount = liveState.value("staleObjectCount").toInt();
  const auto mismatchCount = liveState.value("mismatchCount").toInt();

  auto result = QJsonObject{
    {"operationId", operationId},
    {"valid", liveState.value("valid").toBool(false)},
    {"undone", operation->undone},
    {"changedObjectCount", operation->changedObjectIds.size()},
    {"liveObjectCount", liveObjectCount},
    {"staleObjectCount", staleObjectCount},
    {"mismatchCount", mismatchCount},
  };
  for (auto it = liveState.begin(); it != liveState.end(); ++it)
  {
    result.insert(it.key(), it.value());
  }
  return McpBridgeToolResult::success(result);
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

  const auto liveState = operationLiveStateJson(mapWindow->document().map(), *operation);
  const auto liveObjectCount = liveState.value("liveObjectCount").toInt();
  const auto staleObjectCount = liveState.value("staleObjectCount").toInt();

  auto result = QJsonObject{
    {"operationId", operationId},
    {"valid", liveState.value("valid").toBool(false)},
    {"undone", operation->undone},
    {"changedObjectCount", operation->changedObjectIds.size()},
    {"liveObjectCount", liveObjectCount},
    {"staleObjectCount", staleObjectCount},
    {"mismatchCount", 0},
  };
  for (auto it = liveState.begin(); it != liveState.end(); ++it)
  {
    result.insert(it.key(), it.value());
  }
  return McpBridgeToolResult::success(result);
}

McpBridgeToolResult historyUndoResult(
  AppController& appController, std::vector<McpOperationRecord>& history)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return historyUndoForMapResult(mapWindow->document().map(), history);
}

McpBridgeToolResult historyUndoForMapResult(
  mdl::Map& map, std::vector<McpOperationRecord>& history)
{
  auto it = std::find_if(history.rbegin(), history.rend(), [](const auto& operation) {
    return !operation.undone;
  });
  if (it == history.rend())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden, "No MCP operation is available to undo");
  }

  auto skippedSelectionCommands = 0;
  while (const auto* undoName = map.undoCommandName())
  {
    const auto commandName = QString::fromStdString(*undoName);
    if (commandName == it->transactionName)
    {
      break;
    }
    if (!isSelectionCommandName(commandName))
    {
      break;
    }

    map.undoCommand();
    ++skippedSelectionCommands;
  }

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
    {"skippedSelectionCommands", skippedSelectionCommands},
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
