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

#include "ui/mcp/McpBridgeServer.h"

#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QWidget>

#include "fs/PathMatcher.h"
#include "fs/TraversalMode.h"
#include "gl/Material.h"
#include "gl/MaterialManager.h"
#include "mcp/McpError.h"
#include "mcp/McpToolCatalog.h"
#include "mdl/AddRemoveNodesCommand.h"
#include "mdl/Brush.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFaceHandle.h"
#include "mdl/BrushNode.h"
#include "mdl/CircleShape.h"
#include "mdl/EditorContext.h"
#include "mdl/Entity.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityDefinitionUtils.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/EntityProperties.h"
#include "mdl/GameFileSystem.h"
#include "mdl/GameInfo.h"
#include "mdl/Grid.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Assets.h"
#include "mdl/Map_Brushes.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Map_World.h"
#include "mdl/Node.h"
#include "mdl/NodeContents.h"
#include "mdl/PatchNode.h"
#include "mdl/PropertyDefinition.h"
#include "mdl/Selection.h"
#include "mdl/SwapNodeContentsCommand.h"
#include "mdl/Transaction.h"
#include "mdl/UpdateBrushFaceAttributes.h"
#include "mdl/WorldNode.h"
#include "ui/Action.h"
#include "ui/ActionExecutionContext.h"
#include "ui/ActionManager.h"
#include "ui/AppController.h"
#include "ui/AssetBrowserModel.h"
#include "ui/GetVersion.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"

#include "vm/bbox.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <type_traits>

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

QString pathToQString(const std::filesystem::path& path)
{
  return path.empty() ? QString{} : pathAsQString(path);
}

QString genericPathToQString(const std::filesystem::path& path)
{
  return pathAsGenericQString(path);
}

QString browserCellTypeName(const BrowserCellType type)
{
  switch (type)
  {
  case BrowserCellType::Folder:
    return "folder";
  case BrowserCellType::Model:
    return "model";
  case BrowserCellType::Sprite:
    return "sprite";
  case BrowserCellType::Sound:
    return "sound";
  }
  return "unknown";
}

std::optional<BrowserCellType> browserCellTypeFromString(const QString& type)
{
  if (type.compare("model", Qt::CaseInsensitive) == 0)
  {
    return BrowserCellType::Model;
  }
  if (type.compare("sprite", Qt::CaseInsensitive) == 0)
  {
    return BrowserCellType::Sprite;
  }
  if (type.compare("sound", Qt::CaseInsensitive) == 0)
  {
    return BrowserCellType::Sound;
  }
  return std::nullopt;
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
  if (dynamic_cast<const mdl::LayerNode*>(&node) != nullptr)
  {
    return "layer";
  }
  if (dynamic_cast<const mdl::GroupNode*>(&node) != nullptr)
  {
    return "group";
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

void collectMapCounts(const mdl::Node& node, int& entities, int& brushes, int& patches)
{
  if (dynamic_cast<const mdl::EntityNode*>(&node) != nullptr)
  {
    ++entities;
  }
  else if (dynamic_cast<const mdl::BrushNode*>(&node) != nullptr)
  {
    ++brushes;
  }
  else if (dynamic_cast<const mdl::PatchNode*>(&node) != nullptr)
  {
    ++patches;
  }

  for (const auto* child : node.children())
  {
    collectMapCounts(*child, entities, brushes, patches);
  }
}

QJsonObject documentJson(const MapWindow& mapWindow, const int index)
{
  const auto& map = mapWindow.document().map();
  return QJsonObject{
    {"index", index},
    {"fileName", QString::fromStdString(map.filename())},
    {"path", pathToQString(map.path())},
    {"persistent", map.persistent()},
    {"modified", map.modified()},
    {"game", QString::fromStdString(map.gameInfo().gameConfig.name)},
    {"mapFormat", QString::fromStdString(mdl::formatName(map.worldNode().mapFormat()))},
  };
}

QJsonObject activeDocumentJson(AppController& appController)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return {};
  }

  return documentJson(*mapWindow, 0);
}

QJsonObject mapSnapshotJson(AppController& appController)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return {};
  }

  const auto& map = mapWindow->document().map();
  const auto& worldNode = map.worldNode();
  const auto& grid = map.grid();

  auto entities = 0;
  auto brushes = 0;
  auto patches = 0;
  collectMapCounts(worldNode, entities, brushes, patches);

  auto worldspawn = QJsonObject{};
  for (const auto& property : worldNode.entity().properties())
  {
    worldspawn.insert(
      QString::fromStdString(property.key()), QString::fromStdString(property.value()));
  }

  return QJsonObject{
    {"document", documentJson(*mapWindow, 0)},
    {"world", nodeSummaryJson(worldNode, worldNode)},
    {"worldspawn", worldspawn},
    {"entityCount", entities},
    {"brushCount", brushes},
    {"patchCount", patches},
    {"nodeCount", static_cast<int>(worldNode.descendantCount() + 1)},
    {"bounds", boundsToJson(worldNode.logicalBounds())},
    {"grid",
     QJsonObject{
       {"size", grid.size()},
       {"actualSize", grid.actualSize()},
       {"snap", grid.snap()},
       {"visible", grid.visible()},
     }},
  };
}

bool textMatches(const QString& text, const QString& query)
{
  return text.contains(query, Qt::CaseInsensitive);
}

bool nodeMatchesQuery(
  const mdl::Node& node, const mdl::WorldNode& worldNode, const QString& query)
{
  if (
    textMatches(nodePathId(node, worldNode), query)
    || textMatches(nodeTypeName(node), query)
    || textMatches(QString::fromStdString(node.name()), query))
  {
    return true;
  }

  if (const auto* entityNode = dynamic_cast<const mdl::EntityNodeBase*>(&node))
  {
    if (textMatches(QString::fromStdString(entityNode->entity().classname()), query))
    {
      return true;
    }

    for (const auto& property : entityNode->entity().properties())
    {
      if (
        textMatches(QString::fromStdString(property.key()), query)
        || textMatches(QString::fromStdString(property.value()), query))
      {
        return true;
      }
    }
  }

  return false;
}

void collectSearchResults(
  const mdl::Node& node,
  const mdl::WorldNode& worldNode,
  const QString& query,
  QJsonArray& results)
{
  if (nodeMatchesQuery(node, worldNode, query))
  {
    results.push_back(nodeSummaryJson(node, worldNode));
  }

  for (const auto* child : node.children())
  {
    collectSearchResults(*child, worldNode, query, results);
  }
}

