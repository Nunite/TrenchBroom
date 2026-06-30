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
#include "ui/QPathUtils.h"
#include "ui/mcp/McpObjectRegistry.h"

#include <algorithm>
#include <vector>

namespace tb::ui
{
namespace mcp = tb::mcp;

namespace
{

QString detailFromParams(const QJsonObject& params)
{
  const auto idsMode = params.value("idsMode").toString().trimmed().toLower();
  if (idsMode == "full")
  {
    return "ids";
  }
  if (idsMode == "none" || idsMode == "count" || idsMode == "sample")
  {
    return "summary";
  }

  const auto detail = params.value("detail").toString("summary").toLower();
  return detail == "full" ? "full" : detail == "ids" ? "ids" : "summary";
}

bool isSelectionCommandName(const QString& commandName)
{
  return commandName == "Select None" || commandName == "Select All Objects"
         || commandName == "Select All Brush Faces"
         || commandName == "Convert to Brush Face Selection"
         || commandName == "Drag Select Objects" || commandName.startsWith("Select ")
         || commandName.startsWith("Deselect ");
}

QJsonObject operationRecordJson(const McpOperationRecord& operation)
{
  auto result = QJsonObject{};
  result.insert("operationId", operation.operationId);
  result.insert("toolName", operation.toolName);
  result.insert("transactionName", operation.transactionName);
  result.insert(
    "operationKind",
    operation.operationKind.isEmpty() ? "mutation" : operation.operationKind);
  result.insert("createdAt", operation.createdAt);
  result.insert("createdAtMs", operation.createdAtMs);
  result.insert("changedObjectCount", operation.changedObjectIds.size());
  result.insert("deletedObjectCount", operation.deletedObjectIds.size());
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
  result.insert("summary", operation.summary());
  if (detail == "ids" || detail == "full")
  {
    result.insert("changedObjectIds", operation.changedObjectIdsJson());
    result.insert("deletedObjectIds", operation.deletedObjectIdsJson());
  }
  if (detail == "full")
  {
    const auto operationDetail = operation.detail();
    result.insert("operationDetail", operationDetail);
    if (operationDetail.value("expandedOperations").isArray())
    {
      result.insert("expandedOperations", operationDetail.value("expandedOperations"));
      result.insert(
        "expandedOperationsTruncated",
        operationDetail.value("expandedOperationsTruncated").toBool(false));
    }
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
  mdl::Map& map, const McpOperationRecord& operation, const bool includeLiveState)
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
    auto liveState =
      objectRegistry.liveStateJson(map, operation.changedObjectIds, operation.undone);
    if (operation.operationKind == "delete")
    {
      liveState.insert("valid", !operation.undone);
      liveState.insert("targetsLive", false);
      liveState.insert(
        "staleReason",
        "delete operation records objects that were removed by the transaction");
      liveState.insert(
        "suggestedAction",
        "Use deletedObjectIds for audit only; redo/undo the delete operation to change "
        "map state.");
    }
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
    result.push_back(
      operationRecordJson(map, operation, objectRegistry, includeLiveState));
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
    {"status",
     history.empty()
       ? QJsonObject{
           {"available", false},
           {"reasonIfUnavailable", "noMcpMutationYet"},
         }
       : QJsonObject{
           {"available", true},
           {"reasonIfUnavailable", QJsonValue{}},
         }},
  });
}

McpBridgeToolResult historyStatusResult(
  AppController& appController,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry& objectRegistry,
  const QString& bridgeInstanceId,
  const QString& bridgeStartedAt)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    auto result = QJsonObject{
      {"bridgeInstanceId", bridgeInstanceId},
      {"bridgeStartedAt", bridgeStartedAt},
      {"historyCount", static_cast<int>(history.size())},
      {"lastOperationId", history.empty() ? QString{} : history.back().operationId},
      {"canUndoLatestMcpOperation", false},
      {"reasonIfUnavailable", "noMcpMutationYet"},
    };
    result.insert("activeDocument", false);
    result.insert("reasonIfUnavailable", "noActiveDocument");
    return McpBridgeToolResult::success(result);
  }

  auto& map = mapWindow->document().map();
  return historyStatusForMapResult(
    map,
    history,
    objectRegistry,
    bridgeInstanceId,
    bridgeStartedAt,
    pathAsQString(map.path()));
}

