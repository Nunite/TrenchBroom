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

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include "McpBridgeServerTools.h"
#include "McpSelectionQuery.h"
#include "mcp/McpError.h"
#include "mdl/AddRemoveNodesCommand.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/Map.h"
#include "mdl/Map_Selection.h"
#include "mdl/Node.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/mcp/McpObjectRegistry.h"

#include "vm/bbox.h"

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <vector>

namespace tb::ui
{
namespace mcp = tb::mcp;

namespace
{

constexpr auto DefaultSampleLimit = 12;

struct SelectorDiagnosticsInternal
{
  int matchedBeforeLimit = 0;
  bool limitApplied = false;
  int staleExcluded = 0;
};

QJsonArray stringsToJson(const QStringList& values)
{
  auto result = QJsonArray{};
  for (const auto& value : values)
  {
    result.push_back(value);
  }
  return result;
}

QJsonArray stringsToJson(const std::vector<QString>& values)
{
  auto result = QJsonArray{};
  for (const auto& value : values)
  {
    result.push_back(value);
  }
  return result;
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

vm::bbox3d boundsForNodes(const std::vector<mdl::Node*>& nodes)
{
  auto first = true;
  auto result = vm::bbox3d{};
  for (const auto* node : nodes)
  {
    if (node == nullptr)
    {
      continue;
    }
    if (first)
    {
      result = node->logicalBounds();
      first = false;
    }
    else
    {
      result = vm::merge(result, node->logicalBounds());
    }
  }
  return result;
}

bool metadataValueMatches(const QJsonValue& actual, const QJsonValue& expected)
{
  if (expected.isString())
  {
    return actual.toString().compare(expected.toString(), Qt::CaseInsensitive) == 0;
  }
  return actual == expected;
}

bool metadataObjectMatches(const QJsonObject& actual, const QJsonObject& expected)
{
  for (auto it = expected.begin(); it != expected.end(); ++it)
  {
    if (!metadataValueMatches(actual.value(it.key()), it.value()))
    {
      return false;
    }
  }
  return true;
}

QJsonObject selectorFromParamsInternal(const QJsonObject& params)
{
  auto selector = params.value("selector").isObject()
                    ? params.value("selector").toObject()
                    : QJsonObject{};
  static const auto Keys = QStringList{
    "metadata", "moduleId",  "part",        "role",        "order",
    "routeId",  "temporary", "generatedBy", "operationId", "operationIds",
    "type",     "classname", "targetname",  "material",    "query",
    "min",      "max",       "boundsMode",  "limit",
  };
  for (const auto& key : Keys)
  {
    if (params.contains(key) && !selector.contains(key))
    {
      selector.insert(key, params.value(key));
    }
  }
  return selector;
}

QJsonObject selectorMetadata(const QJsonObject& selector)
{
  auto metadata = selector.value("metadata").isObject()
                    ? selector.value("metadata").toObject()
                    : QJsonObject{};
  static const auto MetadataKeys = QStringList{
    "moduleId", "part", "role", "order", "routeId", "temporary", "generatedBy"};
  for (const auto& key : MetadataKeys)
  {
    if (selector.contains(key) && !metadata.contains(key))
    {
      metadata.insert(key, selector.value(key));
    }
  }
  return metadata;
}

std::optional<QStringList> stringListFromValue(
  const QJsonValue& value, const QString& key, QString& error)
{
  auto result = QStringList{};
  if (value.isUndefined() || value.isNull())
  {
    return result;
  }
  if (value.isString())
  {
    const auto string = value.toString().trimmed();
    if (!string.isEmpty())
    {
      result.push_back(string);
    }
    return result;
  }
  if (!value.isArray())
  {
    error = QString{"%1 must be a string or array of strings"}.arg(key);
    return std::nullopt;
  }
  for (const auto& entry : value.toArray())
  {
    if (!entry.isString())
    {
      error = QString{"%1 must contain only strings"}.arg(key);
      return std::nullopt;
    }
    const auto string = entry.toString().trimmed();
    if (!string.isEmpty())
    {
      result.push_back(string);
    }
  }
  result.removeDuplicates();
  return result;
}

QJsonObject flatFilterFromSelector(const QJsonObject& selector)
{
  auto result = QJsonObject{};
  for (const auto& key : QStringList{
         "type",
         "classname",
         "targetname",
         "material",
         "query",
         "min",
         "max",
         "boundsMode",
         "limit"})
  {
    if (selector.contains(key))
    {
      result.insert(key, selector.value(key));
    }
  }
  return result;
}

const McpOperationRecord* findOperation(
  const std::vector<McpOperationRecord>& history, const QString& operationId)
{
  const auto it = std::ranges::find_if(
    history, [&](const auto& operation) { return operation.operationId == operationId; });
  return it == history.end() ? nullptr : &*it;
}

QStringList operationIdsFromSelector(const QJsonObject& selector, QString& error)
{
  auto result = QStringList{};
  if (auto one = selector.value("operationId").toString().trimmed(); !one.isEmpty())
  {
    result.push_back(one);
  }
  auto many = stringListFromValue(selector.value("operationIds"), "operationIds", error);
  if (!many)
  {
    return {};
  }
  result.append(*many);
  result.removeDuplicates();
  return result;
}

mdl::Node* resolveObjectId(
  mdl::Map& map,
  const QString& objectId,
  const McpObjectRegistry& objectRegistry,
  QJsonArray& warnings)
{
  const auto resolved = objectRegistry.resolveExternalId(map, objectId);
  if (!resolved.ok)
  {
    warnings.push_back(resolved.diagnostic);
    return nullptr;
  }
  const auto path = McpObjectRegistry::parseLegacyObjectId(resolved.legacyPathId);
  if (!path)
  {
    warnings.push_back(
      QString{"Invalid legacy object id: %1"}.arg(resolved.legacyPathId));
    return nullptr;
  }
  auto* node = map.worldNode().resolvePath(*path);
  if (node == nullptr)
  {
    warnings.push_back(QString{"Object id does not resolve: %1"}.arg(objectId));
  }
  return node;
}

QString externalObjectIdForNode(
  mdl::Map& map, const mdl::Node& node, const McpObjectRegistry& objectRegistry)
{
  return objectRegistry.externalIdForLegacy(map, mcpNodePathId(node, map.worldNode()));
}

bool nodeInVector(const std::vector<mdl::Node*>& nodes, const mdl::Node& node)
{
  return std::ranges::find(nodes, &node) != nodes.end();
}

std::vector<mdl::Node*> dedupeNodes(std::vector<mdl::Node*> nodes)
{
  std::ranges::sort(nodes);
  nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
  return nodes;
}

QString metadataStoreKey(const QString& documentFingerprint, const QString& objectId)
{
  return documentFingerprint.isEmpty()
           ? objectId
           : QString{"%1|%2"}.arg(documentFingerprint, objectId);
}

const McpBrushMetadataRecord* findMetadataRecord(
  const QString& objectId,
  const QString& documentFingerprint,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  const auto scopedIt =
    metadataStore.find(metadataStoreKey(documentFingerprint, objectId));
  if (scopedIt != metadataStore.end())
  {
    return &scopedIt->second;
  }

  const auto legacyIt = metadataStore.find(objectId);
  if (
    legacyIt == metadataStore.end()
    || (!legacyIt->second.documentFingerprint.isEmpty() && legacyIt->second.documentFingerprint != documentFingerprint))
  {
    return nullptr;
  }
  return &legacyIt->second;
}

QJsonObject metadataForObject(
  const QString& objectId,
  const QString& documentFingerprint,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  const auto* record = findMetadataRecord(objectId, documentFingerprint, metadataStore);
  if (record == nullptr || record->stale)
  {
    return {};
  }
  return record->metadata;
}

bool isModuleLevelMetadataKey(const QString& key)
{
  static const auto Keys = QStringList{
    "moduleId",
    "routeId",
    "temporary",
    "generatedBy",
    "intent",
    "name",
    "description",
  };
  return Keys.contains(key);
}

QJsonObject moduleLevelMetadata(const QJsonObject& metadata)
{
  auto result = QJsonObject{};
  for (auto it = metadata.begin(); it != metadata.end(); ++it)
  {
    if (isModuleLevelMetadataKey(it.key()))
    {
      result.insert(it.key(), it.value());
    }
  }
  return result;
}

QJsonObject metadataForNode(
  mdl::Map& map,
  const mdl::Node& node,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const McpObjectRegistry& objectRegistry)
{
  const auto documentFingerprint = documentFingerprintForMap(map);
  const auto stableId = externalObjectIdForNode(map, node, objectRegistry);
  if (const auto metadata =
        metadataForObject(stableId, documentFingerprint, metadataStore);
      !metadata.isEmpty())
  {
    return metadata;
  }
  return metadataForObject(
    mcpNodePathId(node, map.worldNode()), documentFingerprint, metadataStore);
}

bool recordMatchesDocument(
  const McpBrushMetadataRecord& record, const QString& documentFingerprint)
{
  return record.documentFingerprint.isEmpty()
         || record.documentFingerprint == documentFingerprint;
}

bool recordMatchesDocument(
  const McpModuleRecord& record, const QString& documentFingerprint)
{
  return record.documentFingerprint.isEmpty()
         || record.documentFingerprint == documentFingerprint;
}

const McpModuleRecord* findModuleRecord(
  const QString& moduleId,
  const QString& documentFingerprint,
  const std::map<QString, McpModuleRecord>& moduleStore)
{
  for (const auto& [key, module] : moduleStore)
  {
    Q_UNUSED(key);
    if (module.moduleId == moduleId && recordMatchesDocument(module, documentFingerprint))
    {
      return &module;
    }
  }
  return nullptr;
}

QString moduleStoreKey(const QString& documentFingerprint, const QString& moduleId)
{
  return documentFingerprint.isEmpty()
           ? moduleId
           : QString{"%1|%2"}.arg(documentFingerprint, moduleId);
}

bool metadataSelectorNeedsObjectFilter(
  const QJsonObject& selector, const QJsonObject& metadata, const bool seededByModuleId)
{
  if (metadata.isEmpty())
  {
    return false;
  }

  if (
    selector.value("metadata").isObject()
    && !selector.value("metadata").toObject().isEmpty())
  {
    return true;
  }

  if (!seededByModuleId)
  {
    return true;
  }

  for (auto it = metadata.begin(); it != metadata.end(); ++it)
  {
    if (it.key() != "moduleId")
    {
      return true;
    }
  }
  return false;
}

bool nodeMatchesMetadata(
  mdl::Map& map,
  const mdl::Node& node,
  const QJsonObject& metadata,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const McpObjectRegistry& objectRegistry)
{
  return metadataObjectMatches(
    metadataForNode(map, node, metadataStore, objectRegistry), metadata);
}

std::vector<mdl::Node*> resolveSelectorNodesInternal(
  mdl::Map& map,
  const QJsonObject& selector,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry,
  QJsonArray& warnings,
  QString& error,
  SelectorDiagnosticsInternal* diagnostics = nullptr)
{
  auto candidates = std::vector<mdl::Node*>{};
  auto seeded = false;
  auto seededByModuleId = false;
  const auto documentFingerprint = documentFingerprintForMap(map);
  auto staleExcluded = 0;

  const auto moduleId = selector.value("moduleId").toString().trimmed();
  if (!moduleId.isEmpty())
  {
    if (const auto* module = findModuleRecord(moduleId, documentFingerprint, moduleStore))
    {
      for (const auto& objectId : module->objectIds)
      {
        if (auto* node = resolveObjectId(map, objectId, objectRegistry, warnings))
        {
          candidates.push_back(node);
        }
        else
        {
          ++staleExcluded;
        }
      }
      seeded = true;
      seededByModuleId = true;
    }
  }

  for (const auto& operationId : operationIdsFromSelector(selector, error))
  {
    if (!error.isEmpty())
    {
      return {};
    }
    const auto* operation = findOperation(history, operationId);
    if (operation == nullptr)
    {
      warnings.push_back(QString{"Unknown MCP operation id: %1"}.arg(operationId));
      continue;
    }
    if (operation->undone)
    {
      warnings.push_back(QString{"MCP operation is undone: %1"}.arg(operationId));
    }
    for (const auto& objectId : operation->changedObjectIds)
    {
      if (auto* node = resolveObjectId(map, objectId, objectRegistry, warnings))
      {
        candidates.push_back(node);
      }
      else
      {
        ++staleExcluded;
      }
    }
    seeded = true;
  }

  const auto metadata = selectorMetadata(selector);
  if (!metadata.isEmpty())
  {
    if (seeded && metadataSelectorNeedsObjectFilter(selector, metadata, seededByModuleId))
    {
      candidates.erase(
        std::remove_if(
          candidates.begin(),
          candidates.end(),
          [&](const auto* node) {
            return node == nullptr
                   || !nodeMatchesMetadata(
                     map, *node, metadata, metadataStore, objectRegistry);
          }),
        candidates.end());
    }
    else if (!seeded)
    {
      for (const auto& [storedObjectId, record] : metadataStore)
      {
        Q_UNUSED(storedObjectId);
        if (
          record.stale || !recordMatchesDocument(record, documentFingerprint)
          || !metadataObjectMatches(record.metadata, metadata))
        {
          continue;
        }
        if (auto* node = resolveObjectId(map, record.objectId, objectRegistry, warnings))
        {
          candidates.push_back(node);
        }
        else
        {
          ++staleExcluded;
        }
      }
      for (const auto& [storedModuleId, module] : moduleStore)
      {
        Q_UNUSED(storedModuleId);
        if (
          !recordMatchesDocument(module, documentFingerprint)
          || !metadataObjectMatches(module.metadata, metadata))
        {
          continue;
        }
        for (const auto& objectId : module.objectIds)
        {
          if (auto* node = resolveObjectId(map, objectId, objectRegistry, warnings))
          {
            candidates.push_back(node);
          }
          else
          {
            ++staleExcluded;
          }
        }
      }
      seeded = true;
    }
  }

  auto filter = flatFilterFromSelector(selector);
  if (!filter.isEmpty() || !seeded)
  {
    if (!filter.contains("limit"))
    {
      filter.insert("limit", selector.value("limit").toInt(1000));
    }
    auto options = McpSelectionQueryOptions{};
    options.excludeWorld = true;
    options.selectableOnly = false;
    options.leafOnly = selector.value("leafOnly").toBool(false);
    options.exactTypeOnly = true;
    options.removeDescendantMatches = false;
    auto filtered = mcpFilteredNodes(map, filter, options, error);
    if (!error.isEmpty())
    {
      return {};
    }
    if (seeded)
    {
      auto intersection = std::vector<mdl::Node*>{};
      for (auto* node : candidates)
      {
        if (node != nullptr && nodeInVector(filtered, *node))
        {
          intersection.push_back(node);
        }
      }
      candidates = std::move(intersection);
    }
    else
    {
      candidates = std::move(filtered);
    }
  }

  candidates = dedupeNodes(std::move(candidates));
  const auto matchedBeforeLimit = static_cast<int>(candidates.size());
  const auto limit = std::clamp(selector.value("limit").toInt(100), 1, 10000);
  auto limitApplied = false;
  if (static_cast<int>(candidates.size()) > limit)
  {
    candidates.resize(static_cast<size_t>(limit));
    limitApplied = true;
    warnings.push_back(QString{"selectorResultTruncated: limit=%1"}.arg(limit));
  }
  if (diagnostics != nullptr)
  {
    diagnostics->matchedBeforeLimit = matchedBeforeLimit;
    diagnostics->limitApplied = limitApplied;
    diagnostics->staleExcluded = staleExcluded;
  }
  return candidates;
}

QJsonObject nodeSummary(
  mdl::Map& map,
  const mdl::Node& node,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const McpObjectRegistry& objectRegistry)
{
  const auto objectId = externalObjectIdForNode(map, node, objectRegistry);
  auto result = QJsonObject{
    {"objectId", objectId},
    {"type", mcpNodeTypeName(node)},
    {"name", QString::fromStdString(node.name())},
    {"bounds", boundsToJson(node.logicalBounds())},
    {"metadata", metadataForNode(map, node, metadataStore, objectRegistry)},
  };
  return result;
}

QJsonObject compactTargetResult(
  mdl::Map& map,
  const std::vector<mdl::Node*>& nodes,
  const QJsonObject& selector,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const McpObjectRegistry& objectRegistry,
  const QJsonArray& warnings,
  const QString& idsMode)
{
  auto objectIds = QStringList{};
  auto samples = QJsonArray{};
  const auto sampleLimit =
    std::clamp(selector.value("sampleLimit").toInt(DefaultSampleLimit), 0, 100);
  for (auto i = 0; i < static_cast<int>(nodes.size()); ++i)
  {
    const auto* node = nodes[static_cast<size_t>(i)];
    if (node == nullptr)
    {
      continue;
    }
    objectIds.push_back(externalObjectIdForNode(map, *node, objectRegistry));
    if (i < sampleLimit)
    {
      samples.push_back(nodeSummary(map, *node, metadataStore, objectRegistry));
    }
  }

  auto result = QJsonObject{
    {"selector", selector},
    {"matchedCount", static_cast<int>(nodes.size())},
    {"sampleCount", samples.size()},
    {"sample", samples},
    {"warnings", warnings},
  };
  if (!nodes.empty())
  {
    result.insert("bounds", boundsToJson(boundsForNodes(nodes)));
  }

  const auto mode = idsMode.trimmed().toLower();
  if (mode == "full")
  {
    result.insert("objectIds", stringsToJson(objectIds));
  }
  else if (mode == "sample")
  {
    auto sampleIds = QStringList{};
    for (auto i = 0; i < std::min(sampleLimit, static_cast<int>(objectIds.size())); ++i)
    {
      sampleIds.push_back(objectIds[i]);
    }
    result.insert("objectIdSample", stringsToJson(sampleIds));
    result.insert("objectIdCount", objectIds.size());
  }
  else if (mode == "count" || mode.isEmpty())
  {
    result.insert("objectIdCount", objectIds.size());
  }
  else if (mode != "none")
  {
    result.insert("objectIdCount", objectIds.size());
    auto updatedWarnings = result.value("warnings").toArray();
    updatedWarnings.push_back(
      QString{"unknownIdsMode: %1; returned count only"}.arg(idsMode));
    result.insert("warnings", updatedWarnings);
  }
  return result;
}

QJsonArray nodeIdsJson(
  mdl::Map& map,
  const std::vector<mdl::Node*>& nodes,
  const McpObjectRegistry& objectRegistry)
{
  auto result = QJsonArray{};
  for (const auto* node : nodes)
  {
    if (node != nullptr)
    {
      result.push_back(externalObjectIdForNode(map, *node, objectRegistry));
    }
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
  nodes = dedupeNodes(std::move(nodes));
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
    removedIds.push_back(mcpNodePathId(*node, map.worldNode()));
  }

  const auto ok = executeTransaction(map, transactionName, [&]() {
    mdl::deselectNodes(map, nodes);
    return map.executeAndStore(mdl::AddRemoveNodesCommand::remove(nodesByParent));
  });

  return ok ? std::optional{removedIds} : std::nullopt;
}

QString makeOperationId(int& nextOperationIndex)
{
  return QString{"mcp-op-%1"}.arg(nextOperationIndex++);
}

void recordDeleteOperation(
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const QString& toolName,
  const QString& transactionName,
  const QJsonArray& deletedObjectIds,
  QJsonObject& result)
{
  auto operation = McpOperationRecord{};
  operation.operationId = makeOperationId(nextOperationIndex);
  operation.toolName = toolName;
  operation.transactionName = transactionName;
  operation.operationKind = "delete";
  operation.setChangedObjectIds(QJsonArray{});
  operation.setDeletedObjectIds(deletedObjectIds);

  result = QJsonObject{
    {"operationId", operation.operationId},
    {"transactionName", operation.transactionName},
    {"operationKind", operation.operationKind},
    {"changedObjectCount", 0},
    {"changedObjectIds", QJsonArray{}},
    {"deletedObjectIds", deletedObjectIds},
    {"deletedObjectCount", deletedObjectIds.size()},
    {"resourceUri", QString{"tbmcp://operation/%1"}.arg(operation.operationId)},
  };
  operation.setSummary(result);
  history.push_back(std::move(operation));
}

void markDeletedMetadata(
  const QJsonArray& deletedObjectIds,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore)
{
  auto deleted = std::set<QString>{};
  for (const auto& value : deletedObjectIds)
  {
    if (value.isString())
    {
      deleted.insert(value.toString());
    }
  }
  for (auto& [objectId, record] : metadataStore)
  {
    if (deleted.contains(objectId) || deleted.contains(record.objectId))
    {
      record.stale = true;
    }
  }
  for (auto& [moduleId, module] : moduleStore)
  {
    module.objectIds.erase(
      std::remove_if(
        module.objectIds.begin(),
        module.objectIds.end(),
        [&](const auto& objectId) { return deleted.contains(objectId); }),
      module.objectIds.end());
  }
}

QJsonObject moduleMetadataFromObjects(
  const QString& moduleId,
  const QString& documentFingerprint,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  auto result = QJsonObject{};
  for (const auto& [objectId, record] : metadataStore)
  {
    Q_UNUSED(objectId);
    if (
      !record.stale && recordMatchesDocument(record, documentFingerprint)
      && record.metadata.value("moduleId").toString() == moduleId)
    {
      const auto moduleMetadata = moduleLevelMetadata(record.metadata);
      for (auto it = moduleMetadata.begin(); it != moduleMetadata.end(); ++it)
      {
        if (!result.contains(it.key()))
        {
          result.insert(it.key(), it.value());
        }
      }
    }
  }
  return result;
}

QJsonArray modulePartSummary(
  const QString& moduleId,
  const QString& documentFingerprint,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const bool staleOnly = false)
{
  auto counts = std::map<QString, int>{};
  for (const auto& [objectId, record] : metadataStore)
  {
    Q_UNUSED(objectId);
    if (
      record.stale != staleOnly || !recordMatchesDocument(record, documentFingerprint)
      || record.metadata.value("moduleId").toString() != moduleId)
    {
      continue;
    }
    const auto part = record.metadata.value("part").toString().trimmed();
    if (!part.isEmpty())
    {
      ++counts[part];
    }
  }

  auto result = QJsonArray{};
  for (const auto& [part, count] : counts)
  {
    result.push_back(QJsonObject{{"part", part}, {"count", count}});
  }
  return result;
}

QStringList moduleObjectIdsFromMetadata(
  const QString& moduleId,
  const QString& documentFingerprint,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  auto result = QStringList{};
  for (const auto& [objectId, record] : metadataStore)
  {
    if (
      !record.stale && recordMatchesDocument(record, documentFingerprint)
      && record.metadata.value("moduleId").toString() == moduleId)
    {
      result.push_back(record.objectId);
    }
  }
  result.removeDuplicates();
  return result;
}

McpModuleRecord mergedModuleRecord(
  const QString& moduleId,
  const QString& documentFingerprint,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore)
{
  auto result = McpModuleRecord{};
  result.moduleId = moduleId;
  result.documentFingerprint = documentFingerprint;
  result.metadata =
    moduleMetadataFromObjects(moduleId, documentFingerprint, metadataStore);
  if (const auto* module = findModuleRecord(moduleId, documentFingerprint, moduleStore))
  {
    result = *module;
    result.moduleId = moduleId;
    result.metadata = moduleLevelMetadata(result.metadata);
    if (result.metadata.isEmpty())
    {
      result.metadata =
        moduleMetadataFromObjects(moduleId, documentFingerprint, metadataStore);
    }
  }
  result.objectIds.append(
    moduleObjectIdsFromMetadata(moduleId, documentFingerprint, metadataStore));
  result.objectIds.removeDuplicates();
  return result;
}

QJsonObject moduleSummary(
  mdl::Map& map,
  const McpModuleRecord& module,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const McpObjectRegistry& objectRegistry)
{
  auto liveCount = 0;
  auto staleCount = 0;
  auto warnings = QJsonArray{};
  auto nodes = std::vector<mdl::Node*>{};
  for (const auto& objectId : module.objectIds)
  {
    if (auto* node = resolveObjectId(map, objectId, objectRegistry, warnings))
    {
      ++liveCount;
      nodes.push_back(node);
    }
    else
    {
      ++staleCount;
    }
  }
  return QJsonObject{
    {"moduleId", module.moduleId},
    {"objectCount", module.objectIds.size()},
    {"liveObjectCount", liveCount},
    {"staleObjectCount", staleCount},
    {"operationIds", stringsToJson(module.operationIds)},
    {"operationCount", module.operationIds.size()},
    {"metadata", module.metadata},
    {"parts",
     modulePartSummary(module.moduleId, module.documentFingerprint, metadataStore)},
    {"liveParts",
     modulePartSummary(module.moduleId, module.documentFingerprint, metadataStore)},
    {"staleParts",
     modulePartSummary(module.moduleId, module.documentFingerprint, metadataStore, true)},
    {"bounds", nodes.empty() ? QJsonObject{} : boundsToJson(boundsForNodes(nodes))},
  };
}

bool metadataMarksContinuityTarget(const QJsonObject& metadata)
{
  const auto role = metadata.value("role").toString().trimmed().toLower();
  if (role == "walkable" || role == "playable" || role == "route")
  {
    return true;
  }
  const auto part = metadata.value("part").toString().trimmed().toLower();
  static const auto Parts = QStringList{
    "road",
    "floor",
    "surface",
    "ribbon",
    "path",
    "platform",
    "bhop-platform",
    "bhop_platform",
    "slide-ramp",
    "slide_ramp",
    "ramp",
    "steps",
    "stair",
    "stairs",
  };
  return Parts.contains(part);
}

std::vector<mdl::Node*> defaultContinuityNodes(
  mdl::Map& map,
  const std::vector<mdl::Node*>& nodes,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const McpObjectRegistry& objectRegistry)
{
  auto result = std::vector<mdl::Node*>{};
  for (auto* node : nodes)
  {
    if (
      node != nullptr
      && metadataMarksContinuityTarget(
        metadataForNode(map, *node, metadataStore, objectRegistry)))
    {
      result.push_back(node);
    }
  }
  return result;
}

QStringList allModuleIds(
  const QString& documentFingerprint,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore)
{
  auto ids = QStringList{};
  for (const auto& [storedModuleId, module] : moduleStore)
  {
    Q_UNUSED(storedModuleId);
    const auto moduleId = module.moduleId.trimmed();
    if (!moduleId.isEmpty())
    {
      if (recordMatchesDocument(module, documentFingerprint))
      {
        ids.push_back(moduleId);
      }
    }
  }
  for (const auto& [objectId, record] : metadataStore)
  {
    Q_UNUSED(objectId);
    if (!recordMatchesDocument(record, documentFingerprint))
    {
      continue;
    }
    const auto moduleId = record.metadata.value("moduleId").toString().trimmed();
    if (!moduleId.isEmpty())
    {
      ids.push_back(moduleId);
    }
  }
  ids.removeDuplicates();
  ids.sort(Qt::CaseInsensitive);
  return ids;
}

bool validateIrShape(const QJsonObject& ir, QString& error)
{
  const auto operationsValue = ir.value("operations");
  const auto entitiesValue = ir.value("entities");
  const auto hasOperations = !operationsValue.isUndefined() && !operationsValue.isNull();
  const auto hasEntities = !entitiesValue.isUndefined() && !entitiesValue.isNull();
  if (!hasOperations && !hasEntities)
  {
    error = "IR requires at least one operations or entities array";
    return false;
  }
  if (hasOperations && !operationsValue.isArray())
  {
    error = "IR operations must be an array";
    return false;
  }
  if (hasEntities && !entitiesValue.isArray())
  {
    error = "IR entities must be an array";
    return false;
  }
  if (hasOperations)
  {
    const auto operations = operationsValue.toArray();
    for (auto i = 0; i < operations.size(); ++i)
    {
      if (!operations[i].isObject())
      {
        error = QString{"IR operations[%1] must be an object"}.arg(i);
        return false;
      }
      const auto type = operations[i].toObject().value("type").toString().trimmed();
      if (type.isEmpty())
      {
        error = QString{"IR operations[%1] requires type"}.arg(i);
        return false;
      }
    }
  }
  if (hasEntities)
  {
    const auto entities = entitiesValue.toArray();
    for (auto i = 0; i < entities.size(); ++i)
    {
      if (!entities[i].isObject())
      {
        error = QString{"IR entities[%1] must be an object"}.arg(i);
        return false;
      }
      const auto classname =
        entities[i].toObject().value("classname").toString().trimmed();
      if (classname.isEmpty())
      {
        error = QString{"IR entities[%1] requires classname"}.arg(i);
        return false;
      }
    }
  }
  if (operationsValue.toArray().isEmpty() && entitiesValue.toArray().isEmpty())
  {
    error = "IR operations/entities must not both be empty";
    return false;
  }
  return true;
}

std::optional<QJsonObject> irFromParams(const QJsonObject& params, QString& error)
{
  const auto irValue = params.value("ir");
  if (irValue.isObject())
  {
    auto ir = irValue.toObject();
    if (!validateIrShape(ir, error))
    {
      return std::nullopt;
    }
    return ir;
  }
  if (!irValue.isUndefined() && !irValue.isNull())
  {
    error = "IR field must be an object";
    return std::nullopt;
  }
  if (params.contains("operations") && !params.value("operations").isArray())
  {
    error = "IR operations must be an array";
    return std::nullopt;
  }
  if (params.contains("entities") && !params.value("entities").isArray())
  {
    error = "IR entities must be an array";
    return std::nullopt;
  }
  if (params.value("operations").isArray() || params.value("entities").isArray())
  {
    auto ir = QJsonObject{};
    if (params.value("operations").isArray())
    {
      ir.insert("operations", params.value("operations"));
    }
    if (params.value("entities").isArray())
    {
      ir.insert("entities", params.value("entities"));
    }
    for (const auto& key : {"name", "moduleId", "defaultMetadata", "material", "grid"})
    {
      if (params.contains(key))
      {
        ir.insert(key, params.value(key));
      }
    }
    if (!validateIrShape(ir, error))
    {
      return std::nullopt;
    }
    return ir;
  }
  error = "IR requires ir object or operations/entities arrays";
  return std::nullopt;
}

std::optional<QJsonObject> irFromFileParams(const QJsonObject& params, QString& error)
{
  const auto path = params.value("path").toString().trimmed();
  if (path.isEmpty())
  {
    error = "file-based IR requires path";
    return std::nullopt;
  }
  const auto info = QFileInfo{path};
  if (!info.isAbsolute())
  {
    error = "file-based IR path must be absolute";
    return std::nullopt;
  }
  if (!info.isFile() || !info.isReadable())
  {
    error = QString{"IR file is not readable: %1"}.arg(path);
    return std::nullopt;
  }
  if (info.size() > 10 * 1024 * 1024)
  {
    error = "IR file is too large; maximum size is 10 MiB";
    return std::nullopt;
  }

  auto file = QFile{path};
  if (!file.open(QIODevice::ReadOnly))
  {
    error = QString{"Could not open IR file: %1"}.arg(path);
    return std::nullopt;
  }
  auto parseError = QJsonParseError{};
  const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
  {
    error =
      QString{"IR file must contain a JSON object: %1"}.arg(parseError.errorString());
    return std::nullopt;
  }
  auto ir = document.object();
  if (!validateIrShape(ir, error))
  {
    return std::nullopt;
  }
  return ir;
}

QJsonObject mergeObjects(QJsonObject base, const QJsonObject& overlay)
{
  for (auto it = overlay.begin(); it != overlay.end(); ++it)
  {
    base.insert(it.key(), it.value());
  }
  return base;
}

int repeatGridCountEstimate(const QJsonValue& counts)
{
  if (counts.isDouble())
  {
    return std::max(1, counts.toInt(1));
  }
  if (!counts.isArray())
  {
    return 1;
  }
  auto count = 1;
  for (const auto& item : counts.toArray())
  {
    count *= std::max(1, item.toInt(1));
  }
  return count;
}

QStringList projectedOperationParts(const QJsonObject& operation)
{
  auto parts = QStringList{};
  const auto append = [&](const QString& part) {
    if (!part.isEmpty())
    {
      parts.push_back(part);
    }
  };

  append(operation.value("metadata").toObject().value("part").toString());
  append(operation.value("part").toString());
  if (operation.value("parts").isArray())
  {
    for (const auto& part : operation.value("parts").toArray())
    {
      append(part.toString());
    }
  }

  const auto type = operation.value("type").toString();
  if (type == "repeat_translate" || type == "repeat_grid")
  {
    const auto child = operation.value("operation").toObject();
    const auto childParts = projectedOperationParts(child);
    const auto count = type == "repeat_translate"
                         ? std::max(1, operation.value("count").toInt(1))
                         : repeatGridCountEstimate(operation.value("counts"));
    for (auto i = 0; i < count; ++i)
    {
      parts.append(childParts);
    }
  }
  else if (type == "arc_ramp" || type == "helical_ramp")
  {
    const auto count = std::max(1, operation.value("segments").toInt(12));
    const auto part =
      operation.value("metadata").toObject().value("part").toString("ramp");
    for (auto i = 0; i < count; ++i)
    {
      append(part);
    }
  }
  return parts;
}

QJsonObject irPreviewJson(const QJsonObject& ir)
{
  const auto operations = ir.value("operations").toArray();
  const auto entities = ir.value("entities").toArray();
  auto parts = QJsonArray{};
  auto warnings = QJsonArray{};
  auto estimatedBrushCount = 0;
  for (const auto& value : operations)
  {
    if (!value.isObject())
    {
      warnings.push_back("operations contains non-object entry");
      continue;
    }
    const auto operation = value.toObject();
    const auto type = operation.value("type").toString();
    for (const auto& part : projectedOperationParts(operation))
    {
      parts.push_back(part);
    }
    if (type == "curved_corridor")
    {
      const auto segments = std::max(1, operation.value("segments").toInt(12));
      estimatedBrushCount += segments * 4;
      const auto caps = operation.value("caps").toString("none").toLower();
      if (caps == "start" || caps == "end")
      {
        ++estimatedBrushCount;
      }
      else if (caps == "both")
      {
        estimatedBrushCount += 2;
      }
    }
    else if (type == "room" || type == "corridor" || type == "sky_shell")
    {
      estimatedBrushCount += 6;
    }
    else if (type == "doorway")
    {
      estimatedBrushCount += 4;
    }
    else if (type == "stairs")
    {
      estimatedBrushCount += std::max(1, operation.value("steps").toInt(8));
    }
    else if (type == "path_ribbon")
    {
      const auto points = operation.value("points3d").isArray()
                            ? operation.value("points3d").toArray()
                            : operation.value("points2d").toArray();
      estimatedBrushCount += std::max(0, static_cast<int>(points.size()) - 1);
    }
    else if (type == "arc_ramp" || type == "helical_ramp")
    {
      estimatedBrushCount += std::max(1, operation.value("segments").toInt(12));
    }
    else if (type == "repeat_translate")
    {
      estimatedBrushCount += std::max(1, operation.value("count").toInt(1));
    }
    else if (type == "repeat_grid")
    {
      estimatedBrushCount += repeatGridCountEstimate(operation.value("counts"));
    }
    else
    {
      ++estimatedBrushCount;
    }
  }

  return QJsonObject{
    {"valid", warnings.isEmpty()},
    {"operationCount", operations.size()},
    {"entityCount", entities.size()},
    {"estimatedBrushCount", estimatedBrushCount},
    {"estimatedObjectCount", estimatedBrushCount + entities.size()},
    {"parts", parts},
    {"moduleId", ir.value("moduleId").toString()},
    {"warnings", warnings},
  };
}

QJsonObject irPreviewJsonForMap(mdl::Map& map, const QJsonObject& ir)
{
  auto preview = irPreviewJson(ir);
  if (const auto operations = ir.value("operations"); operations.isArray())
  {
    const auto defaultMetadata = ir.value("defaultMetadata").isObject()
                                   ? ir.value("defaultMetadata").toObject()
                                   : QJsonObject{};
    const auto moduleId =
      ir.value("moduleId").toString(defaultMetadata.value("moduleId").toString());
    auto mergedDefaultMetadata = defaultMetadata;
    if (!moduleId.isEmpty() && !mergedDefaultMetadata.contains("moduleId"))
    {
      mergedDefaultMetadata.insert("moduleId", moduleId);
    }

    auto batchParams = QJsonObject{
      {"operations", operations.toArray()},
      {"detail", "summary"},
    };
    if (ir.contains("grid"))
    {
      batchParams.insert("grid", ir.value("grid"));
    }
    if (ir.contains("material"))
    {
      batchParams.insert("material", ir.value("material"));
    }
    if (!mergedDefaultMetadata.isEmpty())
    {
      batchParams.insert("defaultMetadata", mergedDefaultMetadata);
    }

    const auto blockoutPreview = blockoutCompilePreviewForMapResult(map, batchParams);
    if (blockoutPreview.ok)
    {
      preview.insert("blockoutPreview", blockoutPreview.result);
      preview.insert(
        "valid",
        preview.value("valid").toBool()
          && blockoutPreview.result.value("valid").toBool());
      preview.insert(
        "estimatedBrushCount",
        blockoutPreview.result.value("estimatedBrushCount")
          .toInt(preview.value("estimatedBrushCount").toInt()));
      if (blockoutPreview.result.contains("bounds"))
      {
        preview.insert("bounds", blockoutPreview.result.value("bounds"));
      }
      auto warnings = preview.value("warnings").toArray();
      for (const auto& error : blockoutPreview.result.value("errors").toArray())
      {
        warnings.push_back(error);
      }
      preview.insert("warnings", warnings);
    }
    else
    {
      preview.insert("valid", false);
      auto warnings = preview.value("warnings").toArray();
      warnings.push_back(blockoutPreview.error.message);
      preview.insert("warnings", warnings);
    }
  }
  preview.insert(
    "estimatedObjectCount",
    preview.value("estimatedBrushCount").toInt() + preview.value("entityCount").toInt());
  return preview;
}

void mergeModuleFromOperationResult(
  const QJsonObject& result,
  const QString& documentFingerprint,
  const QJsonObject& defaultMetadata,
  std::map<QString, McpModuleRecord>& moduleStore)
{
  const auto moduleId = defaultMetadata.value("moduleId").toString().trimmed();
  if (moduleId.isEmpty())
  {
    return;
  }
  auto& module = moduleStore[moduleStoreKey(documentFingerprint, moduleId)];
  module.moduleId = moduleId;
  module.documentFingerprint = documentFingerprint;
  module.metadata = mergeObjects(module.metadata, moduleLevelMetadata(defaultMetadata));
  const auto operationId = result.value("operationId").toString().trimmed();
  if (!operationId.isEmpty())
  {
    module.operationIds.push_back(operationId);
    module.operationIds.removeDuplicates();
  }
  for (const auto& value : result.value("changedObjectIds").toArray())
  {
    if (value.isString())
    {
      module.objectIds.push_back(value.toString());
    }
  }
  module.objectIds.removeDuplicates();
}

} // namespace

QJsonObject selectorFromParams(const QJsonObject& params)
{
  return selectorFromParamsInternal(params);
}

std::vector<mdl::Node*> resolveSelectorNodes(
  mdl::Map& map,
  const QJsonObject& selector,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry,
  QJsonArray& warnings,
  QString& error,
  McpSelectorDiagnostics* diagnostics)
{
  auto internalDiagnostics = SelectorDiagnosticsInternal{};
  auto nodes = resolveSelectorNodesInternal(
    map,
    selector,
    history,
    metadataStore,
    moduleStore,
    objectRegistry,
    warnings,
    error,
    diagnostics != nullptr ? &internalDiagnostics : nullptr);
  if (diagnostics != nullptr)
  {
    diagnostics->matchedBeforeLimit = internalDiagnostics.matchedBeforeLimit;
    diagnostics->limitApplied = internalDiagnostics.limitApplied;
    diagnostics->staleExcluded = internalDiagnostics.staleExcluded;
  }
  return nodes;
}

McpBridgeToolResult selectorPreviewResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }

