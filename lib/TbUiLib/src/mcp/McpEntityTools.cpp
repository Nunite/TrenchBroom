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

#include "Color.h"
#include "Macros.h"
#include "McpBridgeServerTools.h"
#include "mcp/McpError.h"
#include "mdl/AddRemoveNodesCommand.h"
#include "mdl/Brush.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/EntityProperties.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Node.h"
#include "mdl/NodeContents.h"
#include "mdl/PatchNode.h"
#include "mdl/PropertyDefinition.h"
#include "mdl/Selection.h"
#include "mdl/SwapNodeContentsCommand.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"

#include "kd/vector_utils.h"

#include "vm/bbox.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <type_traits>
#include <vector>

namespace tb::ui
{
namespace mcp = tb::mcp;

namespace
{

QJsonArray vecToJson(const vm::vec3d& value)
{
  return QJsonArray{
    value.x(),
    value.y(),
    value.z(),
  };
}

QJsonObject boundsToJson(const vm::bbox3d& bounds)
{
  return QJsonObject{
    {"min", vecToJson(bounds.min)},
    {"max", vecToJson(bounds.max)},
  };
}

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

QString nodeTypeName(const mdl::Node& node)
{
  if (dynamic_cast<const mdl::WorldNode*>(&node) != nullptr)
  {
    return "world";
  }
  if (dynamic_cast<const mdl::EntityNode*>(&node) != nullptr)
  {
    return "entity";
  }
  if (dynamic_cast<const mdl::BrushNode*>(&node) != nullptr)
  {
    return "brush";
  }
  if (dynamic_cast<const mdl::PatchNode*>(&node) != nullptr)
  {
    return "patch";
  }
  return "node";
}

QJsonObject nodeSummaryJson(const mdl::Node& node, const mdl::WorldNode& worldNode)
{
  auto result = QJsonObject{
    {"id", nodePathId(node, worldNode)},
    {"type", nodeTypeName(node)},
    {"name", QString::fromStdString(node.name())},
    {"selected", node.selected()},
    {"childCount", static_cast<int>(node.childCount())},
    {"descendantCount", static_cast<int>(node.descendantCount())},
    {"logicalBounds", boundsToJson(node.logicalBounds())},
  };

  if (const auto* nodeAsWorld = dynamic_cast<const mdl::WorldNode*>(&node))
  {
    result.insert("classname", QString::fromStdString(nodeAsWorld->entity().classname()));
  }
  else if (const auto* entityNode = dynamic_cast<const mdl::EntityNode*>(&node))
  {
    result.insert("classname", QString::fromStdString(entityNode->entity().classname()));
  }
  else if (const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(&node))
  {
    result.insert("faceCount", static_cast<int>(brushNode->brush().faceCount()));
  }

  return result;
}

bool textMatches(const QString& text, const QString& query)
{
  return text.contains(query, Qt::CaseInsensitive);
}

QString makeOperationId(int& nextOperationIndex)
{
  return QString{"mcp-op-%1"}.arg(nextOperationIndex++);
}

QJsonObject mutationResultJson(const McpOperationRecord& operation)
{
  auto result = QJsonObject{};
  result.insert("operationId", operation.operationId);
  result.insert("transactionName", operation.transactionName);
  result.insert("changedObjectIds", operation.changedObjectIdsJson());
  result.insert("changedObjectCount", operation.changedObjectIds.size());
  return result;
}

void mcpRecordOperation(
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const QString& toolName,
  const QString& transactionName,
  const QJsonArray& changedObjectIds,
  QJsonObject& result)
{
  auto operation = McpOperationRecord{};
  operation.operationId = makeOperationId(nextOperationIndex);
  operation.toolName = toolName;
  operation.transactionName = transactionName;
  operation.setChangedObjectIds(changedObjectIds);
  result = mutationResultJson(operation);
  history.push_back(std::move(operation));
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

bool mcpOptionalBool(
  const QJsonObject& params, const QString& key, const bool defaultValue)
{
  const auto value = params.value(key);
  return value.isBool() ? value.toBool() : defaultValue;
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

std::optional<std::map<std::string, std::string>> stringMapFromJson(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto value = params.value(key);
  if (value.isUndefined())
  {
    return std::map<std::string, std::string>{};
  }
  if (!value.isObject())
  {
    error = QString{"%1 must be an object"}.arg(key);
    return std::nullopt;
  }

  auto result = std::map<std::string, std::string>{};
  const auto object = value.toObject();
  for (auto it = object.begin(); it != object.end(); ++it)
  {
    if (!it.value().isString())
    {
      error = QString{"%1.%2 must be a string"}.arg(key, it.key());
      return std::nullopt;
    }
    result.emplace(it.key().toStdString(), it.value().toString().toStdString());
  }
  return result;
}

std::optional<std::vector<std::string>> stringArrayFromJson(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto value = params.value(key);
  if (value.isUndefined())
  {
    return std::vector<std::string>{};
  }
  if (!value.isArray())
  {
    error = QString{"%1 must be an array"}.arg(key);
    return std::nullopt;
  }

  auto result = std::vector<std::string>{};
  for (const auto& item : value.toArray())
  {
    if (!item.isString())
    {
      error = QString{"%1 must contain only strings"}.arg(key);
      return std::nullopt;
    }
    result.push_back(item.toString().toStdString());
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

std::optional<QJsonArray> addNodesWithTransaction(
  mdl::Map& map,
  const QString& transactionName,
  const std::vector<mdl::Node*>& nodes,
  const bool selectCreated)
{
  auto addedNodes = std::vector<mdl::Node*>{};
  auto ok = executeTransaction(map, transactionName, [&]() {
    if (selectCreated)
    {
      mdl::deselectAll(map);
    }

    auto* parent = mdl::parentForNodes(map);
    if (!parent || !parent->canAddChildren(std::begin(nodes), std::end(nodes)))
    {
      return false;
    }

    auto nodesToAdd = std::map<mdl::Node*, std::vector<mdl::Node*>>{};
    nodesToAdd.emplace(parent, nodes);
    if (!map.executeAndStore(mdl::AddRemoveNodesCommand::add(nodesToAdd)))
    {
      return false;
    }

    addedNodes = nodes;
    if (selectCreated)
    {
      mdl::selectNodes(map, addedNodes);
    }
    return true;
  });

  if (!ok)
  {
    return std::nullopt;
  }

  auto result = QJsonArray{};
  for (const auto* node : addedNodes)
  {
    result.push_back(nodePathId(*node, map.worldNode()));
  }
  return result;
}

std::optional<QJsonArray> updateNodeContentsWithTransaction(
  mdl::Map& map,
  const QString& transactionName,
  std::vector<std::pair<mdl::Node*, mdl::NodeContents>> nodesToSwap)
{
  auto changedNodes = QJsonArray{};
  for (const auto& [node, contents] : nodesToSwap)
  {
    unused(contents);
    changedNodes.push_back(nodePathId(*node, map.worldNode()));
  }

  const auto ok = executeTransaction(map, transactionName, [&]() {
    return map.executeAndStore(std::make_unique<mdl::SwapNodeContentsCommand>(
      transactionName.toStdString(), std::move(nodesToSwap)));
  });

  return ok ? std::optional{changedNodes} : std::nullopt;
}

std::optional<QJsonArray> removeNodeWithTransaction(
  mdl::Map& map, const QString& transactionName, mdl::Node& node)
{
  if (&node == &map.worldNode())
  {
    return std::nullopt;
  }
  auto* parent = node.parent();
  if (!parent || !parent->canRemoveChild(node))
  {
    return std::nullopt;
  }

  const auto removedId = nodePathId(node, map.worldNode());
  const auto ok = executeTransaction(map, transactionName, [&]() {
    mdl::deselectNodes(map, {&node});
    return map.executeAndStore(mdl::AddRemoveNodesCommand::remove({{parent, {&node}}}));
  });

  return ok ? std::optional{QJsonArray{removedId}} : std::nullopt;
}

QString entityDefinitionTypeName(const mdl::EntityDefinition& definition)
{
  return mdl::getType(definition) == mdl::EntityDefinitionType::Point ? "point" : "brush";
}

QString propertyValueTypeName(const mdl::PropertyValueType& type)
{
  return std::visit(
    [](const auto& valueType) -> QString {
      using T = std::decay_t<decltype(valueType)>;
      if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::LinkTarget>)
      {
        return "target";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::LinkSource>)
      {
        return "target_source";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::String>)
      {
        return "string";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Boolean>)
      {
        return "boolean";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Integer>)
      {
        return "integer";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Float>)
      {
        return "float";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Choice>)
      {
        return "choice";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Flags>)
      {
        return "flags";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Origin>)
      {
        return "origin";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Input>)
      {
        return "input";
      }
      else if constexpr (std::is_same_v<T, mdl::PropertyValueTypes::Output>)
      {
        return "output";
      }
      else if constexpr (
        std::is_same_v<T, mdl::PropertyValueTypes::Color<RgbF>>
        || std::is_same_v<T, mdl::PropertyValueTypes::Color<RgbB>>
        || std::is_same_v<T, mdl::PropertyValueTypes::Color<Rgb>>)
      {
        return "color";
      }
      else
      {
        return "unknown";
      }
    },
    type);
}

QJsonObject propertyDefinitionJson(const mdl::PropertyDefinition& property)
{
  auto result = QJsonObject{
    {"key", QString::fromStdString(property.key)},
    {"type", propertyValueTypeName(property.valueType)},
    {"shortDescription", QString::fromStdString(property.shortDescription)},
    {"longDescription", QString::fromStdString(property.longDescription)},
    {"readOnly", property.readOnly},
  };

  if (const auto defaultValue = mdl::PropertyDefinition::defaultValue(property))
  {
    result.insert("defaultValue", QString::fromStdString(*defaultValue));
  }

  return result;
}

QJsonObject entityDefinitionJson(const mdl::EntityDefinition& definition)
{
  auto properties = QJsonArray{};
  for (const auto& property : definition.propertyDefinitions)
  {
    properties.push_back(propertyDefinitionJson(property));
  }

  auto result = QJsonObject{
    {"classname", QString::fromStdString(definition.name)},
    {"type", entityDefinitionTypeName(definition)},
    {"description", QString::fromStdString(definition.description)},
    {"propertyCount", properties.size()},
    {"properties", properties},
  };

  if (const auto* pointDefinition = mdl::getPointEntityDefinition(&definition))
  {
    result.insert("bounds", boundsToJson(pointDefinition->bounds));
  }

  return result;
}

std::optional<std::vector<mdl::BrushNode*>> brushNodesFromParamsOrSelection(
  mdl::Map& map, const QJsonObject& params, QString& error)
{
  auto brushes = std::vector<mdl::BrushNode*>{};
  const auto objectIdsValue = params.value("objectIds");
  if (objectIdsValue.isUndefined())
  {
    brushes = map.selection().brushes;
  }
  else
  {
    if (!objectIdsValue.isArray())
    {
      error = "objectIds must be an array";
      return std::nullopt;
    }
    for (const auto& objectIdValue : objectIdsValue.toArray())
    {
      if (!objectIdValue.isString())
      {
        error = "objectIds must contain only strings";
        return std::nullopt;
      }
      auto* node = resolveNodeId(map.worldNode(), objectIdValue.toString());
      auto* brushNode = dynamic_cast<mdl::BrushNode*>(node);
      if (!brushNode)
      {
        error = QString{"Object is not a brush: %1"}.arg(objectIdValue.toString());
        return std::nullopt;
      }
      brushes.push_back(brushNode);
    }
  }

  brushes = kdl::vec_sort_and_remove_duplicates(std::move(brushes));
  if (brushes.empty())
  {
    error = "No brushes selected or specified";
    return std::nullopt;
  }
  return brushes;
}

} // namespace

McpBridgeToolResult createEntityResult(
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

  const auto classname = params.value("classname").toString().trimmed();
  if (classname.isEmpty())
  {
    return invalidParamsFailure("entity_create requires classname");
  }

  auto error = QString{};
  const auto properties = stringMapFromJson(params, "properties", error);
  if (!properties)
  {
    return invalidParamsFailure(error);
  }

  auto entity =
    mdl::Entity{{{mdl::EntityPropertyKeys::Classname, classname.toStdString()}}};
  if (const auto origin = params.value("origin"); !origin.isUndefined())
  {
    const auto originVec = mcpVec3FromJson(params, "origin", error);
    if (!originVec)
    {
      return invalidParamsFailure(error);
    }
    entity.setOrigin(*originVec);
  }

  for (const auto& [key, value] : *properties)
  {
    if (key != mdl::EntityPropertyKeys::Classname)
    {
      entity.addOrUpdateProperty(key, value);
    }
  }

  auto& map = mapWindow->document().map();
  auto* entityNode = new mdl::EntityNode{std::move(entity)};
  const auto transactionName = QString{"MCP: Create %1"}.arg(classname);
  const auto changedObjectIds = addNodesWithTransaction(
    map, transactionName, {entityNode}, mcpOptionalBool(params, "select", true));
  if (!changedObjectIds)
  {
    delete entityNode;
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not create entity");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history, nextOperationIndex, toolName, transactionName, *changedObjectIds, result);
  result.insert("entity", nodeSummaryJson(*entityNode, map.worldNode()));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult updateEntityResult(
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

  const auto objectId = params.value("objectId").toString().trimmed();
  if (objectId.isEmpty())
  {
    return invalidParamsFailure("entity_update requires objectId");
  }

  auto& map = mapWindow->document().map();
  auto* node = resolveNodeId(map.worldNode(), objectId);
  auto* entityNode = dynamic_cast<mdl::EntityNodeBase*>(node);
  if (!entityNode)
  {
    return invalidParamsFailure(QString{"Object is not an entity: %1"}.arg(objectId));
  }

  auto error = QString{};
  const auto properties = stringMapFromJson(params, "properties", error);
  if (!properties)
  {
    return invalidParamsFailure(error);
  }
  const auto removeKeys = stringArrayFromJson(params, "removeKeys", error);
  if (!removeKeys)
  {
    return invalidParamsFailure(error);
  }

  auto entity = entityNode->entity();
  for (const auto& [key, value] : *properties)
  {
    entity.addOrUpdateProperty(key, value);
  }
  for (const auto& key : *removeKeys)
  {
    if (key != mdl::EntityPropertyKeys::Classname)
    {
      entity.removeProperty(key);
    }
  }

  const auto transactionName = QString{"MCP: Update entity"};
  auto nodesToSwap = std::vector<std::pair<mdl::Node*, mdl::NodeContents>>{};
  nodesToSwap.emplace_back(entityNode, mdl::NodeContents{std::move(entity)});
  const auto changedObjectIds =
    updateNodeContentsWithTransaction(map, transactionName, std::move(nodesToSwap));
  if (!changedObjectIds)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not update entity");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history, nextOperationIndex, toolName, transactionName, *changedObjectIds, result);
  result.insert("entity", nodeSummaryJson(*entityNode, map.worldNode()));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult deleteEntityResult(
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

  const auto objectId = params.value("objectId").toString().trimmed();
  if (objectId.isEmpty())
  {
    return invalidParamsFailure("entity_delete requires objectId");
  }

  auto& map = mapWindow->document().map();
  auto* node = resolveNodeId(map.worldNode(), objectId);
  if (!node)
  {
    return invalidParamsFailure(QString{"Unknown MCP object id: %1"}.arg(objectId));
  }

  const auto transactionName = QString{"MCP: Delete object"};
  const auto changedObjectIds = removeNodeWithTransaction(map, transactionName, *node);
  if (!changedObjectIds)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden,
      QString{"Object cannot be deleted: %1"}.arg(objectId));
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history, nextOperationIndex, toolName, transactionName, *changedObjectIds, result);
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult fgdEntitiesListResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  const auto type = params.value("type").toString().trimmed().toLower();
  const auto query = params.value("query").toString().trimmed();
  const auto limit = optionalSize(params, "limit", 200);

  auto definitions = QJsonArray{};
  for (const auto& definition :
       mapWindow->document().map().entityDefinitionManager().definitions())
  {
    if (!type.isEmpty() && entityDefinitionTypeName(definition) != type)
    {
      continue;
    }
    if (
      !query.isEmpty() && !textMatches(QString::fromStdString(definition.name), query)
      && !textMatches(QString::fromStdString(definition.description), query))
    {
      continue;
    }

    definitions.push_back(QJsonObject{
      {"classname", QString::fromStdString(definition.name)},
      {"type", entityDefinitionTypeName(definition)},
      {"description", QString::fromStdString(definition.description)},
      {"propertyCount", static_cast<int>(definition.propertyDefinitions.size())},
    });

    if (definitions.size() >= static_cast<int>(limit))
    {
      break;
    }
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"definitions", definitions},
    {"count", definitions.size()},
  });
}

McpBridgeToolResult entitySchemaResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  const auto classname = params.value("classname").toString().trimmed();
  if (classname.isEmpty())
  {
    return invalidParamsFailure("entity_schema requires classname");
  }

  const auto* definition =
    mapWindow->document().map().entityDefinitionManager().definition(
      classname.toStdString());
  if (!definition)
  {
    return invalidParamsFailure(QString{"Unknown entity classname: %1"}.arg(classname));
  }

  return McpBridgeToolResult::success(entityDefinitionJson(*definition));
}

McpBridgeToolResult createEntityFromSchemaResult(
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

  const auto classname = params.value("classname").toString().trimmed();
  if (classname.isEmpty())
  {
    return invalidParamsFailure("entity_create_from_schema requires classname");
  }

  auto& map = mapWindow->document().map();
  const auto* definition =
    map.entityDefinitionManager().definition(classname.toStdString());
  if (!definition || mdl::getType(*definition) != mdl::EntityDefinitionType::Point)
  {
    return invalidParamsFailure(
      QString{"Unknown point entity classname: %1"}.arg(classname));
  }

  auto error = QString{};
  const auto properties = stringMapFromJson(params, "properties", error);
  if (!properties)
  {
    return invalidParamsFailure(error);
  }

  auto origin = vm::vec3d{0, 0, 0};
  if (const auto originValue = params.value("origin"); !originValue.isUndefined())
  {
    const auto parsedOrigin = mcpVec3FromJson(params, "origin", error);
    if (!parsedOrigin)
    {
      return invalidParamsFailure(error);
    }
    origin = *parsedOrigin;
  }

  auto entity =
    mdl::Entity{{{mdl::EntityPropertyKeys::Classname, classname.toStdString()}}};
  mdl::setDefaultProperties(*definition, entity, mdl::SetDefaultPropertyMode::SetAll);
  entity.setOrigin(origin);
  for (const auto& [key, value] : *properties)
  {
    entity.addOrUpdateProperty(key, value);
  }

  auto* entityNode = new mdl::EntityNode{std::move(entity)};
  const auto transactionName = QString{"MCP: Create %1"}.arg(classname);
  const auto changedObjectIds = addNodesWithTransaction(
    map, transactionName, {entityNode}, mcpOptionalBool(params, "select", true));
  if (!changedObjectIds)
  {
    delete entityNode;
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError,
      QString{"Failed to create entity from schema: %1"}.arg(classname));
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history, nextOperationIndex, toolName, transactionName, *changedObjectIds, result);
  result.insert("classname", classname);
  result.insert("entity", nodeSummaryJson(*entityNode, map.worldNode()));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult createEntityCheckedResult(
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

  const auto classname = params.value("classname").toString().trimmed();
  if (classname.isEmpty())
  {
    return invalidParamsFailure("entity_create_checked requires classname");
  }

  const auto* definition =
    mapWindow->document().map().entityDefinitionManager().definition(
      classname.toStdString());
  if (!definition)
  {
    return invalidParamsFailure(
      QString{"FGD does not define entity classname: %1"}.arg(classname));
  }
  if (mdl::getType(*definition) != mdl::EntityDefinitionType::Point)
  {
    return invalidParamsFailure(
      QString{"entity_create_checked only creates point entities; %1 is %2"}
        .arg(classname)
        .arg(entityDefinitionTypeName(*definition)));
  }

  auto result = createEntityFromSchemaResult(
    appController, toolName, params, history, nextOperationIndex);
  if (result.ok)
  {
    result.result.insert("checked", true);
    result.result.insert("schemaClassname", classname);
  }
  return result;
}

McpBridgeToolResult createEntityCheckedBatchResult(
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

  return createEntityCheckedBatchForMapResult(
    mapWindow->document().map(), toolName, params, history, nextOperationIndex);
}

McpBridgeToolResult createEntityCheckedBatchForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  const auto entitiesValue = params.value("entities");
  if (!entitiesValue.isArray())
  {
    return invalidParamsFailure("entity_create_checked_batch requires entities array");
  }

