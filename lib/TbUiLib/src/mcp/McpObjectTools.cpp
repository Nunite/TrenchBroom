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
#include <QStringList>

#include "McpBridgeServerTools.h"
#include "McpResponseUtils.h"
#include "McpSelectionQuery.h"
#include "mcp/McpError.h"
#include "mdl/AddRemoveNodesCommand.h"
#include "mdl/EditorContext.h"
#include "mdl/Map.h"
#include "mdl/Map_Geometry.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Node.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/mcp/McpObjectRegistry.h"

#include "kd/vector_utils.h"

#include "vm/bbox.h"
#include "vm/mat_ext.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <vector>

namespace tb::ui
{
namespace mcp = tb::mcp;

namespace
{

QString nodePathId(const mdl::Node& node, const mdl::WorldNode& worldNode)
{
  if (&node == &worldNode)
  {
    return "node:world";
  }

  auto parts = QStringList{};
  for (const auto index : node.pathFrom(worldNode).indices)
  {
    parts.push_back(QString::number(index));
  }
  return QString{"node:%1"}.arg(parts.join('/'));
}

std::optional<mdl::NodePath> parseNodePathId(const QString& id)
{
  if (id == "node:world")
  {
    return mdl::NodePath{};
  }

  static const auto Prefix = QString{"node:"};
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

mdl::Node* resolveNodeId(mdl::WorldNode& worldNode, const QString& id)
{
  const auto path = parseNodePathId(id);
  if (!path)
  {
    return nullptr;
  }
  return worldNode.resolvePath(*path);
}

QString makeOperationId(int& nextOperationIndex)
{
  return QString{"mcp-op-%1"}.arg(nextOperationIndex++);
}

QJsonArray vecToJson(const vm::vec3d& value)
{
  return QJsonArray{value.x(), value.y(), value.z()};
}

QJsonObject boundsToJson(const vm::bbox3d& bounds)
{
  return QJsonObject{
    {"min", vecToJson(bounds.min)},
    {"max", vecToJson(bounds.max)},
  };
}

QJsonObject mutationResultJson(
  const McpOperationRecord& operation, const QString& idsMode)
{
  auto result = QJsonObject{};
  result.insert("operationId", operation.operationId);
  result.insert("transactionName", operation.transactionName);
  mcpApplyChangedObjectIdsMode(result, operation.changedObjectIdsJson(), idsMode);
  return result;
}

void mcpRecordOperation(
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const QString& toolName,
  const QString& transactionName,
  const QJsonArray& changedObjectIds,
  QJsonObject& result,
  const QString& idsMode = "count")
{
  auto operation = McpOperationRecord{};
  operation.operationId = makeOperationId(nextOperationIndex);
  operation.toolName = toolName;
  operation.transactionName = transactionName;
  operation.setChangedObjectIds(changedObjectIds);
  result = mutationResultJson(operation, idsMode);
  history.push_back(std::move(operation));
}

void markDeleteOperation(
  std::vector<McpOperationRecord>& history,
  QJsonObject& result,
  const QJsonArray& deletedObjectIds,
  const QString& idsMode)
{
  if (!history.empty())
  {
    auto& operation = history.back();
    operation.operationKind = "delete";
    operation.setChangedObjectIds(QJsonArray{});
    operation.setDeletedObjectIds(deletedObjectIds);
  }
  result.insert("operationKind", "delete");
  result.insert("changedObjectIds", QJsonArray{});
  result.insert("changedObjectCount", 0);
  mcpApplyDeletedObjectIdsMode(result, deletedObjectIds, idsMode);
}

std::optional<vm::vec3d> mcpVec3FromJson(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto value = params.value(key);
  if (!value.isArray())
  {
    error = QString{"%1 must be an array of three numbers"}.arg(key);
    return std::nullopt;
  }

  const auto array = value.toArray();
  if (array.size() != 3)
  {
    error = QString{"%1 must contain exactly three numbers"}.arg(key);
    return std::nullopt;
  }

  auto components = std::array<double, 3>{};
  for (auto i = 0; i < 3; ++i)
  {
    if (!array[i].isDouble())
    {
      error = QString{"%1[%2] must be a number"}.arg(key).arg(i);
      return std::nullopt;
    }
    components[static_cast<size_t>(i)] = array[i].toDouble();
  }

  return vm::vec3d{components[0], components[1], components[2]};
}

std::optional<double> numberFromJson(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto value = params.value(key);
  if (!value.isDouble())
  {
    error = QString{"%1 must be a number"}.arg(key);
    return std::nullopt;
  }

  const auto result = value.toDouble();
  if (!std::isfinite(result))
  {
    error = QString{"%1 must be finite"}.arg(key);
    return std::nullopt;
  }
  return result;
}

std::optional<std::vector<QString>> stringListFromJson(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto value = params.value(key);
  if (!value.isArray())
  {
    error = QString{"%1 must be an array"}.arg(key);
    return std::nullopt;
  }

  auto result = std::vector<QString>{};
  for (const auto& item : value.toArray())
  {
    if (!item.isString())
    {
      error = QString{"%1 must contain only strings"}.arg(key);
      return std::nullopt;
    }
    result.push_back(item.toString());
  }
  return result;
}

bool executeTransaction(
  mdl::Map& map, const QString& transactionName, const std::function<bool()>& operation)
{
  auto transaction = mdl::Transaction{map, transactionName.toStdString()};
  if (!operation())
  {
    transaction.cancel();
    return false;
  }
  return transaction.commit();
}

std::optional<QJsonArray> removeNodesWithTransaction(
  mdl::Map& map, const QString& transactionName, std::vector<mdl::Node*> nodes)
{
  nodes = kdl::vec_sort_and_remove_duplicates(std::move(nodes));
  nodes.erase(
    std::remove_if(
      std::begin(nodes),
      std::end(nodes),
      [&](const auto* node) {
        return node == &map.worldNode()
               || std::ranges::any_of(nodes, [&](const auto* other) {
                    return node != other && node->isDescendantOf(*other);
                  });
      }),
    std::end(nodes));

  if (nodes.empty())
  {
    return std::nullopt;
  }

  auto nodesByParent = std::map<mdl::Node*, std::vector<mdl::Node*>>{};
  auto removedIds = QJsonArray{};
  for (auto* node : nodes)
  {
    auto* parent = node->parent();
    if (!parent || !parent->canRemoveChild(*node))
    {
      return std::nullopt;
    }
    nodesByParent[parent].push_back(node);
    removedIds.push_back(nodePathId(*node, map.worldNode()));
  }

  const auto ok = executeTransaction(map, transactionName, [&]() {
    mdl::deselectNodes(map, nodes);
    return map.executeAndStore(mdl::AddRemoveNodesCommand::remove(nodesByParent));
  });

  return ok ? std::optional{removedIds} : std::nullopt;
}

std::optional<std::vector<mdl::Node*>> nodesFromObjectIds(
  mdl::Map& map, const QJsonObject& params, QString& error)
{
  const auto objectIds = stringListFromJson(params, "objectIds", error);
  if (!objectIds)
  {
    return std::nullopt;
  }
  if (objectIds->empty())
  {
    error = "objectIds must not be empty";
    return std::nullopt;
  }

  auto result = std::vector<mdl::Node*>{};
  for (const auto& objectId : *objectIds)
  {
    auto* node = resolveNodeId(map.worldNode(), objectId);
    if (!node)
    {
      error = QString{"Unknown MCP object id: %1"}.arg(objectId);
      return std::nullopt;
    }
    result.push_back(node);
  }

  return kdl::vec_sort_and_remove_duplicates(std::move(result));
}

std::optional<QStringList> operationIdsFromParams(
  const QJsonObject& params, QString& error)
{
  auto result = QStringList{};
  const auto operationId = params.value("operationId").toString().trimmed();
  if (!operationId.isEmpty())
  {
    result.push_back(operationId);
  }

  const auto operationIdsValue = params.value("operationIds");
  if (!operationIdsValue.isUndefined())
  {
    if (!operationIdsValue.isArray())
    {
      error = "operationIds must be an array";
      return std::nullopt;
    }
    for (const auto& value : operationIdsValue.toArray())
    {
      if (!value.isString())
      {
        error = "operationIds must contain only strings";
        return std::nullopt;
      }
      const auto id = value.toString().trimmed();
      if (!id.isEmpty())
      {
        result.push_back(id);
      }
    }
  }

  result.removeDuplicates();
  return result;
}

std::optional<const McpOperationRecord*> findOperation(
  const std::vector<McpOperationRecord>& history, const QString& operationId)
{
  const auto it = std::ranges::find_if(
    history, [&](const auto& operation) { return operation.operationId == operationId; });
  if (it == history.end())
  {
    return std::nullopt;
  }
  return &*it;
}

std::optional<std::vector<mdl::Node*>> nodesFromOperation(
  mdl::Map& map,
  const McpOperationRecord& operation,
  const McpObjectRegistry& objectRegistry,
  QString& error)
{
  if (operation.undone)
  {
    error = QString{"MCP operation is already undone: %1"}.arg(operation.operationId);
    return std::nullopt;
  }

  auto result = std::vector<mdl::Node*>{};
  for (const auto& objectId : operation.changedObjectIds)
  {
    const auto resolved = objectRegistry.resolveExternalId(map, objectId);
    if (!resolved.ok)
    {
      error = resolved.error;
      return std::nullopt;
    }
    auto* node = resolveNodeId(map.worldNode(), resolved.legacyPathId);
    if (!node)
    {
      error = QString{"Operation object no longer resolves: %1"}.arg(objectId);
      return std::nullopt;
    }
    if (node == &map.worldNode() || !map.editorContext().selectable(*node))
    {
      error = QString{"Operation object cannot be edited: %1"}.arg(objectId);
      return std::nullopt;
    }
    result.push_back(node);
  }

  if (result.empty())
  {
    error =
      QString{"MCP operation has no changed objects: %1"}.arg(operation.operationId);
    return std::nullopt;
  }

  return kdl::vec_sort_and_remove_duplicates(std::move(result));
}

std::optional<std::vector<mdl::Node*>> nodesFromObjectIdsOrOperations(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry& objectRegistry,
  QString& error,
  QJsonObject* diagnostics = nullptr,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore = nullptr,
  const std::map<QString, McpModuleRecord>* moduleStore = nullptr)
{
  auto result = std::vector<mdl::Node*>{};
  auto source = QString{};
  auto selectorWarnings = QJsonArray{};
  const auto objectIdsProvided = params.value("objectIds").isArray();
  if (objectIdsProvided)
  {
    auto objectNodes = nodesFromObjectIds(map, params, error);
    if (!objectNodes)
    {
      return std::nullopt;
    }
    result.insert(std::end(result), std::begin(*objectNodes), std::end(*objectNodes));
    source = "objectIds";
  }

  auto operationIds = operationIdsFromParams(params, error);
  if (!operationIds)
  {
    return std::nullopt;
  }
  const auto operationIdsProvided = !operationIds->isEmpty();
  if (!objectIdsProvided)
  {
    for (const auto& operationId : *operationIds)
    {
      const auto operation = findOperation(history, operationId);
      if (!operation)
      {
        error = QString{"Unknown MCP operation id: %1"}.arg(operationId);
        return std::nullopt;
      }
      auto operationNodes = nodesFromOperation(map, **operation, objectRegistry, error);
      if (!operationNodes)
      {
        return std::nullopt;
      }
      result.insert(
        std::end(result), std::begin(*operationNodes), std::end(*operationNodes));
    }
    if (operationIdsProvided)
    {
      source = "operationIds";
    }
  }

  if (
    !objectIdsProvided && !operationIdsProvided && result.empty()
    && params.value("selector").isObject() && metadataStore != nullptr
    && moduleStore != nullptr)
  {
    auto selectorDiagnostics = McpSelectorDiagnostics{};
    const auto selector = selectorFromParams(params);
    result = resolveSelectorNodes(
      map,
      selector,
      history,
      *metadataStore,
      *moduleStore,
      objectRegistry,
      selectorWarnings,
      error,
      &selectorDiagnostics);
    if (!error.isEmpty())
    {
      return std::nullopt;
    }
    source = "selector";
    if (diagnostics != nullptr)
    {
      diagnostics->insert("selector", selector);
      diagnostics->insert("selectorMatchedCount", selectorDiagnostics.matchedBeforeLimit);
      diagnostics->insert("matchedBeforeLimit", selectorDiagnostics.matchedBeforeLimit);
      diagnostics->insert("limitApplied", selectorDiagnostics.limitApplied);
      diagnostics->insert("staleExcluded", selectorDiagnostics.staleExcluded);
      diagnostics->insert("selectorWarnings", selectorWarnings);
    }
  }

  result = kdl::vec_sort_and_remove_duplicates(std::move(result));
  if (diagnostics != nullptr)
  {
    diagnostics->insert("targetSource", source);
    diagnostics->insert("resolvedObjectCount", static_cast<int>(result.size()));
    if (operationIdsProvided)
    {
      diagnostics->insert(
        "sourceOperationIds", QJsonArray::fromStringList(*operationIds));
      diagnostics->insert("sourceOperationCount", operationIds->size());
    }
  }
  if (result.empty())
  {
    if (objectIdsProvided)
    {
      error = "objectIds must contain at least one live transform target";
    }
    else if (operationIdsProvided)
    {
      error = "operationIds must resolve to at least one live transform target";
    }
    else if (params.value("selector").isObject())
    {
      error = "selector resolved to no live transform targets";
    }
    else
    {
      error =
        "objects_transform requires objectIds, operationId, operationIds, or selector "
        "that resolves to live objects";
    }
    return std::nullopt;
  }
  return result;
}

std::vector<mdl::Node*> removeDescendantNodes(std::vector<mdl::Node*> nodes)
{
  nodes = kdl::vec_sort_and_remove_duplicates(std::move(nodes));
  nodes.erase(
    std::remove_if(
      std::begin(nodes),
      std::end(nodes),
      [&](const auto* node) {
        return std::ranges::any_of(nodes, [&](const auto* other) {
          return node != other && node->isDescendantOf(*other);
        });
      }),
    std::end(nodes));
  return nodes;
}

vm::bbox3d boundsForNodes(const std::vector<mdl::Node*>& nodes)
{
  auto builder = vm::bbox3d::builder{};
  for (const auto* node : nodes)
  {
    builder.add(node->logicalBounds());
  }
  return builder.bounds();
}

QJsonArray objectIdsJson(
  mdl::Map& map,
  const std::vector<mdl::Node*>& nodes,
  const McpObjectRegistry& objectRegistry)
{
  auto result = QJsonArray{};
  for (const auto* node : nodes)
  {
    if (node == nullptr)
    {
      continue;
    }
    result.push_back(
      objectRegistry.externalIdForLegacy(map, nodePathId(*node, map.worldNode())));
  }
  return result;
}

void applyIdsMode(
  QJsonObject& result,
  mdl::Map& map,
  const std::vector<mdl::Node*>& nodes,
  const McpObjectRegistry& objectRegistry,
  const QString& idsMode,
  const int sampleLimit)
{
  const auto normalized = idsMode.trimmed().toLower();
  result.remove("changedObjectIds");
  if (normalized == "full")
  {
    const auto ids = objectIdsJson(map, nodes, objectRegistry);
    result.insert("objectIds", ids);
    result.insert("changedObjectIds", ids);
    return;
  }
  if (normalized == "sample")
  {
    const auto clampedSampleLimit = std::clamp(sampleLimit, 0, 100);
    auto sampleNodes = nodes;
    if (static_cast<int>(sampleNodes.size()) > clampedSampleLimit)
    {
      sampleNodes.resize(static_cast<size_t>(clampedSampleLimit));
    }
    result.insert("objectIdSample", objectIdsJson(map, sampleNodes, objectRegistry));
    result.insert("objectIdCount", static_cast<int>(nodes.size()));
    return;
  }
  result.insert("objectIdCount", static_cast<int>(nodes.size()));
}

std::optional<vm::vec3d> optionalCenterFromJson(
  const QJsonObject& params, const std::vector<mdl::Node*>& nodes, QString& error)
{
  if (!params.contains("center"))
  {
    return boundsForNodes(nodes).center();
  }
  return mcpVec3FromJson(params, "center", error);
}

std::optional<vm::vec3d> transformAxisFromJson(const QJsonObject& params, QString& error)
{
  const auto value = params.value("axis");
  if (value.isUndefined())
  {
    return vm::vec3d{0, 0, 1};
  }
  if (value.isString())
  {
    const auto axis = value.toString().trimmed().toLower();
    if (axis == "x")
    {
      return vm::vec3d{1, 0, 0};
    }
    if (axis == "y")
    {
      return vm::vec3d{0, 1, 0};
    }
    if (axis == "z")
    {
      return vm::vec3d{0, 0, 1};
    }
    error = "axis must be x, y, z, or an array of three numbers";
    return std::nullopt;
  }
  if (value.isArray())
  {
    auto axis = mcpVec3FromJson(params, "axis", error);
    if (!axis)
    {
      return std::nullopt;
    }
    if (vm::is_zero(*axis, vm::Cd::almost_zero()))
    {
      error = "axis vector must not be zero";
      return std::nullopt;
    }
    return vm::normalize(*axis);
  }

  error = "axis must be x, y, z, or an array of three numbers";
  return std::nullopt;
}

std::optional<vm::vec3d> scaleFactorsFromJson(const QJsonObject& params, QString& error)
{
  const auto value = params.value("scale");
  if (value.isDouble())
  {
    const auto factor = value.toDouble();
    if (!std::isfinite(factor) || factor == 0.0)
    {
      error = "scale must be finite and non-zero";
      return std::nullopt;
    }
    return vm::vec3d{factor, factor, factor};
  }
  if (value.isArray())
  {
    const auto factors = mcpVec3FromJson(params, "scale", error);
    if (!factors)
    {
      return std::nullopt;
    }
    if (
      factors->x() == 0.0 || factors->y() == 0.0 || factors->z() == 0.0
      || !std::isfinite(factors->x()) || !std::isfinite(factors->y())
      || !std::isfinite(factors->z()))
    {
      error = "scale factors must be finite and non-zero";
      return std::nullopt;
    }
    return factors;
  }

  error = "scale must be a number or an array of three numbers";
  return std::nullopt;
}

} // namespace

McpBridgeToolResult deleteObjectsResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return deleteObjectsForMapResult(
    mapWindow->document().map(), toolName, params, history, nextOperationIndex);
}

McpBridgeToolResult deleteObjectsForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto error = QString{};
  const auto nodes = nodesFromObjectIds(map, params, error);
  if (!nodes)
  {
    return invalidParamsFailure(error);
  }