QJsonObject operationRecordJson(const McpOperationRecord& operation)
{
  auto result = QJsonObject{};
  result.insert("operationId", operation.operationId);
  result.insert("toolName", operation.toolName);
  result.insert("transactionName", operation.transactionName);
  result.insert("changedObjectIds", operation.changedObjectIds);
  result.insert("undone", operation.undone);
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

QString makeOperationId(int& nextOperationIndex)
{
  return QString{"mcp-op-%1"}.arg(nextOperationIndex++);
}

QJsonObject mutationResultJson(const McpOperationRecord& operation)
{
  auto result = QJsonObject{};
  result.insert("operationId", operation.operationId);
  result.insert("transactionName", operation.transactionName);
  result.insert("changedObjectIds", operation.changedObjectIds);
  result.insert("changedObjectCount", operation.changedObjectIds.size());
  return result;
}

void recordOperation(
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
  operation.changedObjectIds = changedObjectIds;
  result = mutationResultJson(operation);
  history.push_back(std::move(operation));
}

std::optional<vm::vec3d> vec3FromJson(
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

std::optional<vm::bbox3d> boundsFromJson(const QJsonObject& params, QString& error)
{
  const auto min = vec3FromJson(params, "min", error);
  if (!min)
  {
    return std::nullopt;
  }
  const auto max = vec3FromJson(params, "max", error);
  if (!max)
  {
    return std::nullopt;
  }

  if (
    min->x() >= max->x() || min->y() >= max->y() || min->z() >= max->z()
    || !std::isfinite(min->x()) || !std::isfinite(min->y()) || !std::isfinite(min->z())
    || !std::isfinite(max->x()) || !std::isfinite(max->y()) || !std::isfinite(max->z()))
  {
    error = "min must be smaller than max on all axes";
    return std::nullopt;
  }

  return vm::bbox3d{*min, *max};
}

std::optional<vm::bbox3d> boundsFromJson(
  const QJsonObject& params, const QString& minKey, const QString& maxKey, QString& error)
{
  const auto min = vec3FromJson(params, minKey, error);
  if (!min)
  {
    return std::nullopt;
  }
  const auto max = vec3FromJson(params, maxKey, error);
  if (!max)
  {
    return std::nullopt;
  }

  if (
    min->x() >= max->x() || min->y() >= max->y() || min->z() >= max->z()
    || !std::isfinite(min->x()) || !std::isfinite(min->y()) || !std::isfinite(min->z())
    || !std::isfinite(max->x()) || !std::isfinite(max->y()) || !std::isfinite(max->z()))
  {
    error = QString{"%1 must be smaller than %2 on all axes"}.arg(minKey, maxKey);
    return std::nullopt;
  }

  return vm::bbox3d{*min, *max};
}

std::string optionalString(
  const QJsonObject& params, const QString& key, const std::string& defaultValue = {})
{
  const auto value = params.value(key);
  return value.isString() ? value.toString().toStdString() : defaultValue;
}

bool optionalBool(const QJsonObject& params, const QString& key, const bool defaultValue)
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

double optionalDouble(
  const QJsonObject& params, const QString& key, const double defaultValue)
{
  const auto value = params.value(key);
  return value.isDouble() ? value.toDouble(defaultValue) : defaultValue;
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

std::optional<vm::axis::type> axisFromJson(
  const QJsonObject& params,
  const QString& key,
  const vm::axis::type defaultValue,
  QString& error)
{
  const auto value = params.value(key);
  if (value.isUndefined())
  {
    return defaultValue;
  }
  if (!value.isString())
  {
    error = QString{"%1 must be x, y, or z"}.arg(key);
    return std::nullopt;
  }

  const auto axis = value.toString().trimmed().toLower();
  if (axis == "x")
  {
    return vm::axis::x;
  }
  if (axis == "y")
  {
    return vm::axis::y;
  }
  if (axis == "z")
  {
    return vm::axis::z;
  }

  error = QString{"%1 must be x, y, or z"}.arg(key);
  return std::nullopt;
}

std::string materialNameFromParams(mdl::Map& map, const QJsonObject& params)
{
  auto material = optionalString(params, "material");
  if (material.empty())
  {
    material = map.currentMaterialName();
  }
  if (material.empty())
  {
    material = "__TB_empty";
  }
  return material;
}

McpBridgeToolResult noActiveDocumentFailure()
{
  return McpBridgeToolResult::failure(
    mcp::McpErrorCode::NoActiveDocument, "No active document");
}

McpBridgeToolResult invalidParamsFailure(const QString& message)
{
  return McpBridgeToolResult::failure(mcp::McpErrorCode::InvalidParams, message);
}

template <typename Result>
QString resultErrorMessage(const Result& result)
{
  const auto error = result.error();
  return QString::fromStdString(std::get<Error>(error).msg);
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
    const auto originVec = vec3FromJson(params, "origin", error);
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
    map, transactionName, {entityNode}, optionalBool(params, "select", true));
  if (!changedObjectIds)
  {
    delete entityNode;
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not create entity");
  }

  auto result = QJsonObject{};
  recordOperation(
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
  recordOperation(
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
  recordOperation(
    history, nextOperationIndex, toolName, transactionName, *changedObjectIds, result);
  return McpBridgeToolResult::success(std::move(result));
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
      !query.isEmpty()
      && !textMatches(QString::fromStdString(definition.name), query)
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
  const auto* definition = map.entityDefinitionManager().definition(classname.toStdString());
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
    const auto parsedOrigin = vec3FromJson(params, "origin", error);
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
    map, transactionName, {entityNode}, optionalBool(params, "select", true));
  if (!changedObjectIds)
  {
    delete entityNode;
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError,
      QString{"Failed to create entity from schema: %1"}.arg(classname));
  }

  auto result = QJsonObject{};
  recordOperation(
    history,
    nextOperationIndex,
    toolName,
    transactionName,
    *changedObjectIds,
    result);
  result.insert("classname", classname);
  result.insert("entity", nodeSummaryJson(*entityNode, map.worldNode()));
  return McpBridgeToolResult::success(std::move(result));
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
  const auto* definition = map.entityDefinitionManager().definition(classname.toStdString());
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
  recordOperation(
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
  recordOperation(
    history,
    nextOperationIndex,
    toolName,
    "MCP: Untie brushes",
    changedObjectIds,
    result);
  return McpBridgeToolResult::success(std::move(result));
}

Result<mdl::Brush> createWedgeBrush(
  const mdl::BrushBuilder& builder,
  const vm::bbox3d& bounds,
  const vm::axis::type axis,
  const std::string& material)
{
  const auto& min = bounds.min;
  const auto& max = bounds.max;

  auto points = std::vector<vm::vec3d>{};
  switch (axis)
  {
  case vm::axis::x:
    points = {
      {min.x(), min.y(), min.z()},
      {min.x(), max.y(), min.z()},
      {min.x(), min.y(), max.z()},
      {min.x(), max.y(), max.z()},
      {max.x(), min.y(), min.z()},
      {max.x(), max.y(), min.z()},
    };
    break;
  case vm::axis::y:
    points = {
      {min.x(), min.y(), min.z()},
      {max.x(), min.y(), min.z()},
      {min.x(), min.y(), max.z()},
      {max.x(), min.y(), max.z()},
      {min.x(), max.y(), min.z()},
      {max.x(), max.y(), min.z()},
    };
    break;
  case vm::axis::z:
    points = {
      {min.x(), min.y(), min.z()},
      {max.x(), min.y(), min.z()},
      {min.x(), max.y(), min.z()},
      {max.x(), max.y(), min.z()},
      {min.x(), min.y(), max.z()},
      {max.x(), min.y(), max.z()},
    };
    break;
  }

  return builder.createBrush(points, material);
}

bool validThickness(const double thickness)
{
  return std::isfinite(thickness) && thickness > 0.0;
}

std::optional<std::vector<vm::bbox3d>> roomShellBounds(
  const vm::bbox3d& innerBounds, const double thickness, QString& error)
{
  if (!validThickness(thickness))
  {
    error = "thickness must be greater than zero";
    return std::nullopt;
  }

  const auto& min = innerBounds.min;
  const auto& max = innerBounds.max;
  auto result = std::vector<vm::bbox3d>{};
  result.reserve(6);
  result.emplace_back(
    vm::vec3d{min.x() - thickness, min.y() - thickness, min.z() - thickness},
    vm::vec3d{max.x() + thickness, max.y() + thickness, min.z()});
  result.emplace_back(
    vm::vec3d{min.x() - thickness, min.y() - thickness, max.z()},
    vm::vec3d{max.x() + thickness, max.y() + thickness, max.z() + thickness});
  result.emplace_back(
    vm::vec3d{min.x() - thickness, min.y() - thickness, min.z()},
    vm::vec3d{min.x(), max.y() + thickness, max.z()});
  result.emplace_back(
    vm::vec3d{max.x(), min.y() - thickness, min.z()},
    vm::vec3d{max.x() + thickness, max.y() + thickness, max.z()});
  result.emplace_back(
    vm::vec3d{min.x(), min.y() - thickness, min.z()},
    vm::vec3d{max.x(), min.y(), max.z()});
  result.emplace_back(
    vm::vec3d{min.x(), max.y(), min.z()},
    vm::vec3d{max.x(), max.y() + thickness, max.z()});
  return result;
}

std::optional<std::vector<vm::bbox3d>> doorwayBounds(
  const vm::bbox3d& wallBounds, const vm::bbox3d& doorBounds, QString& error)
{
  if (
    doorBounds.min.x() < wallBounds.min.x() || doorBounds.max.x() > wallBounds.max.x()
    || doorBounds.min.y() < wallBounds.min.y() || doorBounds.max.y() > wallBounds.max.y()
    || doorBounds.min.z() < wallBounds.min.z() || doorBounds.max.z() > wallBounds.max.z())
  {
    error = "door bounds must be inside wall bounds";
    return std::nullopt;
  }

  auto result = std::vector<vm::bbox3d>{};
  const auto addIfNonEmpty = [&](const vm::bbox3d& bounds) {
    if (
      bounds.min.x() < bounds.max.x() && bounds.min.y() < bounds.max.y()
      && bounds.min.z() < bounds.max.z())
    {
      result.push_back(bounds);
    }
  };

  const auto& min = wallBounds.min;
  const auto& max = wallBounds.max;
  const auto& dMin = doorBounds.min;
  const auto& dMax = doorBounds.max;

  const auto wallSize = max - min;
  const auto splitAlongX = wallSize.x() >= wallSize.y();
  if (splitAlongX)
  {
    addIfNonEmpty(vm::bbox3d{min, vm::vec3d{dMin.x(), max.y(), max.z()}});
    addIfNonEmpty(vm::bbox3d{vm::vec3d{dMax.x(), min.y(), min.z()}, max});
    addIfNonEmpty(vm::bbox3d{
      vm::vec3d{dMin.x(), min.y(), dMax.z()}, vm::vec3d{dMax.x(), max.y(), max.z()}});
  }
  else
  {
    addIfNonEmpty(vm::bbox3d{min, vm::vec3d{max.x(), dMin.y(), max.z()}});
    addIfNonEmpty(vm::bbox3d{vm::vec3d{min.x(), dMax.y(), min.z()}, max});
    addIfNonEmpty(vm::bbox3d{
      vm::vec3d{min.x(), dMin.y(), dMax.z()}, vm::vec3d{max.x(), dMax.y(), max.z()}});
  }

  if (result.empty())
  {
    error = "doorway did not produce any wall segments";
    return std::nullopt;
  }
  return result;
}

std::optional<std::vector<vm::bbox3d>> stairsBounds(
  const vm::bbox3d& bounds, const size_t steps, const vm::axis::type axis, QString& error)
{
  if (steps == 0)
  {
    error = "steps must be greater than zero";
    return std::nullopt;
  }
  if (axis != vm::axis::x && axis != vm::axis::y)
  {
    error = "stairs axis must be x or y";
    return std::nullopt;
  }

  const auto& min = bounds.min;
  const auto& max = bounds.max;
  const auto runMin = axis == vm::axis::x ? min.x() : min.y();
  const auto runMax = axis == vm::axis::x ? max.x() : max.y();
  const auto runStep = (runMax - runMin) / static_cast<double>(steps);
  const auto riseStep = (max.z() - min.z()) / static_cast<double>(steps);

  auto result = std::vector<vm::bbox3d>{};
  result.reserve(steps);
  for (size_t i = 0; i < steps; ++i)
  {
    const auto stepMinRun = runMin + runStep * static_cast<double>(i);
    const auto stepMaxRun = runMin + runStep * static_cast<double>(i + 1);
    const auto stepMaxZ = min.z() + riseStep * static_cast<double>(i + 1);
    if (axis == vm::axis::x)
    {
      result.emplace_back(
        vm::vec3d{stepMinRun, min.y(), min.z()},
        vm::vec3d{stepMaxRun, max.y(), stepMaxZ});
    }
    else
    {
      result.emplace_back(
        vm::vec3d{min.x(), stepMinRun, min.z()},
        vm::vec3d{max.x(), stepMaxRun, stepMaxZ});
    }
  }
  return result;
}

std::vector<vm::bbox3d> skyShellBounds(
  const vm::bbox3d& innerBounds, const double thickness)
{
  auto error = QString{};
  return roomShellBounds(innerBounds, thickness, error)
    .value_or(std::vector<vm::bbox3d>{});
}

std::optional<QJsonObject> validateBlockoutParams(
  const QString& type, const QJsonObject& params, QString& error)
{
  const auto bounds = boundsFromJson(params, error);
  if (!bounds)
  {
    return std::nullopt;
  }

  if (type == "room" || type == "corridor" || type == "sky_shell")
  {
    const auto thickness = optionalDouble(params, "thickness", 16.0);
    if (!validThickness(thickness))
    {
      error = "thickness must be greater than zero";
      return std::nullopt;
    }
  }
  else if (type == "stairs")
  {
    if (optionalSize(params, "steps", 8) == 0)
    {
      error = "steps must be greater than zero";
      return std::nullopt;
    }
  }
  else if (type == "doorway")
  {
    const auto door = boundsFromJson(params, "doorMin", "doorMax", error);
    if (!door)
    {
      return std::nullopt;
    }
    if (!doorwayBounds(*bounds, *door, error))
    {
      return std::nullopt;
    }
  }
  else if (type != "ramp" && type != "cover")
  {
    error = "Unknown blockout type";
    return std::nullopt;
  }

  return QJsonObject{
    {"valid", true},
    {"type", type},
    {"bounds", boundsToJson(*bounds)},
  };
}

std::vector<mdl::Node*> brushNodesFromBounds(
  const mdl::BrushBuilder& builder,
  const std::vector<vm::bbox3d>& boundsList,
  const std::string& material,
  QString& error)
{
  auto result = std::vector<mdl::Node*>{};
  result.reserve(boundsList.size());
  for (const auto& bounds : boundsList)
  {
    auto brush = builder.createCuboid(bounds, material);
    if (brush.is_error())
    {
      for (auto* node : result)
      {
        delete node;
      }
      error = "Could not create one or more blockout brushes";
      return {};
    }
    result.push_back(new mdl::BrushNode{std::move(brush.value())});
  }
  return result;
}

McpBridgeToolResult blockoutValidateResult(const QJsonObject& params)
{
  const auto type = params.value("type").toString().trimmed();
  if (type.isEmpty())
  {
    return invalidParamsFailure("blockout_validate requires type");
  }

  auto error = QString{};
  const auto result = validateBlockoutParams(type, params, error);
  if (!result)
  {
    return McpBridgeToolResult::success(QJsonObject{
      {"valid", false},
      {"type", type},
      {"errors", QJsonArray{error}},
    });
  }
  return McpBridgeToolResult::success(*result);
}

McpBridgeToolResult blockoutCreateResult(
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

  auto error = QString{};
  const auto bounds = boundsFromJson(params, error);
  if (!bounds)
  {
    return invalidParamsFailure(error);
  }

  auto& map = mapWindow->document().map();
  const auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
  auto material = materialNameFromParams(map, params);
  auto nodes = std::vector<mdl::Node*>{};
  auto transactionName = QString{};

  if (toolName == "blockout_create_room" || toolName == "blockout_create_corridor")
  {
    const auto shellBounds =
      roomShellBounds(*bounds, optionalDouble(params, "thickness", 16.0), error);
    if (!shellBounds)
    {
      return invalidParamsFailure(error);
    }
    nodes = brushNodesFromBounds(builder, *shellBounds, material, error);
    transactionName = toolName == "blockout_create_room" ? "MCP: Blockout room"
                                                         : "MCP: Blockout corridor";
  }
  else if (toolName == "blockout_create_stairs")
  {
    const auto axis = axisFromJson(params, "axis", vm::axis::x, error);
    if (!axis)
    {
      return invalidParamsFailure(error);
    }
    const auto stairBoxes =
      stairsBounds(*bounds, optionalSize(params, "steps", 8), *axis, error);
    if (!stairBoxes)
    {
      return invalidParamsFailure(error);
    }
    nodes = brushNodesFromBounds(builder, *stairBoxes, material, error);
    transactionName = "MCP: Blockout stairs";
  }
  else if (toolName == "blockout_create_ramp")
  {
    const auto axis = axisFromJson(params, "axis", vm::axis::x, error);
    if (!axis)
    {
      return invalidParamsFailure(error);
    }
    auto brush = createWedgeBrush(builder, *bounds, *axis, material);
    if (brush.is_error())
    {
      return invalidParamsFailure("Could not create ramp brush");
    }
    nodes.push_back(new mdl::BrushNode{std::move(brush.value())});
    transactionName = "MCP: Blockout ramp";
  }
  else if (toolName == "blockout_create_doorway")
  {
    const auto door = boundsFromJson(params, "doorMin", "doorMax", error);
    if (!door)
    {
      return invalidParamsFailure(error);
    }
    const auto wallSegments = doorwayBounds(*bounds, *door, error);
    if (!wallSegments)
    {
      return invalidParamsFailure(error);
    }
    nodes = brushNodesFromBounds(builder, *wallSegments, material, error);
    transactionName = "MCP: Blockout doorway";
  }
  else if (toolName == "blockout_create_cover")
  {
    nodes = brushNodesFromBounds(builder, {*bounds}, material, error);
    transactionName = "MCP: Blockout cover";
  }
  else if (toolName == "blockout_create_sky_shell")
  {
    material = optionalString(params, "material", "sky");
    const auto shellBounds =
      skyShellBounds(*bounds, optionalDouble(params, "thickness", 16.0));
    if (shellBounds.empty())
    {
      return invalidParamsFailure("Could not create sky shell bounds");
    }
    nodes = brushNodesFromBounds(builder, shellBounds, material, error);
    transactionName = "MCP: Blockout sky shell";
  }

  if (nodes.empty())
  {
    return invalidParamsFailure(
      error.isEmpty() ? "No blockout brushes were generated" : error);
  }

  const auto changedObjectIds = addNodesWithTransaction(
    map, transactionName, nodes, optionalBool(params, "select", true));
  if (!changedObjectIds)
  {
    for (auto* node : nodes)
    {
      delete node;
    }
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not add blockout brushes");
  }

  auto result = QJsonObject{};
  recordOperation(
    history, nextOperationIndex, toolName, transactionName, *changedObjectIds, result);
  result.insert("brushCount", static_cast<int>(nodes.size()));
  result.insert("material", QString::fromStdString(material));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult createBrushResult(
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

  auto error = QString{};
  const auto bounds = boundsFromJson(params, error);
  if (!bounds)
  {
    return invalidParamsFailure(error);
  }

  auto& map = mapWindow->document().map();
  const auto material = materialNameFromParams(map, params);
  const auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};

  auto brush = Result<mdl::Brush>{Error{"Unsupported brush tool"}};
  auto transactionName = QString{};
  if (toolName == "brush_create_box")
  {
    brush = builder.createCuboid(*bounds, material);
    transactionName = "MCP: Create box brush";
  }
  else if (toolName == "brush_create_wedge")
  {
    const auto axis = axisFromJson(params, "axis", vm::axis::x, error);
    if (!axis)
    {
      return invalidParamsFailure(error);
    }
    brush = createWedgeBrush(builder, *bounds, *axis, material);
    transactionName = "MCP: Create wedge brush";
  }
  else if (toolName == "brush_create_cylinder")
  {
    const auto axis = axisFromJson(params, "axis", vm::axis::z, error);
    if (!axis)
    {
      return invalidParamsFailure(error);
    }
    const auto sides = std::max<size_t>(3, optionalSize(params, "sides", 16));
    brush =
      builder.createCylinder(*bounds, mdl::EdgeAlignedCircle{sides}, *axis, material);
    transactionName = "MCP: Create cylinder brush";
  }

  if (brush.is_error())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams, "Could not create brush from the given bounds");
  }

  auto* brushNode = new mdl::BrushNode{std::move(brush.value())};
  const auto changedObjectIds = addNodesWithTransaction(
    map, transactionName, {brushNode}, optionalBool(params, "select", true));
  if (!changedObjectIds)
  {
    delete brushNode;
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not add brush to the active document");
  }

  auto result = QJsonObject{};
  recordOperation(
    history, nextOperationIndex, toolName, transactionName, *changedObjectIds, result);
  result.insert("brush", nodeSummaryJson(*brushNode, map.worldNode()));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult historyListResult(const std::vector<McpOperationRecord>& history)
{
  return McpBridgeToolResult::success(QJsonObject{
    {"operations", operationHistoryJson(history)},
    {"count", static_cast<int>(history.size())},
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

std::optional<std::vector<BrowserAsset>> collectMcpAssets(mdl::Map& map)
{
  const auto enabledMods = mdl::enabledMods(map);
  if (enabledMods.empty())
  {
    return std::vector<BrowserAsset>{};
  }

  auto modRoots = std::vector<std::filesystem::path>{};
  modRoots.reserve(enabledMods.size());
  for (const auto& mod : enabledMods)
  {
    modRoots.push_back((map.gamePath() / std::filesystem::path{mod}).lexically_normal());
  }

  const auto& fs = map.gameFileSystem();
  return collectBrowserAssets(
    {},
    modRoots,
    [&](const auto& rootPath) {
      return fs.find(
        rootPath,
        fs::TraversalMode::Recursive,
        fs::makeExtensionPathMatcher(goldSrcAssetExtensions()));
    },
    [&](const auto& path) { return fs.makeAbsolute(path); });
}

QJsonObject assetJson(const BrowserAsset& asset)
{
  return QJsonObject{
    {"type", browserCellTypeName(asset.type)},
    {"path", genericPathToQString(asset.path)},
    {"absolutePath", pathToQString(asset.absolutePath)},
    {"displayName", QString::fromStdString(asset.displayName)},
  };
}

McpBridgeToolResult assetSearchResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto& map = mapWindow->document().map();
  const auto assets = collectMcpAssets(map);
  if (!assets)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not scan assets");
  }

  const auto query = params.value("query").toString().trimmed();
  const auto typeText = params.value("type").toString().trimmed();
  const auto type = typeText.isEmpty() ? std::optional<BrowserCellType>{}
                                       : browserCellTypeFromString(typeText);
  if (!typeText.isEmpty() && !type)
  {
    return invalidParamsFailure("type must be model, sprite, or sound");
  }

  const auto limit = std::max(1, params.value("limit").toInt(50));
  auto results = QJsonArray{};
  for (const auto& asset : *assets)
  {
    if (type && asset.type != *type)
    {
      continue;
    }

    const auto pathText = genericPathToQString(asset.path);
    const auto displayName = QString::fromStdString(asset.displayName);
    if (
      !query.isEmpty() && !pathText.contains(query, Qt::CaseInsensitive)
      && !displayName.contains(query, Qt::CaseInsensitive))
    {
      continue;
    }

    results.push_back(assetJson(asset));
    if (results.size() >= limit)
    {
      break;
    }
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"query", query},
    {"type", type ? browserCellTypeName(*type) : QString{}},
    {"results", results},
    {"count", results.size()},
  });
}

McpBridgeToolResult placeAssetResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  const auto path = params.value("path").toString().trimmed();
  if (path.isEmpty())
  {
    return invalidParamsFailure(QString{"%1 requires path"}.arg(toolName));
  }

  auto assetType = BrowserCellType::Model;
  auto defaultClassname = std::string{"cycler_sprite"};
  auto defaultProperty = std::string{"model"};
  auto transactionLabel = QString{"model"};
  if (toolName == "asset_place_sprite")
  {
    assetType = BrowserCellType::Sprite;
    defaultClassname = "cycler_sprite";
    defaultProperty = "model";
    transactionLabel = "sprite";
  }
  else if (toolName == "asset_place_sound")
  {
    assetType = BrowserCellType::Sound;
    defaultClassname = "ambient_generic";
    defaultProperty = "message";
    transactionLabel = "sound";
  }

  const auto actualType =
    assetTypeForExtension(std::filesystem::path{path.toStdString()});
  if (actualType != assetType)
  {
    return invalidParamsFailure(
      QString{"path does not match %1 asset type"}.arg(browserCellTypeName(assetType)));
  }

  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto error = QString{};
  auto entity = mdl::Entity{
    {{mdl::EntityPropertyKeys::Classname,
      optionalString(params, "classname", defaultClassname)}}};
  entity.addOrUpdateProperty(
    optionalString(params, "property", defaultProperty), path.toStdString());

  if (const auto origin = params.value("origin"); !origin.isUndefined())
  {
    const auto originVec = vec3FromJson(params, "origin", error);
    if (!originVec)
    {
      return invalidParamsFailure(error);
    }
    entity.setOrigin(*originVec);
  }

  auto& map = mapWindow->document().map();
  auto* entityNode = new mdl::EntityNode{std::move(entity)};
  const auto transactionName = QString{"MCP: Place %1 asset"}.arg(transactionLabel);
  const auto changedObjectIds = addNodesWithTransaction(
    map, transactionName, {entityNode}, optionalBool(params, "select", true));
  if (!changedObjectIds)
  {
    delete entityNode;
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not place asset entity");
  }

  auto result = QJsonObject{};
  recordOperation(
    history, nextOperationIndex, toolName, transactionName, *changedObjectIds, result);
  result.insert("entity", nodeSummaryJson(*entityNode, map.worldNode()));
  result.insert("assetPath", path);
  return McpBridgeToolResult::success(std::move(result));
}