  const auto entitiesArray = entitiesValue.toArray();
  if (entitiesArray.isEmpty())
  {
    return invalidParamsFailure("entities must not be empty");
  }

  auto nodes = std::vector<mdl::Node*>{};
  auto createdClassnames = QJsonArray{};

  const auto cleanupNodes = [&] {
    for (auto* node : nodes)
    {
      delete node;
    }
    nodes.clear();
  };

  for (auto i = 0; i < entitiesArray.size(); ++i)
  {
    const auto entityValue = entitiesArray[i];
    if (!entityValue.isObject())
    {
      cleanupNodes();
      return invalidParamsFailure(QString{"entities[%1] must be an object"}.arg(i));
    }

    const auto entityParams = entityValue.toObject();
    const auto classname = entityParams.value("classname").toString().trimmed();
    if (classname.isEmpty())
    {
      cleanupNodes();
      return invalidParamsFailure(QString{"entities[%1] requires classname"}.arg(i));
    }

    const auto* definition =
      map.entityDefinitionManager().definition(classname.toStdString());
    if (!definition)
    {
      cleanupNodes();
      return invalidParamsFailure(
        QString{"FGD does not define entity classname: %1"}.arg(classname));
    }
    if (mdl::getType(*definition) != mdl::EntityDefinitionType::Point)
    {
      cleanupNodes();
      return invalidParamsFailure(
        QString{"entity_create_checked_batch only creates point entities; %1 is %2"}
          .arg(classname)
          .arg(entityDefinitionTypeName(*definition)));
    }

    auto error = QString{};
    const auto properties = stringMapFromJson(entityParams, "properties", error);
    if (!properties)
    {
      cleanupNodes();
      return invalidParamsFailure(QString{"entities[%1].%2"}.arg(i).arg(error));
    }

    auto origin = vm::vec3d{0, 0, 0};
    if (const auto originValue = entityParams.value("origin"); !originValue.isUndefined())
    {
      const auto parsedOrigin = mcpVec3FromJson(entityParams, "origin", error);
      if (!parsedOrigin)
      {
        cleanupNodes();
        return invalidParamsFailure(QString{"entities[%1].%2"}.arg(i).arg(error));
      }
      origin = *parsedOrigin;
    }

    auto entity =
      mdl::Entity{{{mdl::EntityPropertyKeys::Classname, classname.toStdString()}}};
    mdl::setDefaultProperties(*definition, entity, mdl::SetDefaultPropertyMode::SetAll);
    entity.setOrigin(origin);
    for (const auto& [key, value] : *properties)
    {
      entity.addOrUpdateProperty(key, value);
    }

    nodes.push_back(new mdl::EntityNode{std::move(entity)});
    createdClassnames.push_back(classname);
  }