  const auto transactionName = QString{"MCP: Delete objects"};
  const auto changedObjectIds = removeNodesWithTransaction(map, transactionName, *nodes);
  if (!changedObjectIds)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden, "One or more objects cannot be deleted");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    toolName,
    transactionName,
    *changedObjectIds,
    result,
    mcpIdsModeFromParams(params));
  markDeleteOperation(history, result, *changedObjectIds, mcpIdsModeFromParams(params));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult deleteObjectsByFilterResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto& map = mapWindow->document().map();
  const auto hasFilter =
    !params.value("type").toString().trimmed().isEmpty()
    || !params.value("classname").toString().trimmed().isEmpty()
    || !params.value("targetname").toString().trimmed().isEmpty()
    || !params.value("material").toString().trimmed().isEmpty()
    || !params.value("query").toString().trimmed().isEmpty()
    || (!params.value("min").isUndefined() && !params.value("max").isUndefined());
  if (!hasFilter)
  {
    return invalidParamsFailure(
      "objects_delete_by_filter requires at least one filter: type, classname, "
      "targetname, material, query, or bounds");
  }
  if (
    params.value("type").toString().trimmed().compare("world", Qt::CaseInsensitive) == 0)
  {
    return invalidParamsFailure("objects_delete_by_filter cannot delete world");
  }

  auto error = QString{};
  auto options = McpSelectionQueryOptions{};
  options.excludeWorld = true;
  options.selectableOnly = true;
  options.leafOnly = params.value("leafOnly").isUndefined()
                       ? false
                       : params.value("leafOnly").toBool(false);
  options.exactTypeOnly = true;
  options.removeDescendantMatches = true;
  auto nodes = mcpFilteredNodes(map, params, options, error);
  if (!error.isEmpty())
  {
    return invalidParamsFailure(error);
  }
  if (nodes.empty())
  {
    return invalidParamsFailure("objects_delete_by_filter matched no deletable objects");
  }

  const auto transactionName = QString{"MCP: Delete objects by filter"};
  const auto changedObjectIds = removeNodesWithTransaction(map, transactionName, nodes);
  if (!changedObjectIds)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden, "Matched objects cannot be deleted");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    toolName,
    transactionName,
    *changedObjectIds,
    result,
    mcpIdsModeFromParams(params));
  markDeleteOperation(history, result, *changedObjectIds, mcpIdsModeFromParams(params));
  result.insert("matchedCount", changedObjectIds->size());
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult deleteObjectsByOperationResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return deleteObjectsByOperationForMapResult(
    mapWindow->document().map(),
    toolName,
    params,
    history,
    nextOperationIndex,
    objectRegistry);
}