QJsonObject materialJson(const gl::Material& material)
{
  return QJsonObject{
    {"name", QString::fromStdString(material.name())},
    {"collection", QString::fromStdString(material.collectionName())},
    {"relativePath", genericPathToQString(material.relativePath())},
    {"absolutePath", pathToQString(material.absolutePath())},
    {"usageCount", static_cast<int>(material.usageCount())},
  };
}

McpBridgeToolResult textureSearchResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  const auto query = params.value("query").toString().trimmed();
  const auto limit = std::max(1, params.value("limit").toInt(50));
  auto results = QJsonArray{};

  const auto& materials = mapWindow->document().map().materialManager().materials();
  for (const auto* material : materials)
  {
    if (!material)
    {
      continue;
    }
    const auto name = QString::fromStdString(material->name());
    const auto relativePath = genericPathToQString(material->relativePath());
    if (
      !query.isEmpty() && !name.contains(query, Qt::CaseInsensitive)
      && !relativePath.contains(query, Qt::CaseInsensitive))
    {
      continue;
    }

    results.push_back(materialJson(*material));
    if (results.size() >= limit)
    {
      break;
    }
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"query", query},
    {"results", results},
    {"count", results.size()},
  });
}

std::vector<mdl::BrushFaceHandle> brushFaceHandlesForTextureApply(
  mdl::Map& map, const QJsonObject& params, QString& error)
{
  auto result = std::vector<mdl::BrushFaceHandle>{};
  const auto objectId = params.value("objectId").toString().trimmed();
  if (!objectId.isEmpty())
  {
    auto* node = resolveNodeId(map.worldNode(), objectId);
    auto* brushNode = dynamic_cast<mdl::BrushNode*>(node);
    if (!brushNode)
    {
      error = QString{"objectId is not a brush: %1"}.arg(objectId);
      return {};
    }

    const auto faceIndexValue = params.value("faceIndex");
    if (faceIndexValue.isUndefined())
    {
      return mdl::toHandles(brushNode);
    }

    if (!faceIndexValue.isDouble())
    {
      error = "faceIndex must be an integer";
      return {};
    }
    const auto faceIndex = static_cast<size_t>(faceIndexValue.toInt());
    if (faceIndex >= brushNode->brush().faceCount())
    {
      error = "faceIndex is out of range";
      return {};
    }
    result.emplace_back(brushNode, faceIndex);
    return result;
  }

  if (!map.selection().brushFaces.empty())
  {
    return map.selection().brushFaces;
  }

  for (auto* brushNode : map.selection().brushes)
  {
    const auto handles = mdl::toHandles(brushNode);
    result.insert(std::end(result), std::begin(handles), std::end(handles));
  }

  if (result.empty())
  {
    error = "texture_apply requires objectId or selected brush faces/brushes";
  }
  return result;
}