  return selectorPreviewForMapResult(
    mapWindow->document().map(),
    params,
    history,
    metadataStore,
    moduleStore,
    objectRegistry);
}

McpBridgeToolResult selectorPreviewForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto warnings = QJsonArray{};
  auto error = QString{};
  const auto selector = selectorFromParams(params);
  auto diagnostics = McpSelectorDiagnostics{};
  const auto nodes = resolveSelectorNodes(
    map,
    selector,
    history,
    metadataStore,
    moduleStore,
    objectRegistry,
    warnings,
    error,
    &diagnostics);
  if (!error.isEmpty())
  {
    return invalidParamsFailure(error);
  }

  auto result = compactTargetResult(
    map,
    nodes,
    selector,
    metadataStore,
    objectRegistry,
    warnings,
    params.value("idsMode").toString("sample"));
  result.insert("tool", "selector_preview");
  result.insert("matchedBeforeLimit", diagnostics.matchedBeforeLimit);
  result.insert("limitApplied", diagnostics.limitApplied);
  result.insert("staleExcluded", diagnostics.staleExcluded);
  return McpBridgeToolResult::success(result);
}

McpBridgeToolResult objectsSelectBySelectorResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }

  auto& map = mapWindow->document().map();
  auto warnings = QJsonArray{};
  auto error = QString{};
  const auto selector = selectorFromParams(params);
  auto nodes = resolveSelectorNodes(
    map, selector, history, metadataStore, moduleStore, objectRegistry, warnings, error);
  if (!error.isEmpty())
  {
    return invalidParamsFailure(error);
  }
  nodes.erase(
    std::remove_if(
      nodes.begin(),
      nodes.end(),
      [&](const auto* node) {
        return node == nullptr || !map.editorContext().selectable(*node);
      }),
    nodes.end());

  mdl::deselectAll(map);
  if (!nodes.empty())
  {
    mdl::selectNodes(map, nodes);
  }

  auto result = compactTargetResult(
    map,
    nodes,
    selector,
    metadataStore,
    objectRegistry,
    warnings,
    params.value("idsMode").toString("sample"));
  result.insert("tool", "objects_select_by_selector");
  result.insert("selectedCount", static_cast<int>(nodes.size()));
  return McpBridgeToolResult::success(result);
}