McpBridgeToolResult historyStatusForMapResult(
  mdl::Map& map,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry& objectRegistry,
  const QString& bridgeInstanceId,
  const QString& bridgeStartedAt,
  const QString& activeDocumentPath)
{
  auto result = QJsonObject{
    {"bridgeInstanceId", bridgeInstanceId},
    {"bridgeStartedAt", bridgeStartedAt},
    {"historyCount", static_cast<int>(history.size())},
    {"lastOperationId", history.empty() ? QString{} : history.back().operationId},
    {"canUndoLatestMcpOperation", false},
    {"reasonIfUnavailable", "noMcpMutationYet"},
  };

  result.insert("activeDocument", true);
  result.insert("activeDocumentPath", activeDocumentPath);
  result.insert("activeDocumentFingerprint", objectRegistry.documentFingerprint(map));
  result.insert("documentEpoch", objectRegistry.documentEpoch(map));

  if (history.empty())
  {
    return McpBridgeToolResult::success(result);
  }

  const auto latestIt = std::find_if(
    history.rbegin(), history.rend(), [](const auto& op) { return !op.undone; });
  if (latestIt == history.rend())
  {
    result.insert("reasonIfUnavailable", "allMcpOperationsUndone");
    return McpBridgeToolResult::success(result);
  }

  result.insert("latestUndoCandidateId", latestIt->operationId);
  result.insert("latestUndoCandidateTransactionName", latestIt->transactionName);
  const auto* undoName = map.undoCommandName();
  result.insert(
    "nativeUndoCommandName",
    undoName != nullptr ? QString::fromStdString(*undoName) : QString{});
  if (
    undoName != nullptr && QString::fromStdString(*undoName) == latestIt->transactionName)
  {
    result.insert("canUndoLatestMcpOperation", true);
    result.insert("selectionCommandsAboveLatestMcpOperation", 0);
    result.insert("reasonIfUnavailable", QJsonValue{});
  }
  else if (
    undoName != nullptr && isSelectionCommandName(QString::fromStdString(*undoName)))
  {
    result.insert("canUndoLatestMcpOperation", true);
    result.insert("selectionCommandsAboveLatestMcpOperation", 1);
    result.insert("reasonIfUnavailable", QJsonValue{});
    result.insert(
      "note",
      "A selection-only command is above the latest MCP operation; history_undo_mcp "
      "will skip it before undoing the MCP transaction.");
  }
  else
  {
    result.insert("reasonIfUnavailable", "nativeUndoStackTopDoesNotMatchMcp");
  }
  return McpBridgeToolResult::success(result);
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

  return McpBridgeToolResult::success(
    operationRecordDetailJson(*operation, detailFromParams(params)));
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

  const auto detail = detailFromParams(params);
  auto result = operationRecordDetailJson(*operation, detail);
  if (auto* mapWindow = appController.mapWindowManager().topMapWindow())
  {
    auto liveState = objectRegistry.liveStateJson(
      mapWindow->document().map(),
      operation->changedObjectIds,
      operation->undone,
      detail == "full");
    if (operation->operationKind == "delete")
    {
      liveState.insert("valid", !operation->undone);
      liveState.insert("targetsLive", false);
      liveState.insert(
        "staleReason",
        "delete operation records objects that were removed by the transaction");
      liveState.insert(
        "suggestedAction",
        "Use deletedObjectIds for audit only; redo/undo the delete operation to change "
        "map state.");
    }
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

  auto result = operationRecordDetailJson(*operation, detailFromParams(params));
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

  return operationValidateForMapResult(
    mapWindow->document().map(), history, params, objectRegistry);
}

McpBridgeToolResult operationValidateForMapResult(
  mdl::Map& map,
  const std::vector<McpOperationRecord>& history,
  const QJsonObject& params,
  const McpObjectRegistry& objectRegistry)
{
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

  const auto detail = detailFromParams(params);
  auto liveState = objectRegistry.liveStateJson(
    map, operation->changedObjectIds, operation->undone, detail == "full");
  if (operation->operationKind == "delete")
  {
    liveState.insert("valid", !operation->undone);
    liveState.insert("targetsLive", false);
    liveState.insert(
      "staleReason",
      "delete operation records objects that were removed by the transaction");
    liveState.insert(
      "suggestedAction",
      "Use deletedObjectIds for audit only; redo/undo the delete operation to change "
      "map state.");
  }
  const auto liveObjectCount = liveState.value("liveObjectCount").toInt();
  const auto staleObjectCount = liveState.value("staleObjectCount").toInt();
  const auto mismatchCount = liveState.value("mismatchCount").toInt();

  auto result = QJsonObject{
    {"operationId", operationId},
    {"valid", liveState.value("valid").toBool(false)},
    {"undone", operation->undone},
    {"operationKind",
     operation->operationKind.isEmpty() ? "mutation" : operation->operationKind},
    {"changedObjectCount", operation->changedObjectIds.size()},
    {"deletedObjectCount", operation->deletedObjectIds.size()},
    {"liveObjectCount", liveObjectCount},
    {"staleObjectCount", staleObjectCount},
    {"mismatchCount", mismatchCount},
  };
  if (detail == "ids" || detail == "full")
  {
    result.insert("changedObjectIds", operation->changedObjectIdsJson());
    result.insert("deletedObjectIds", operation->deletedObjectIdsJson());
  }
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

QJsonArray stringListJson(const QStringList& values)
{
  auto result = QJsonArray{};
  for (const auto& value : values)
  {
    result.push_back(value);
  }
  return result;
}

QStringList remainingOperationIdsToTarget(
  const std::vector<McpOperationRecord>& history, const QString& targetOperationId)
{
  auto result = QStringList{};
  for (auto it = history.rbegin(); it != history.rend(); ++it)
  {
    if (it->undone)
    {
      continue;
    }
    result.push_back(it->operationId);
    if (it->operationId == targetOperationId)
    {
      break;
    }
  }
  return result;
}

McpBridgeToolResult historyUndoToOperationResult(
  AppController& appController,
  std::vector<McpOperationRecord>& history,
  const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return historyUndoToOperationForMapResult(mapWindow->document().map(), history, params);
}

McpBridgeToolResult historyUndoToOperationForMapResult(
  mdl::Map& map, std::vector<McpOperationRecord>& history, const QJsonObject& params)
{
  const auto targetOperationId = params.value("operationId").toString().trimmed();
  if (targetOperationId.isEmpty())
  {
    return invalidParamsFailure("history_undo_to_operation requires operationId");
  }

  const auto targetIndex = findOperationIndex(history, targetOperationId);
  if (!targetIndex)
  {
    return invalidParamsFailure(
      QString{"Unknown MCP operation id: %1"}.arg(targetOperationId));
  }
  if (history[*targetIndex].undone)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden,
      "Target MCP operation is already undone",
      QJsonObject{
        {"mutatedDocument", false},
        {"targetOperationId", targetOperationId},
        {"recoveryAction", "refresh_status_or_validate"},
      });
  }

  auto undoneOperationIds = QStringList{};
  auto skippedSelectionCommandCount = 0;

  while (true)
  {
    auto it = std::find_if(history.rbegin(), history.rend(), [](const auto& operation) {
      return !operation.undone;
    });
    if (it == history.rend())
    {
      return McpBridgeToolResult::failure(
        mcp::McpErrorCode::Forbidden,
        "No MCP operation remains to undo before reaching target",
        QJsonObject{
          {"mutatedDocument", !undoneOperationIds.isEmpty()},
          {"partiallyUndone", !undoneOperationIds.isEmpty()},
          {"targetOperationId", targetOperationId},
          {"undoneOperationIds", stringListJson(undoneOperationIds)},
          {"remainingOperationIds",
           stringListJson(remainingOperationIdsToTarget(history, targetOperationId))},
          {"recoveryAction", "refresh_status_or_validate"},
        });
    }

    auto skippedThisOperation = 0;
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
      ++skippedThisOperation;
      ++skippedSelectionCommandCount;
    }

    const auto* undoName = map.undoCommandName();
    if (!undoName || QString::fromStdString(*undoName) != it->transactionName)
    {
      return McpBridgeToolResult::failure(
        mcp::McpErrorCode::Forbidden,
        "The next MCP operation to undo is no longer on top of the native undo stack",
        QJsonObject{
          {"mutatedDocument", !undoneOperationIds.isEmpty()},
          {"partiallyUndone", !undoneOperationIds.isEmpty()},
          {"targetOperationId", targetOperationId},
          {"blockedOperationId", it->operationId},
          {"nativeUndoCommandName",
           undoName != nullptr ? QString::fromStdString(*undoName) : QString{}},
          {"undoneOperationIds", stringListJson(undoneOperationIds)},
          {"remainingOperationIds",
           stringListJson(remainingOperationIdsToTarget(history, targetOperationId))},
          {"skippedSelectionCommandCount", skippedSelectionCommandCount},
          {"skippedSelectionCommandsForBlockedOperation", skippedThisOperation},
          {"recoveryAction", "refresh_status_or_validate"},
        });
    }

    const auto operationId = it->operationId;
    map.undoCommand();
    it->undone = true;
    undoneOperationIds.push_back(operationId);

    if (operationId == targetOperationId)
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"mutatedDocument", true},
        {"undone", true},
        {"targetOperationId", targetOperationId},
        {"undoneOperationIds", stringListJson(undoneOperationIds)},
        {"undoneCount", undoneOperationIds.size()},
        {"skippedSelectionCommandCount", skippedSelectionCommandCount},
      });
    }
  }
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