McpBridgeToolResult textureApplyResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  const auto material = params.value("material").toString().trimmed();
  if (material.isEmpty())
  {
    return invalidParamsFailure("texture_apply requires material");
  }

  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto& map = mapWindow->document().map();
  auto error = QString{};
  auto handles = brushFaceHandlesForTextureApply(map, params, error);
  if (handles.empty())
  {
    return invalidParamsFailure(error);
  }

  auto changedNodes = QJsonArray{};
  for (const auto* node : mdl::toNodes(handles))
  {
    changedNodes.push_back(nodePathId(*node, map.worldNode()));
  }

  const auto transactionName = QString{"MCP: Apply texture"};
  auto ok = executeTransaction(map, transactionName, [&]() {
    mdl::deselectAll(map);
    mdl::selectBrushFaces(map, handles);
    return mdl::setBrushFaceAttributes(
      map, mdl::UpdateBrushFaceAttributes{.materialName = material.toStdString()});
  });
  if (!ok)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not apply texture");
  }

  auto result = QJsonObject{};
  recordOperation(
    history, nextOperationIndex, toolName, transactionName, changedNodes, result);
  result.insert("material", material);
  result.insert("faceCount", static_cast<int>(handles.size()));
  return McpBridgeToolResult::success(std::move(result));
}