McpBridgeToolResult objectsDeleteBySelectorResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }

  auto& map = mapWindow->document().map();
  auto warnings = QJsonArray{};
  auto error = QString{};
  const auto selector = selectorFromParams(params);
  auto nodes = resolveSelectorNodes(
    map, selector, history, metadataStore, moduleStore, objectRegistry, warnings, error);
  if (!error.isEmpty())
  {
    return invalidParamsFailure(error);
  }
  nodes.erase(
    std::remove_if(
      nodes.begin(),
      nodes.end(),
      [&](const auto* node) { return node == nullptr || node == &map.worldNode(); }),
    nodes.end());
  if (nodes.empty())
  {
    return invalidParamsFailure(
      "objects_delete_by_selector matched no deletable objects");
  }

  const auto transactionName =
    params.value("transactionName").toString("MCP: Delete objects by selector");
  const auto deletedIds = nodeIdsJson(map, nodes, objectRegistry);
  const auto changedObjectIds = removeNodesWithTransaction(
    map,
    transactionName.isEmpty() ? QString{"MCP: Delete objects by selector"}
                              : transactionName,
    nodes);
  if (!changedObjectIds)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden, "Matched selector objects cannot be deleted");
  }

  markDeletedMetadata(deletedIds, metadataStore, moduleStore);
  auto result = QJsonObject{};
  recordDeleteOperation(
    history,
    nextOperationIndex,
    toolName,
    transactionName.isEmpty() ? QString{"MCP: Delete objects by selector"}
                              : transactionName,
    deletedIds,
    result);
  result.insert("matchedCount", deletedIds.size());
  result.insert("selector", selector);
  result.insert("warnings", warnings);
  return McpBridgeToolResult::success(result);
}