McpBridgeToolResult deleteObjectsByOperationForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry)
{
  const auto operationId = params.value("operationId").toString().trimmed();
  if (operationId.isEmpty())
  {
    return invalidParamsFailure("objects_delete_by_operation requires operationId");
  }
  const auto operation = findOperation(history, operationId);
  if (!operation)
  {
    return invalidParamsFailure(QString{"Unknown MCP operation id: %1"}.arg(operationId));
  }

  auto error = QString{};
  const auto nodes = nodesFromOperation(map, **operation, objectRegistry, error);
  if (!nodes)
  {
    return invalidParamsFailure(error);
  }

  const auto transactionName =
    QString{"MCP: Delete operation %1 objects"}.arg(operationId);
  auto deletedObjectIds = QJsonArray{};
  for (const auto& objectId : (**operation).changedObjectIds)
  {
    deletedObjectIds.push_back(objectId);
  }
  const auto changedObjectIds = removeNodesWithTransaction(map, transactionName, *nodes);
  if (!changedObjectIds)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden, "Operation objects cannot be deleted");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    toolName,
    transactionName,
    *changedObjectIds,
    result,
    mcpIdsModeFromParams(params));
  markDeleteOperation(history, result, deletedObjectIds, mcpIdsModeFromParams(params));
  result.insert("sourceOperationId", operationId);
  result.insert("deletedCount", deletedObjectIds.size());
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult transformObjectsResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore,
  const std::map<QString, McpModuleRecord>* moduleStore)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return transformObjectsForMapResult(
    mapWindow->document().map(),
    toolName,
    params,
    history,
    nextOperationIndex,
    objectRegistry,
    metadataStore,
    moduleStore);
}