QJsonObject mapSearchJson(AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  const auto query = params.value("query").toString().trimmed();

  if (!mapWindow || query.isEmpty())
  {
    return QJsonObject{
      {"query", query},
      {"results", QJsonArray{}},
      {"count", 0},
    };
  }

  const auto& worldNode = mapWindow->document().map().worldNode();
  auto results = QJsonArray{};
  collectSearchResults(worldNode, worldNode, query, results);

  return QJsonObject{
    {"query", query},
    {"results", results},
    {"count", results.size()},
  };
}

QJsonObject selectionJson(AppController& appController)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return {};
  }

  const auto& map = mapWindow->document().map();
  const auto& worldNode = map.worldNode();
  const auto& selection = map.selection();

  auto nodes = QJsonArray{};
  for (const auto* node : selection.nodes)
  {
    nodes.push_back(nodeSummaryJson(*node, worldNode));
  }

  return QJsonObject{
    {"hasSelection", selection.hasAny()},
    {"nodes", nodes},
    {"nodeCount", static_cast<int>(selection.nodes.size())},
    {"groupCount", static_cast<int>(selection.groups.size())},
    {"entityCount", static_cast<int>(selection.entities.size())},
    {"brushCount", static_cast<int>(selection.brushes.size())},
    {"patchCount", static_cast<int>(selection.patches.size())},
    {"brushFaceCount", static_cast<int>(selection.brushFaces.size())},
  };
}

McpBridgeToolResult selectionSetResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::NoActiveDocument, "No active document");
  }

  const auto objectIdsValue = params.value("objectIds");
  if (!objectIdsValue.isArray())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams, "selection_set requires objectIds array");
  }

  auto& map = mapWindow->document().map();
  auto& worldNode = map.worldNode();
  auto nodes = std::vector<mdl::Node*>{};

  for (const auto& objectIdValue : objectIdsValue.toArray())
  {
    if (!objectIdValue.isString())
    {
      return McpBridgeToolResult::failure(
        mcp::McpErrorCode::InvalidParams, "objectIds must contain only strings");
    }

    const auto objectId = objectIdValue.toString();
    auto* node = resolveNodeId(worldNode, objectId);
    if (!node)
    {
      return McpBridgeToolResult::failure(
        mcp::McpErrorCode::InvalidParams,
        QString{"Unknown MCP object id: %1"}.arg(objectId));
    }
    if (!map.editorContext().selectable(*node))
    {
      return McpBridgeToolResult::failure(
        mcp::McpErrorCode::InvalidParams,
        QString{"MCP object id is not selectable: %1"}.arg(objectId));
    }
    nodes.push_back(node);
  }

  mdl::deselectAll(map);
  if (!nodes.empty())
  {
    mdl::selectNodes(map, nodes);
  }

  auto selectedIds = QJsonArray{};
  for (const auto* node : nodes)
  {
    selectedIds.push_back(nodePathId(*node, worldNode));
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"selectedObjectIds", selectedIds},
    {"selectedCount", selectedIds.size()},
  });
}

bool boundsMatch(
  const vm::bbox3d& candidate, const vm::bbox3d& queryBounds, const QString& mode)
{
  if (mode.compare("contains", Qt::CaseInsensitive) == 0)
  {
    return queryBounds.contains(candidate);
  }
  return queryBounds.intersects(candidate);
}

bool materialMatches(const mdl::Node& node, const QString& material)
{
  if (material.isEmpty())
  {
    return true;
  }

  const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(&node);
  if (!brushNode)
  {
    return false;
  }

  const auto materialName = material.toStdString();
  for (const auto& face : brushNode->brush().faces())
  {
    if (face.attributes().materialName() == materialName)
    {
      return true;
    }
  }
  return false;
}

bool entityPropertyMatches(
  const mdl::Node& node,
  const QString& classname,
  const QString& targetname)
{
  const auto* entityNode = dynamic_cast<const mdl::EntityNodeBase*>(&node);
  if (!entityNode)
  {
    return classname.isEmpty() && targetname.isEmpty();
  }

  if (
    !classname.isEmpty()
    && !textMatches(QString::fromStdString(entityNode->entity().classname()), classname))
  {
    return false;
  }

  if (!targetname.isEmpty())
  {
    const auto targetnameProperty = entityNode->entity().property("targetname");
    const auto targetnameValue =
      targetnameProperty ? QString::fromStdString(*targetnameProperty) : QString{};
    if (!textMatches(targetnameValue, targetname))
    {
      return false;
    }
  }

  return true;
}