McpBridgeToolResult renderReviewSelectorResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }

  auto& map = mapWindow->document().map();
  auto warnings = QJsonArray{};
  auto error = QString{};
  const auto selector = selectorFromParams(params);
  const auto nodes = resolveSelectorNodes(
    map, selector, history, metadataStore, moduleStore, objectRegistry, warnings, error);
  if (!error.isEmpty())
  {
    return invalidParamsFailure(error);
  }
  auto objectIds = nodeIdsJson(map, nodes, objectRegistry);
  auto reviewParams = params;
  reviewParams.insert("objectIds", objectIds);
  reviewParams.insert("detail", params.value("detail").toString("summary"));
  auto result =
    renderReviewTargetsForMapResult(map, reviewParams, history, &objectRegistry);
  if (result.ok)
  {
    result.result.insert("selector", selector);
    result.result.insert("selectorMatchedCount", objectIds.size());
    auto resultWarnings = result.result.value("warnings").toArray();
    for (const auto& warning : warnings)
    {
      resultWarnings.push_back(warning);
    }
    result.result.insert("warnings", resultWarnings);
  }
  return result;
}

McpBridgeToolResult moduleListResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }
  return moduleListForMapResult(
    mapWindow->document().map(), params, metadataStore, moduleStore, objectRegistry);
}