  const auto transactionName =
    params.value("transactionName").toString("MCP: Create checked entities").trimmed();
  const auto finalTransactionName =
    transactionName.isEmpty() ? QString{"MCP: Create checked entities"} : transactionName;
  const auto changedObjectIds = addNodesWithTransaction(
    map, finalTransactionName, nodes, mcpOptionalBool(params, "select", true));
  if (!changedObjectIds)
  {
    cleanupNodes();
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not create checked entity batch");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    toolName,
    finalTransactionName,
    *changedObjectIds,
    result);
  result.insert("checked", true);
  result.insert("entityCount", static_cast<int>(nodes.size()));
  result.insert("classNames", createdClassnames);

  const auto detail = params.value("detail").toString("summary").toLower();
  if (detail == "ids" || detail == "full")
  {
    result.insert("changedObjectIds", *changedObjectIds);
  }
  if (detail == "full")
  {
    auto entitySummaries = QJsonArray{};
    for (const auto* node : nodes)
    {
      entitySummaries.push_back(nodeSummaryJson(*node, map.worldNode()));
    }
    result.insert("entities", entitySummaries);
  }

  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult tieBrushesResult(
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

  const auto classname = params.value("classname").toString().trimmed();
  if (classname.isEmpty())
  {
    return invalidParamsFailure("entity_tie_brushes requires classname");
  }

  auto& map = mapWindow->document().map();
  const auto* definition =
    map.entityDefinitionManager().definition(classname.toStdString());
  if (!definition || mdl::getType(*definition) != mdl::EntityDefinitionType::Brush)
  {
    return invalidParamsFailure(
      QString{"Unknown brush entity classname: %1"}.arg(classname));
  }

  auto error = QString{};
  const auto brushes = brushNodesFromParamsOrSelection(map, params, error);
  if (!brushes)
  {
    return invalidParamsFailure(error);
  }

  mdl::deselectAll(map);
  mdl::selectNodes(map, kdl::vec_static_cast<mdl::Node*>(*brushes));
  const auto* entityNode = mdl::createBrushEntity(map, *definition);
  if (!entityNode)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Failed to tie brushes to entity");
  }