bool nodeFilterMatches(
  const mdl::Node& node,
  const mdl::WorldNode& worldNode,
  const QJsonObject& params,
  const std::optional<vm::bbox3d>& queryBounds)
{
  const auto type = params.value("type").toString().trimmed();
  if (!type.isEmpty() && nodeTypeName(node).compare(type, Qt::CaseInsensitive) != 0)
  {
    return false;
  }

  if (
    !entityPropertyMatches(
      node,
      params.value("classname").toString().trimmed(),
      params.value("targetname").toString().trimmed()))
  {
    return false;
  }

  if (!materialMatches(node, params.value("material").toString().trimmed()))
  {
    return false;
  }

  if (const auto query = params.value("query").toString().trimmed();
      !query.isEmpty() && !nodeMatchesQuery(node, worldNode, query))
  {
    return false;
  }

  if (queryBounds)
  {
    const auto mode = params.value("boundsMode").toString("intersects");
    if (!boundsMatch(node.logicalBounds(), *queryBounds, mode))
    {
      return false;
    }
  }

  return true;
}

void collectFilteredNodes(
  const mdl::Node& node,
  const mdl::WorldNode& worldNode,
  const QJsonObject& params,
  const std::optional<vm::bbox3d>& queryBounds,
  const size_t limit,
  std::vector<mdl::Node*>& matches)
{
  if (matches.size() >= limit)
  {
    return;
  }

  if (nodeFilterMatches(node, worldNode, params, queryBounds))
  {
    matches.push_back(const_cast<mdl::Node*>(&node));
  }

  for (const auto* child : node.children())
  {
    collectFilteredNodes(*child, worldNode, params, queryBounds, limit, matches);
    if (matches.size() >= limit)
    {
      return;
    }
  }
}

McpBridgeToolResult selectionFilterResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto error = QString{};
  auto queryBounds = std::optional<vm::bbox3d>{};
  const auto hasMin = !params.value("min").isUndefined();
  const auto hasMax = !params.value("max").isUndefined();
  if (hasMin != hasMax)
  {
    return invalidParamsFailure("min and max must be provided together");
  }
  if (hasMin && hasMax)
  {
    queryBounds = boundsFromJson(params, error);
    if (!queryBounds)
    {
      return invalidParamsFailure(error);
    }
  }

  auto& map = mapWindow->document().map();
  auto& worldNode = map.worldNode();
  auto matches = std::vector<mdl::Node*>{};
  collectFilteredNodes(
    worldNode, worldNode, params, queryBounds, optionalSize(params, "limit", 100), matches);

  if (optionalBool(params, "select", false))
  {
    auto selectableNodes = std::vector<mdl::Node*>{};
    for (auto* node : matches)
    {
      if (&worldNode != node && map.editorContext().selectable(*node))
      {
        selectableNodes.push_back(node);
      }
    }
    mdl::deselectAll(map);
    if (!selectableNodes.empty())
    {
      mdl::selectNodes(map, selectableNodes);
    }
  }

  auto results = QJsonArray{};
  for (const auto* node : matches)
  {
    results.push_back(nodeSummaryJson(*node, worldNode));
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"results", results},
    {"count", results.size()},
  });
}

McpBridgeToolResult selectionByBoundsResult(
  AppController& appController, const QJsonObject& params)
{
  auto paramsWithSelect = params;
  paramsWithSelect.insert("select", true);
  paramsWithSelect.insert("boundsMode", params.value("mode").toString("intersects"));
  return selectionFilterResult(appController, paramsWithSelect);
}

McpBridgeToolResult selectionGrowResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto& map = mapWindow->document().map();
  const auto selectedNodes = map.selection().nodes;
  if (selectedNodes.empty())
  {
    return McpBridgeToolResult::success(QJsonObject{
      {"selectedObjectIds", QJsonArray{}},
      {"selectedCount", 0},
    });
  }

  const auto mode = params.value("mode").toString("parents").trimmed().toLower();
  auto grown = std::vector<mdl::Node*>{};
  auto seen = std::set<mdl::Node*>{};
  const auto addNode = [&](mdl::Node* node) {
    if (node && node != &map.worldNode() && map.editorContext().selectable(*node))
    {
      if (seen.insert(node).second)
      {
        grown.push_back(node);
      }
    }
  };

  if (mode == "parents")
  {
    for (auto* node : selectedNodes)
    {
      addNode(node->parent());
    }
  }
  else if (mode == "children")
  {
    for (auto* node : selectedNodes)
    {
      for (auto* child : node->children())
      {
        addNode(child);
      }
    }
  }
  else if (mode == "siblings")
  {
    for (auto* node : selectedNodes)
    {
      if (auto* parent = node->parent())
      {
        for (auto* sibling : parent->children())
        {
          addNode(sibling);
        }
      }
    }
  }
  else
  {
    return invalidParamsFailure("selection_grow mode must be parents, children, or siblings");
  }

  mdl::deselectAll(map);
  if (!grown.empty())
  {
    mdl::selectNodes(map, grown);
  }

  auto selectedIds = QJsonArray{};
  for (const auto* node : grown)
  {
    selectedIds.push_back(nodePathId(*node, map.worldNode()));
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"mode", mode},
    {"selectedObjectIds", selectedIds},
    {"selectedCount", selectedIds.size()},
  });
}

McpBridgeToolResult viewportFocusResult(
  AppController& appController, const QJsonObject& params)
{
  if (const auto objectIds = params.value("objectIds"); objectIds.isArray())
  {
    const auto selectionResult = selectionSetResult(
      appController, QJsonObject{{"objectIds", objectIds.toArray()}});
    if (!selectionResult.ok)
    {
      return selectionResult;
    }
  }

  const auto& actionsMap = appController.actionManager().actionsMap();
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  auto context = ActionExecutionContext{appController, mapWindow, nullptr};
  const auto actionPath = std::filesystem::path{"Menu/View/Focus on Selection"};
  const auto actionIt = actionsMap.find(actionPath);
  if (actionIt != std::end(actionsMap) && actionIt->second.enabled(context))
  {
    actionIt->second.execute(context);
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"focused", true},
    {"selection", selectionJson(appController)},
  });
}

McpBridgeToolResult viewportClearMarksResult(
  AppController& appController, const QJsonObject& params, QJsonObject& overlayState)
{
  overlayState = QJsonObject{};

  if (optionalBool(params, "clearSelection", false))
  {
    auto* mapWindow = appController.mapWindowManager().topMapWindow();
    if (!mapWindow)
    {
      return noActiveDocumentFailure();
    }
    mdl::deselectAll(mapWindow->document().map());
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"overlay", overlayState},
    {"active", false},
    {"selectionCleared", optionalBool(params, "clearSelection", false)},
  });
}

QJsonObject actionsListJson(AppController& appController)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  auto context = ActionExecutionContext{appController, mapWindow, nullptr};

  auto actions = QJsonArray{};
  for (const auto& [path, action] : appController.actionManager().actionsMap())
  {
    const auto enabled = action.enabled(context);
    auto actionJson = QJsonObject{
      {"id", pathAsGenericQString(path)},
      {"label", action.label()},
      {"enabled", enabled},
      {"menuAction", action.isMenuAction()},
      {"checkable", action.checkable()},
    };

    if (action.checkable())
    {
      actionJson.insert("checked", action.checked(context));
    }

    actions.push_back(actionJson);
  }

  return QJsonObject{
    {"actions", actions},
    {"count", actions.size()},
  };
}

McpBridgeToolResult actionExecuteResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  const auto actionId = params.value("actionId").toString().trimmed();

  if (actionId.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams, "action_execute requires actionId");
  }

  const auto actionPath = pathFromQString(actionId);
  const auto& actionsMap = appController.actionManager().actionsMap();
  const auto actionIt = actionsMap.find(actionPath);
  if (actionIt == std::end(actionsMap))
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams, QString{"Unknown action id: %1"}.arg(actionId));
  }

  auto context = ActionExecutionContext{appController, mapWindow, nullptr};
  const auto& action = actionIt->second;
  if (!action.enabled(context))
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden, QString{"Action is disabled: %1"}.arg(actionId));
  }

  action.execute(context);
  return McpBridgeToolResult::success(QJsonObject{
    {"actionId", actionId},
    {"executed", true},
  });
}