McpBridgeToolResult moduleListForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  const auto documentFingerprint = documentFingerprintForMap(map);
  const auto limit = std::clamp(params.value("limit").toInt(100), 1, 1000);
  const auto includeStale = params.value("includeStale").toBool(false);
  const auto includeEmpty = params.value("includeEmpty").toBool(includeStale);
  auto modules = QJsonArray{};
  auto ids = allModuleIds(documentFingerprint, metadataStore, moduleStore);
  auto liveModuleCount = 0;
  auto staleModuleCount = 0;
  auto emptyModuleCount = 0;
  auto filteredStaleModuleCount = 0;
  auto filteredEmptyModuleCount = 0;
  for (const auto& id : ids)
  {
    const auto module =
      mergedModuleRecord(id, documentFingerprint, metadataStore, moduleStore);
    const auto summary = moduleSummary(map, module, metadataStore, objectRegistry);
    const auto objectCount = summary.value("objectCount").toInt();
    const auto liveObjectCount = summary.value("liveObjectCount").toInt();
    if (objectCount == 0)
    {
      ++emptyModuleCount;
      if (!includeEmpty)
      {
        ++filteredEmptyModuleCount;
        continue;
      }
    }
    else if (liveObjectCount == 0)
    {
      ++staleModuleCount;
      if (!includeStale)
      {
        ++filteredStaleModuleCount;
        continue;
      }
    }
    else
    {
      ++liveModuleCount;
    }
    if (modules.size() < limit)
    {
      modules.push_back(summary);
    }
  }
  return McpBridgeToolResult::success(QJsonObject{
    {"tool", "module_list"},
    {"moduleCount", ids.size()},
    {"liveModuleCount", liveModuleCount},
    {"staleModuleCount", staleModuleCount},
    {"emptyModuleCount", emptyModuleCount},
    {"filteredStaleModuleCount", filteredStaleModuleCount},
    {"filteredEmptyModuleCount", filteredEmptyModuleCount},
    {"includeStale", includeStale},
    {"includeEmpty", includeEmpty},
    {"returnedCount", modules.size()},
    {"modules", modules},
  });
}