McpBridgeToolResult transformObjectsForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry)
{
  return transformObjectsForMapResult(
    map, toolName, params, history, nextOperationIndex, objectRegistry, nullptr, nullptr);
}

McpBridgeToolResult transformObjectsForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore,
  const std::map<QString, McpModuleRecord>* moduleStore)
{
  auto error = QString{};
  auto targetDiagnostics = QJsonObject{};
  const auto nodes = nodesFromObjectIdsOrOperations(
    map,
    params,
    history,
    objectRegistry,
    error,
    &targetDiagnostics,
    metadataStore,
    moduleStore);
  if (!nodes)
  {
    if (!targetDiagnostics.isEmpty())
    {
      return McpBridgeToolResult::failure(
        mcp::McpErrorCode::InvalidParams, error, targetDiagnostics);
    }
    return invalidParamsFailure(error);
  }
  auto transformNodes = removeDescendantNodes(*nodes);
  if (transformNodes.empty())
  {
    return invalidParamsFailure(
      "objectIds must contain at least one transformable object");
  }
  const auto beforeBounds = boundsForNodes(transformNodes);
  for (const auto* node : transformNodes)
  {
    if (node == &map.worldNode() || !map.editorContext().selectable(*node))
    {
      return invalidParamsFailure(QString{"Object cannot be transformed: %1"}.arg(
        nodePathId(*node, map.worldNode())));
    }
  }

  const auto operation = params.value("operation").toString().trimmed().toLower();
  if (operation.isEmpty())
  {
    return invalidParamsFailure("objects_transform requires operation");
  }

  auto transformation = vm::mat4x4d{};
  auto transactionName = QString{};
  if (operation == "translate")
  {
    const auto delta = mcpVec3FromJson(params, "delta", error);
    if (!delta)
    {
      return invalidParamsFailure(error);
    }
    transformation = vm::translation_matrix(*delta);
    transactionName = "MCP: Translate objects";
  }
  else if (operation == "rotate")
  {
    const auto center = optionalCenterFromJson(params, transformNodes, error);
    if (!center)
    {
      return invalidParamsFailure(error);
    }
    const auto axis = transformAxisFromJson(params, error);
    if (!axis)
    {
      return invalidParamsFailure(error);
    }
    const auto angle = numberFromJson(params, "angle", error);
    if (!angle)
    {
      return invalidParamsFailure(error);
    }
    transformation = vm::translation_matrix(*center)
                     * vm::rotation_matrix(*axis, vm::to_radians(*angle))
                     * vm::translation_matrix(-*center);
    transactionName = "MCP: Rotate objects";
  }
  else if (operation == "scale")
  {
    const auto center = optionalCenterFromJson(params, transformNodes, error);
    if (!center)
    {
      return invalidParamsFailure(error);
    }
    const auto factors = scaleFactorsFromJson(params, error);
    if (!factors)
    {
      return invalidParamsFailure(error);
    }
    transformation = vm::translation_matrix(*center) * vm::scaling_matrix(*factors)
                     * vm::translation_matrix(-*center);
    transactionName = "MCP: Scale objects";
  }
  else
  {
    return invalidParamsFailure("operation must be translate, rotate, or scale");
  }

  auto changedObjectIds = QJsonArray{};
  for (const auto* node : transformNodes)
  {
    changedObjectIds.push_back(nodePathId(*node, map.worldNode()));
  }

  mdl::deselectAll(map);
  mdl::selectNodes(map, transformNodes);
  const auto ok =
    mdl::transformSelection(map, transactionName.toStdString(), transformation);
  if (!ok)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError,
      QString{"Could not transform objects with operation: %1"}.arg(operation));
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    toolName,
    transactionName,
    changedObjectIds,
    result,
    mcpIdsModeFromParams(params));
  const auto afterBounds = boundsForNodes(transformNodes);
  result.insert("operation", operation);
  result.insert("bounds", boundsToJson(afterBounds));
  result.insert("beforeBounds", boundsToJson(beforeBounds));
  result.insert("afterBounds", boundsToJson(afterBounds));
  result.insert("targetCount", static_cast<int>(transformNodes.size()));
  result.insert(
    "resolvedObjectCount", targetDiagnostics.value("resolvedObjectCount").toInt());
  result.insert("selectedCount", static_cast<int>(transformNodes.size()));
  result.insert("targetSource", targetDiagnostics.value("targetSource").toString());
  if (targetDiagnostics.contains("selector"))
  {
    result.insert("selector", targetDiagnostics.value("selector"));
    result.insert(
      "selectorMatchedCount", targetDiagnostics.value("selectorMatchedCount"));
    result.insert("matchedBeforeLimit", targetDiagnostics.value("matchedBeforeLimit"));
    result.insert("limitApplied", targetDiagnostics.value("limitApplied"));
    result.insert("staleExcluded", targetDiagnostics.value("staleExcluded"));
    result.insert("warnings", targetDiagnostics.value("selectorWarnings").toArray());
  }
  if (auto operationIds = operationIdsFromParams(params, error);
      operationIds && !operationIds->isEmpty())
  {
    result.insert("sourceOperationIds", QJsonArray::fromStringList(*operationIds));
    result.insert("sourceOperationCount", operationIds->size());
  }
  applyIdsMode(
    result,
    map,
    transformNodes,
    objectRegistry,
    params.value("idsMode").toString("count"),
    params.value("sampleLimit").toInt(12));
  result.insert(
    "validation",
    QJsonObject{
      {"valid", true},
      {"errors", QJsonArray{}},
    });
  return McpBridgeToolResult::success(std::move(result));
}

} // namespace tb::ui