QJsonObject documentsListJson(AppController& appController)
{
  auto documents = QJsonArray{};
  auto index = 0;
  for (const auto* mapWindow : appController.mapWindowManager().mapWindows())
  {
    documents.push_back(documentJson(*mapWindow, index));
    ++index;
  }

  return QJsonObject{
    {"documents", documents},
    {"count", documents.size()},
  };
}

std::filesystem::path absolutePathFromParams(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto pathString = params.value(key).toString().trimmed();
  if (pathString.isEmpty())
  {
    error = QString{"%1 is required"}.arg(key);
    return {};
  }

  auto path = pathFromQString(pathString);
  if (path.empty() || !path.is_absolute())
  {
    error = QString{"%1 must be an absolute path"}.arg(key);
    return {};
  }

  return path.lexically_normal();
}

MapWindow* documentWindowByParams(
  AppController& appController, const QJsonObject& params, QString& error)
{
  const auto windows = appController.mapWindowManager().mapWindows();
  if (windows.empty())
  {
    error = "No active document";
    return nullptr;
  }

  const auto indexValue = params.value("index");
  if (indexValue.isDouble())
  {
    const auto index = indexValue.toInt(-1);
    if (index < 0 || index >= static_cast<int>(windows.size()))
    {
      error = QString{"Unknown document index: %1"}.arg(index);
      return nullptr;
    }
    return windows[static_cast<size_t>(index)];
  }

  const auto pathValue = params.value("path");
  if (pathValue.isString())
  {
    const auto path = pathFromQString(pathValue.toString()).lexically_normal();
    for (auto* window : windows)
    {
      if (window->document().map().path().lexically_normal() == path)
      {
        return window;
      }
    }
    error = QString{"No open document for path: %1"}.arg(pathValue.toString());
    return nullptr;
  }

  return appController.mapWindowManager().topMapWindow();
}

McpBridgeToolResult documentOpenResult(
  AppController& appController, const QJsonObject& params)
{
  auto error = QString{};
  const auto path = absolutePathFromParams(params, "path", error);
  if (!error.isEmpty())
  {
    return invalidParamsFailure(error);
  }
  if (!std::filesystem::is_regular_file(path))
  {
    return invalidParamsFailure(QString{"Document does not exist: %1"}.arg(pathToQString(path)));
  }

  if (!appController.openDocument(path))
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError,
      QString{"Failed to open document: %1"}.arg(pathToQString(path)));
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"opened", true},
    {"document", activeDocumentJson(appController)},
  });
}

McpBridgeToolResult documentActivateResult(
  AppController& appController, const QJsonObject& params)
{
  auto error = QString{};
  auto* mapWindow = documentWindowByParams(appController, params, error);
  if (!mapWindow)
  {
    return error == "No active document" ? noActiveDocumentFailure()
                                         : invalidParamsFailure(error);
  }

  mapWindow->show();
  mapWindow->raise();
  mapWindow->activateWindow();

  return McpBridgeToolResult::success(QJsonObject{
    {"activated", true},
    {"document", documentJson(*mapWindow, 0)},
  });
}

McpBridgeToolResult documentSaveResult(
  AppController& appController, const QJsonObject& params)
{
  auto error = QString{};
  auto* mapWindow = documentWindowByParams(appController, params, error);
  if (!mapWindow)
  {
    return error == "No active document" ? noActiveDocumentFailure()
                                         : invalidParamsFailure(error);
  }

  auto& map = mapWindow->document().map();
  const auto pathValue = params.value("path");
  const auto savePath =
    pathValue.isString() && !pathValue.toString().trimmed().isEmpty()
      ? absolutePathFromParams(params, "path", error)
      : std::filesystem::path{};
  if (!error.isEmpty())
  {
    return invalidParamsFailure(error);
  }

  if (savePath.empty() && !map.persistent())
  {
    return invalidParamsFailure("Transient documents require absolute path");
  }

  const auto result = savePath.empty() ? map.save() : map.saveAs(savePath);
  if (!result)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, resultErrorMessage(result));
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"saved", true},
    {"path", pathToQString(map.path())},
    {"document", documentJson(*mapWindow, 0)},
  });
}

McpBridgeToolResult documentCloseResult(
  AppController& appController, const QJsonObject& params)
{
  auto error = QString{};
  auto* mapWindow = documentWindowByParams(appController, params, error);
  if (!mapWindow)
  {
    return error == "No active document" ? noActiveDocumentFailure()
                                         : invalidParamsFailure(error);
  }

  if (mapWindow->document().map().modified() && !optionalBool(params, "discardChanges", false))
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden,
      "Document has unsaved changes; pass discardChanges=true to close it");
  }

  const auto document = documentJson(*mapWindow, 0);
  mapWindow->closeDocument(optionalBool(params, "discardChanges", false));

  return McpBridgeToolResult::success(QJsonObject{
    {"closed", true},
    {"document", document},
  });
}

McpBridgeToolResult documentExportResult(
  AppController& appController, const QJsonObject& params)
{
  auto error = QString{};
  auto* mapWindow = documentWindowByParams(appController, params, error);
  if (!mapWindow)
  {
    return error == "No active document" ? noActiveDocumentFailure()
                                         : invalidParamsFailure(error);
  }

  const auto exportPath = absolutePathFromParams(params, "path", error);
  if (!error.isEmpty())
  {
    return invalidParamsFailure(error);
  }
  if (exportPath == mapWindow->document().map().path())
  {
    return invalidParamsFailure("Export path must not overwrite the current document");
  }

  const auto stripTbProperties = optionalBool(params, "stripTbProperties", true);
  const auto options = mdl::MapExportOptions{exportPath, stripTbProperties};
  const auto result = mapWindow->document().map().exportAs(options);
  if (!result)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, resultErrorMessage(result));
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"exported", true},
    {"path", pathToQString(exportPath)},
    {"stripTbProperties", stripTbProperties},
  });
}

QJsonObject makeStatus(AppController& appController, const mcp::McpBridgeConfig& config)
{
  auto result = QJsonObject{
    {"application", "TrenchBroom"},
    {"version", getBuildVersion()},
    {"mode", mcp::modeName(config.mode)},
    {"pipeName", config.pipeName},
    {"documentCount",
     static_cast<int>(appController.mapWindowManager().mapWindows().size())},
    {"activeDocument", false},
  };

  if (auto* mapWindow = appController.mapWindowManager().topMapWindow())
  {
    const auto& map = mapWindow->document().map();
    result.insert("activeDocument", true);
    result.insert("activeDocumentFileName", QString::fromStdString(map.filename()));
    result.insert("activeDocumentPath", pathAsQString(map.path()));
    result.insert("activeDocumentModified", map.modified());
  }

  return result;
}

QJsonObject doctorJson(AppController& appController, const mcp::McpBridgeConfig& config)
{
  const auto implementedTools = mcp::toolsListJson(config.mode);
  return QJsonObject{
    {"configPath", mcp::defaultConfigPath()},
    {"pipeName", config.pipeName},
    {"mode", mcp::modeName(config.mode)},
    {"tokenPresent", !config.token.isEmpty()},
    {"listening", config.mode != mcp::McpMode::Off},
    {"documentCount",
     static_cast<int>(appController.mapWindowManager().mapWindows().size())},
    {"activeDocument", appController.mapWindowManager().topMapWindow() != nullptr},
    {"implementedToolCount", implementedTools.size()},
    {"implementedTools", implementedTools},
  };
}

mcp::McpBridgeResponse makeFailure(
  const mcp::McpBridgeRequest& request,
  const mcp::McpErrorCode code,
  const QString& message)
{
  return mcp::McpBridgeResponse::failure(request.id, mcp::McpError{code, message});
}

} // namespace

McpBridgeToolResult McpBridgeToolResult::success(QJsonObject result)
{
  return McpBridgeToolResult{true, std::move(result), {}};
}

McpBridgeToolResult McpBridgeToolResult::failure(
  const mcp::McpErrorCode code, QString message)
{
  return McpBridgeToolResult{false, {}, mcp::McpError{code, std::move(message)}};
}