McpBridgeToolResult moduleInspectResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }
  const auto moduleId = params.value("moduleId").toString().trimmed();
  if (moduleId.isEmpty())
  {
    return invalidParamsFailure("module_inspect requires moduleId");
  }
  auto& map = mapWindow->document().map();
  const auto documentFingerprint = documentFingerprintForMap(map);
  auto module =
    mergedModuleRecord(moduleId, documentFingerprint, metadataStore, moduleStore);
  auto summary = moduleSummary(map, module, metadataStore, objectRegistry);
  summary.insert("tool", "module_inspect");
  if (params.value("idsMode").toString("sample") == "full")
  {
    summary.insert("objectIds", stringsToJson(module.objectIds));
  }
  else
  {
    summary.insert("objectIdCount", module.objectIds.size());
    auto sample = QStringList{};
    for (auto i = 0;
         i < std::min(DefaultSampleLimit, static_cast<int>(module.objectIds.size()));
         ++i)
    {
      sample.push_back(module.objectIds[i]);
    }
    summary.insert("objectIdSample", stringsToJson(sample));
  }
  return McpBridgeToolResult::success(summary);
}

McpBridgeToolResult moduleSelectResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto selectorParams = params;
  auto selector = selectorFromParams(params);
  selector.insert("moduleId", params.value("moduleId"));
  selectorParams.insert("selector", selector);
  return objectsSelectBySelectorResult(
    appController, selectorParams, {}, metadataStore, moduleStore, objectRegistry);
}