  auto changedObjectIds = QJsonArray{nodePathId(*entityNode, map.worldNode())};
  for (const auto* brush : *brushes)
  {
    changedObjectIds.push_back(nodePathId(*brush, map.worldNode()));
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    toolName,
    QString{"MCP: Tie brushes to %1"}.arg(classname),
    changedObjectIds,
    result);
  result.insert("classname", classname);
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult untieBrushesResult(
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
  auto brushes = std::vector<mdl::BrushNode*>{};
  const auto objectIdsValue = params.value("objectIds");
  if (objectIdsValue.isUndefined())
  {
    brushes = map.selection().brushes;
  }
  else if (objectIdsValue.isArray())
  {
    for (const auto& objectIdValue : objectIdsValue.toArray())
    {
      if (!objectIdValue.isString())
      {
        return invalidParamsFailure("objectIds must contain only strings");
      }
      auto* node = resolveNodeId(map.worldNode(), objectIdValue.toString());
      if (auto* brushNode = dynamic_cast<mdl::BrushNode*>(node))
      {
        brushes.push_back(brushNode);
      }
      else if (auto* entityNode = dynamic_cast<mdl::EntityNode*>(node))
      {
        for (auto* child : entityNode->children())
        {
          if (auto* childBrush = dynamic_cast<mdl::BrushNode*>(child))
          {
            brushes.push_back(childBrush);
          }
        }
      }
      else
      {
        return invalidParamsFailure(
          QString{"Object is not a brush or brush entity: %1"}.arg(
            objectIdValue.toString()));
      }
    }
  }
  else
  {
    return invalidParamsFailure("objectIds must be an array");
  }

  brushes.erase(
    std::remove_if(
      brushes.begin(),
      brushes.end(),
      [&](const auto* brush) { return brush->entity() == &map.worldNode(); }),
    brushes.end());
  brushes = kdl::vec_sort_and_remove_duplicates(std::move(brushes));
  if (brushes.empty())
  {
    return invalidParamsFailure("No brush entity brushes selected or specified");
  }

  auto changedObjectIds = QJsonArray{};
  for (const auto* brush : brushes)
  {
    changedObjectIds.push_back(nodePathId(*brush, map.worldNode()));
  }

  const auto nodes = kdl::vec_static_cast<mdl::Node*>(brushes);
  auto* parent = mdl::parentForNodes(map, nodes);
  if (!parent || !mdl::reparentNodes(map, {{parent, nodes}}))
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Failed to untie brushes");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    toolName,
    "MCP: Untie brushes",
    changedObjectIds,
    result);
  return McpBridgeToolResult::success(std::move(result));
}

} // namespace tb::ui