McpBridgeServer::McpBridgeServer(AppController& appController, QObject* parent)
  : McpBridgeServer{
      [&appController, this](const auto& toolName, const auto& params) {
        if (toolName == "tb_status")
        {
          return McpBridgeToolResult::success(makeStatus(appController, m_config));
        }
        if (toolName == "tb_doctor")
        {
          auto doctor = doctorJson(appController, m_config);
          doctor.insert("overlay", m_overlayState);
          return McpBridgeToolResult::success(std::move(doctor));
        }
        if (toolName == "documents_list")
        {
          return McpBridgeToolResult::success(documentsListJson(appController));
        }
        if (toolName == "documents_open")
        {
          return documentOpenResult(appController, params);
        }
        if (toolName == "documents_activate")
        {
          return documentActivateResult(appController, params);
        }
        if (toolName == "documents_save")
        {
          return documentSaveResult(appController, params);
        }
        if (toolName == "documents_close")
        {
          return documentCloseResult(appController, params);
        }
        if (toolName == "documents_export")
        {
          return documentExportResult(appController, params);
        }
        if (toolName == "document_snapshot")
        {
          return McpBridgeToolResult::success(activeDocumentJson(appController));
        }
        if (toolName == "map_snapshot")
        {
          return McpBridgeToolResult::success(mapSnapshotJson(appController));
        }
        if (toolName == "map_search")
        {
          return McpBridgeToolResult::success(mapSearchJson(appController, params));
        }
        if (toolName == "selection_get")
        {
          return McpBridgeToolResult::success(selectionJson(appController));
        }
        if (toolName == "selection_set")
        {
          return selectionSetResult(appController, params);
        }
        if (toolName == "selection_filter")
        {
          return selectionFilterResult(appController, params);
        }
        if (toolName == "selection_by_bounds")
        {
          return selectionByBoundsResult(appController, params);
        }
        if (toolName == "selection_grow")
        {
          return selectionGrowResult(appController, params);
        }
        if (toolName == "viewport_focus")
        {
          return viewportFocusResult(appController, params);
        }
        if (toolName == "viewport_clear_marks")
        {
          return viewportClearMarksResult(appController, params, m_overlayState);
        }
        if (toolName == "actions_list")
        {
          return McpBridgeToolResult::success(actionsListJson(appController));
        }
        if (toolName == "action_execute")
        {
          return actionExecuteResult(appController, params);
        }
        if (toolName == "overlay_set")
        {
          m_overlayState = params;
          return McpBridgeToolResult::success(QJsonObject{
            {"overlay", m_overlayState},
            {"active", true},
          });
        }
        if (toolName == "overlay_clear")
        {
          m_overlayState = QJsonObject{};
          return McpBridgeToolResult::success(QJsonObject{
            {"overlay", m_overlayState},
            {"active", false},
          });
        }
        if (toolName == "entity_create")
        {
          return createEntityResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "entity_update")
        {
          return updateEntityResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "entity_delete")
        {
          return deleteEntityResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "fgd_entities_list")
        {
          return fgdEntitiesListResult(appController, params);
        }
        if (toolName == "entity_schema")
        {
          return entitySchemaResult(appController, params);
        }
        if (toolName == "entity_create_from_schema")
        {
          return createEntityFromSchemaResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "entity_tie_brushes")
        {
          return tieBrushesResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "entity_untie_brushes")
        {
          return untieBrushesResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (
          toolName == "brush_create_box" || toolName == "brush_create_wedge"
          || toolName == "brush_create_cylinder")
        {
          return createBrushResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "history_list")
        {
          return historyListResult(m_operationHistory);
        }
        if (toolName == "history_undo_mcp")
        {
          return historyUndoResult(appController, m_operationHistory);
        }
        if (toolName == "history_redo_mcp")
        {
          return historyRedoResult(appController, m_operationHistory);
        }
        if (toolName == "asset_search")
        {
          return assetSearchResult(appController, params);
        }
        if (
          toolName == "asset_place_model" || toolName == "asset_place_sprite"
          || toolName == "asset_place_sound")
        {
          return placeAssetResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "texture_search")
        {
          return textureSearchResult(appController, params);
        }
        if (toolName == "texture_apply")
        {
          return textureApplyResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (
          toolName == "blockout_create_room" || toolName == "blockout_create_corridor"
          || toolName == "blockout_create_stairs" || toolName == "blockout_create_ramp"
          || toolName == "blockout_create_doorway" || toolName == "blockout_create_cover"
          || toolName == "blockout_create_sky_shell")
        {
          return blockoutCreateResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "blockout_validate")
        {
          return blockoutValidateResult(params);
        }
        return McpBridgeToolResult::failure(
          mcp::McpErrorCode::ToolNotFound,
          QString{"MCP tool is registered but not wired yet: %1"}.arg(toolName));
      },
      parent}
{
}

McpBridgeServer::McpBridgeServer(ToolHandler toolHandler, QObject* parent)
  : QObject{parent}
  , m_toolHandler{std::move(toolHandler)}
{
}

McpBridgeServer::~McpBridgeServer()
{
  stop();
}

bool McpBridgeServer::start(const mcp::McpBridgeConfig& config, QString* error)
{
  stop();
  m_config = config;

  if (m_config.mode == mcp::McpMode::Off)
  {
    return true;
  }

  m_server = std::make_unique<QLocalServer>();
  connect(
    m_server.get(),
    &QLocalServer::newConnection,
    this,
    &McpBridgeServer::handleNewConnection);

  QLocalServer::removeServer(m_config.pipeName);
  if (!m_server->listen(m_config.pipeName))
  {
    if (error)
    {
      *error = m_server->errorString();
    }
    m_server.reset();
    return false;
  }

  return true;
}

void McpBridgeServer::stop()
{
  if (m_server)
  {
    m_server->close();
    QLocalServer::removeServer(m_config.pipeName);
    m_server.reset();
  }
}

bool McpBridgeServer::isListening() const
{
  return m_server != nullptr && m_server->isListening();
}

QString McpBridgeServer::pipeName() const
{
  return m_config.pipeName;
}

mcp::McpMode McpBridgeServer::mode() const
{
  return m_config.mode;
}

mcp::McpBridgeResponse McpBridgeServer::dispatchRequest(
  const mcp::McpBridgeRequest& request) const
{
  if (request.token != m_config.token)
  {
    return makeFailure(
      request, mcp::McpErrorCode::Unauthorized, "Invalid MCP bridge token");
  }

  const auto tool = mcp::findToolDefinition(request.tool);
  if (!tool)
  {
    return makeFailure(
      request,
      mcp::McpErrorCode::ToolNotFound,
      QString{"Unknown MCP tool: %1"}.arg(request.tool));
  }

  if (!mcp::canCallTool(*tool, m_config.mode))
  {
    return makeFailure(
      request,
      mcp::McpErrorCode::Forbidden,
      QString{"MCP tool is not available in mode %1"}.arg(mcp::modeName(m_config.mode)));
  }

  if (tool->implemented)
  {
    const auto result = m_toolHandler(request.tool, request.params);
    if (result.ok)
    {
      return mcp::McpBridgeResponse::success(request.id, result.result);
    }
    return mcp::McpBridgeResponse::failure(request.id, result.error);
  }

  return makeFailure(
    request,
    mcp::McpErrorCode::ToolNotFound,
    QString{"MCP tool is registered but not wired yet: %1"}.arg(request.tool));
}

void McpBridgeServer::handleNewConnection()
{
  while (auto* socket = m_server->nextPendingConnection())
  {
    socket->setParent(this);
    connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
      handleSocketReadyRead(*socket);
    });
    connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
  }
}

void McpBridgeServer::handleSocketReadyRead(QLocalSocket& socket)
{
  while (socket.canReadLine())
  {
    auto parseError = QJsonParseError{};
    const auto document =
      QJsonDocument::fromJson(socket.readLine().trimmed(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
      writeResponse(
        socket,
        mcp::McpBridgeResponse::failure(
          {},
          mcp::McpError{
            mcp::McpErrorCode::InvalidRequest, "Invalid MCP bridge JSON request"}));
      continue;
    }

    auto error = QString{};
    const auto request = mcp::bridgeRequestFromJson(document.object(), &error);
    if (!request)
    {
      writeResponse(
        socket,
        mcp::McpBridgeResponse::failure(
          {}, mcp::McpError{mcp::McpErrorCode::InvalidRequest, error}));
      continue;
    }

    writeResponse(socket, dispatchRequest(*request));
  }
}

void McpBridgeServer::writeResponse(
  QLocalSocket& socket, const mcp::McpBridgeResponse& response) const
{
  socket.write(QJsonDocument{mcp::toJson(response)}.toJson(QJsonDocument::Compact));
  socket.write("\n");
  socket.flush();
}

} // namespace tb::ui