McpBridgeToolResult moduleRenderReviewResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto selectorParams = params;
  auto selector = selectorFromParams(params);
  selector.insert("moduleId", params.value("moduleId"));
  selectorParams.insert("selector", selector);
  return renderReviewSelectorResult(
    appController, selectorParams, history, metadataStore, moduleStore, objectRegistry);
}

McpBridgeToolResult moduleValidateResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }
  const auto moduleId = params.value("moduleId").toString().trimmed();
  if (moduleId.isEmpty())
  {
    return invalidParamsFailure("module_validate requires moduleId");
  }
  auto& map = mapWindow->document().map();
  const auto documentFingerprint = documentFingerprintForMap(map);
  auto module =
    mergedModuleRecord(moduleId, documentFingerprint, metadataStore, moduleStore);
  auto warnings = QJsonArray{};
  auto nodes = std::vector<mdl::Node*>{};
  auto staleCount = 0;
  for (const auto& objectId : module.objectIds)
  {
    if (auto* node = resolveObjectId(map, objectId, objectRegistry, warnings))
    {
      nodes.push_back(node);
    }
    else
    {
      ++staleCount;
    }
  }

  auto result = QJsonObject{
    {"tool", "module_validate"},
    {"moduleId", moduleId},
    {"valid", staleCount == 0 && !nodes.empty()},
    {"objectCount", module.objectIds.size()},
    {"liveObjectCount", static_cast<int>(nodes.size())},
    {"staleObjectCount", staleCount},
    {"operationIds", stringsToJson(module.operationIds)},
    {"warnings", warnings},
  };
  if (!nodes.empty())
  {
    result.insert("bounds", boundsToJson(boundsForNodes(nodes)));
  }

  if (params.value("checkRouteContinuity").toBool(false))
  {
    auto continuityWarnings = QJsonArray{};
    auto continuityError = QString{};
    auto continuityNodes = std::vector<mdl::Node*>{};
    if (params.value("continuitySelector").isObject())
    {
      auto selector = params.value("continuitySelector").toObject();
      if (!selector.contains("moduleId"))
      {
        selector.insert("moduleId", moduleId);
      }
      continuityNodes = resolveSelectorNodes(
        map,
        selector,
        history,
        metadataStore,
        moduleStore,
        objectRegistry,
        continuityWarnings,
        continuityError);
      if (!continuityError.isEmpty())
      {
        return invalidParamsFailure(continuityError);
      }
    }
    else
    {
      continuityNodes = defaultContinuityNodes(map, nodes, metadataStore, objectRegistry);
      if (continuityNodes.empty() && !nodes.empty())
      {
        continuityWarnings.push_back(
          "moduleValidateRouteContinuityUsedAllObjects: no walkable route "
          "metadata found; pass continuitySelector to avoid rails/supports/markers.");
        continuityNodes = nodes;
      }
    }

    for (const auto& warning : continuityWarnings)
    {
      warnings.push_back(warning);
    }
    result.insert("routeContinuityObjectCount", static_cast<int>(continuityNodes.size()));
    result.insert("warnings", warnings);

    auto continuityParams =
      QJsonObject{{"objectIds", nodeIdsJson(map, continuityNodes, objectRegistry)}};
    if (params.value("start").isArray())
    {
      continuityParams.insert("start", params.value("start"));
    }
    if (params.value("end").isArray())
    {
      continuityParams.insert("end", params.value("end"));
    }
    if (params.value("routeDirection").isArray())
    {
      continuityParams.insert("routeDirection", params.value("routeDirection"));
    }
    for (const auto& key :
         {"continuityMode",
          "maxStepHeight",
          "maxJumpGap",
          "verticalTolerance",
          "horizontalTolerance",
          "minUpNormal",
          "orderBy",
          "closedLoop"})
    {
      if (params.contains(key))
      {
        continuityParams.insert(key, params.value(key));
      }
    }
    const auto continuity = geometryAnalyzeRouteContinuityForMapResult(
      map, continuityParams, history, &objectRegistry, &metadataStore, &moduleStore);
    result.insert("routeContinuity", continuity.result);
    if (
      continuity.ok
      && !continuity.result.value("semanticContinuous")
            .toBool(continuity.result.value("continuous").toBool(false)))
    {
      result.insert("valid", false);
    }
  }
  return McpBridgeToolResult::success(result);
}

McpBridgeToolResult moduleCompactResult(
  AppController& appController,
  const QJsonObject& params,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }
  const auto moduleId = params.value("moduleId").toString().trimmed();
  if (moduleId.isEmpty())
  {
    return invalidParamsFailure("module_compact requires moduleId");
  }
  auto& map = mapWindow->document().map();
  const auto documentFingerprint = documentFingerprintForMap(map);
  auto warnings = QJsonArray{};
  auto removedMetadataCount = 0;
  for (auto it = metadataStore.begin(); it != metadataStore.end();)
  {
    const auto& record = it->second;
    if (
      recordMatchesDocument(record, documentFingerprint)
      && record.metadata.value("moduleId").toString() == moduleId && record.stale)
    {
      it = metadataStore.erase(it);
      ++removedMetadataCount;
    }
    else
    {
      ++it;
    }
  }

  auto removedObjectIdCount = 0;
  const auto key = moduleStoreKey(documentFingerprint, moduleId);
  auto moduleIt = moduleStore.find(key);
  if (moduleIt == moduleStore.end())
  {
    for (auto it = moduleStore.begin(); it != moduleStore.end(); ++it)
    {
      if (
        it->second.moduleId == moduleId
        && recordMatchesDocument(it->second, documentFingerprint))
      {
        moduleIt = it;
        break;
      }
    }
  }
  if (moduleIt != moduleStore.end())
  {
    auto& objectIds = moduleIt->second.objectIds;
    const auto before = objectIds.size();
    objectIds.erase(
      std::remove_if(
        objectIds.begin(),
        objectIds.end(),
        [&](const auto& objectId) {
          return resolveObjectId(map, objectId, objectRegistry, warnings) == nullptr;
        }),
      objectIds.end());
    removedObjectIdCount = before - objectIds.size();
  }

  const auto module =
    mergedModuleRecord(moduleId, documentFingerprint, metadataStore, moduleStore);
  auto summary = moduleSummary(map, module, metadataStore, objectRegistry);
  summary.insert("tool", "module_compact");
  summary.insert("removedStaleMetadataCount", removedMetadataCount);
  summary.insert("removedStaleObjectIdCount", removedObjectIdCount);
  summary.insert("warnings", warnings);
  return McpBridgeToolResult::success(summary);
}

McpBridgeToolResult irValidateResult(
  AppController& appController, const QJsonObject& params)
{
  auto error = QString{};
  const auto ir = irFromParams(params, error);
  if (!ir)
  {
    return invalidParamsFailure(error);
  }
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  auto preview = mapWindow != nullptr
                   ? irPreviewJsonForMap(mapWindow->document().map(), *ir)
                   : irPreviewJson(*ir);
  preview.insert("tool", "ir_validate");
  return McpBridgeToolResult::success(preview);
}

McpBridgeToolResult irCompilePreviewResult(
  AppController& appController, const QJsonObject& params)
{
  auto error = QString{};
  const auto ir = irFromParams(params, error);
  if (!ir)
  {
    return invalidParamsFailure(error);
  }
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  auto preview = mapWindow != nullptr
                   ? irPreviewJsonForMap(mapWindow->document().map(), *ir)
                   : irPreviewJson(*ir);
  preview.insert("tool", "ir_compile_preview");
  preview.insert("willCommit", false);
  return McpBridgeToolResult::success(preview);
}

McpBridgeToolResult irCompilePreviewFromFileResult(
  AppController& appController, const QJsonObject& params)
{
  auto error = QString{};
  const auto ir = irFromFileParams(params, error);
  if (!ir)
  {
    return invalidParamsFailure(error);
  }
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  auto preview = mapWindow != nullptr
                   ? irPreviewJsonForMap(mapWindow->document().map(), *ir)
                   : irPreviewJson(*ir);
  preview.insert("tool", "ir_compile_preview_from_file");
  preview.insert("willCommit", false);
  preview.insert("sourcePath", params.value("path").toString());
  return McpBridgeToolResult::success(preview);
}

McpBridgeToolResult irCompilePreviewFromFileForMapResult(
  mdl::Map& map, const QJsonObject& params)
{
  auto error = QString{};
  const auto ir = irFromFileParams(params, error);
  if (!ir)
  {
    return invalidParamsFailure(error);
  }
  auto preview = irPreviewJsonForMap(map, *ir);
  preview.insert("tool", "ir_compile_preview_from_file");
  preview.insert("willCommit", false);
  preview.insert("sourcePath", params.value("path").toString());
  return McpBridgeToolResult::success(preview);
}

McpBridgeToolResult irApplyResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }
  auto& map = mapWindow->document().map();
  auto error = QString{};
  const auto ir = irFromParams(params, error);
  if (!ir)
  {
    return invalidParamsFailure(error);
  }

  return irApplyForMapResult(
    map,
    toolName,
    QJsonObject{{"ir", *ir}},
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore);
}

McpBridgeToolResult irApplyForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore)
{
  auto error = QString{};
  const auto ir = irFromParams(params, error);
  if (!ir)
  {
    return invalidParamsFailure(error);
  }

  const auto documentFingerprint = documentFingerprintForMap(map);
  const auto defaultMetadata = ir->value("defaultMetadata").isObject()
                                 ? ir->value("defaultMetadata").toObject()
                                 : QJsonObject{};
  const auto moduleId =
    ir->value("moduleId").toString(defaultMetadata.value("moduleId").toString());
  auto mergedDefaultMetadata = defaultMetadata;
  if (!moduleId.isEmpty() && !mergedDefaultMetadata.contains("moduleId"))
  {
    mergedDefaultMetadata.insert("moduleId", moduleId);
  }

  auto appliedOperations = QJsonArray{};
  auto operationIds = QStringList{};
  auto objectIds = QStringList{};
  auto warnings = QJsonArray{};
  auto ok = true;

  if (const auto operations = ir->value("operations"); operations.isArray())
  {
    auto batchParams = QJsonObject{
      {"operations", operations.toArray()},
      {"name", ir->value("name").toString("MCP: Apply IR blockout")},
      {"select", ir->value("select").toBool(true)},
      {"detail", "ids"},
    };
    if (ir->contains("grid"))
    {
      batchParams.insert("grid", ir->value("grid"));
    }
    if (ir->contains("material"))
    {
      batchParams.insert("material", ir->value("material"));
    }
    if (!mergedDefaultMetadata.isEmpty())
    {
      batchParams.insert("defaultMetadata", mergedDefaultMetadata);
    }
    auto blockoutResult = blockoutCreateBatchForMapResult(
      map,
      "blockout_create_batch",
      batchParams,
      history,
      nextOperationIndex,
      &metadataStore,
      &moduleStore);
    ok =
      ok && blockoutResult.ok
      && blockoutResult.result.value("validation").toObject().value("valid").toBool(true);
    appliedOperations.push_back(blockoutResult.result);
    if (!ok)
    {
      warnings.push_back("irApplyPreflightFailed");
      auto resultObject = QJsonObject{
        {"tool", toolName},
        {"valid", false},
        {"moduleId", moduleId},
        {"operationIds", QJsonArray{}},
        {"operationCount", 0},
        {"changedObjectCount", 0},
        {"changedObjectIds", QJsonArray{}},
        {"applied", appliedOperations},
        {"preview", irPreviewJsonForMap(map, *ir)},
        {"warnings", warnings},
      };
      if (!moduleId.isEmpty())
      {
        resultObject.insert("resourceUri", QString{"tbmcp://module/%1"}.arg(moduleId));
      }
      return McpBridgeToolResult::success(resultObject);
    }
    const auto operationId = blockoutResult.result.value("operationId").toString();
    if (!operationId.isEmpty())
    {
      operationIds.push_back(operationId);
    }
    for (const auto& value : blockoutResult.result.value("changedObjectIds").toArray())
    {
      if (value.isString())
      {
        objectIds.push_back(value.toString());
      }
    }
    mergeModuleFromOperationResult(
      blockoutResult.result, documentFingerprint, mergedDefaultMetadata, moduleStore);
  }

  if (const auto entities = ir->value("entities"); entities.isArray())
  {
    auto entityParams = QJsonObject{
      {"entities", entities.toArray()},
      {"transactionName",
       ir->value("entityTransactionName").toString("MCP: Apply IR entities")},
      {"select", ir->value("selectEntities").toBool(false)},
      {"detail", "ids"},
    };
    auto result = createEntityCheckedBatchForMapResult(
      map, "entity_create_checked_batch", entityParams, history, nextOperationIndex);
    ok = ok && result.ok;
    appliedOperations.push_back(result.result);
    const auto operationId = result.result.value("operationId").toString();
    if (!operationId.isEmpty())
    {
      operationIds.push_back(operationId);
    }
    for (const auto& value : result.result.value("changedObjectIds").toArray())
    {
      if (value.isString())
      {
        objectIds.push_back(value.toString());
      }
    }
    mergeModuleFromOperationResult(
      result.result, documentFingerprint, mergedDefaultMetadata, moduleStore);
  }

  operationIds.removeDuplicates();
  objectIds.removeDuplicates();
  if (!ok)
  {
    warnings.push_back("irApplyPartialFailure");
  }

  auto result = QJsonObject{
    {"tool", toolName},
    {"valid", ok},
    {"moduleId", moduleId},
    {"operationIds", stringsToJson(operationIds)},
    {"operationCount", operationIds.size()},
    {"changedObjectCount", objectIds.size()},
    {"changedObjectIds", stringsToJson(objectIds)},
    {"applied", appliedOperations},
    {"preview", irPreviewJsonForMap(map, *ir)},
    {"warnings", warnings},
  };
  if (!moduleId.isEmpty())
  {
    result.insert("resourceUri", QString{"tbmcp://module/%1"}.arg(moduleId));
  }
  return McpBridgeToolResult::success(result);
}

McpBridgeToolResult irApplyFromFileResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore)
{
  auto error = QString{};
  const auto ir = irFromFileParams(params, error);
  if (!ir)
  {
    return invalidParamsFailure(error);
  }
  auto applyParams = params;
  applyParams.insert("ir", *ir);
  auto result = irApplyResult(
    appController,
    toolName,
    applyParams,
    history,
    nextOperationIndex,
    metadataStore,
    moduleStore);
  if (result.ok)
  {
    result.result.insert("sourcePath", params.value("path").toString());
  }
  return result;
}

McpBridgeToolResult irApplyFromFileForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore)
{
  auto error = QString{};
  const auto ir = irFromFileParams(params, error);
  if (!ir)
  {
    return invalidParamsFailure(error);
  }
  auto applyParams = params;
  applyParams.insert("ir", *ir);
  auto result = irApplyForMapResult(
    map, toolName, applyParams, history, nextOperationIndex, metadataStore, moduleStore);
  if (result.ok)
  {
    result.result.insert("sourcePath", params.value("path").toString());
  }
  return result;
}

} // namespace tb::ui
