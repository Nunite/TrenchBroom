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

#include <QBuffer>
#include <QIODevice>
#include <QLocalServer>
#include <QPixmap>
#include <QWidget>

#include "McpBridgeServerTools.h"
#include "gl/Material.h"
#include "gl/MaterialManager.h"
#include "mcp/McpError.h"
#include "mcp/McpToolCatalog.h"
#include "mdl/AddRemoveNodesCommand.h"
#include "mdl/Brush.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
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
#include "mdl/GameInfo.h"
#include "mdl/Grid.h"
#include "mdl/GroupNode.h"
#include "mdl/Issue.h"
#include "mdl/IssueQuickFix.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Brushes.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Geometry.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Map_World.h"
#include "mdl/Node.h"
#include "mdl/NodeContents.h"
#include "mdl/NodeQueries.h"
#include "mdl/PatchNode.h"
#include "mdl/PropertyDefinition.h"
#include "mdl/Selection.h"
#include "mdl/SwapNodeContentsCommand.h"
#include "mdl/Transaction.h"
#include "mdl/UpdateBrushFaceAttributes.h"
#include "mdl/Validator.h"
#include "mdl/WorldNode.h"
#include "ui/Action.h"
#include "ui/ActionExecutionContext.h"
#include "ui/ActionManager.h"
#include "ui/AppController.h"
#include "ui/GetVersion.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"
#include "ui/SystemPaths.h"

#include "kd/string_compare.h"
#include "kd/vector_utils.h"

#include "vm/bbox.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <ranges>
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

QJsonObject mcpNodeSummaryJson(const mdl::Node& node, const mdl::WorldNode& worldNode)
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
    {"world", mcpNodeSummaryJson(worldNode, worldNode)},
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
    results.push_back(mcpNodeSummaryJson(node, worldNode));
  }

  for (const auto* child : node.children())
  {
    collectSearchResults(*child, worldNode, query, results);
  }
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
  operation.changedObjectIds = changedObjectIds;
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

std::optional<vm::bbox3d> boundsFromJson(const QJsonObject& params, QString& error)
{
  const auto min = mcpVec3FromJson(params, "min", error);
  if (!min)
  {
    return std::nullopt;
  }
  const auto max = mcpVec3FromJson(params, "max", error);
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
  const auto min = mcpVec3FromJson(params, minKey, error);
  if (!min)
  {
    return std::nullopt;
  }
  const auto max = mcpVec3FromJson(params, maxKey, error);
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

std::string mcpOptionalString(
  const QJsonObject& params, const QString& key, const std::string& defaultValue = {})
{
  const auto value = params.value(key);
  return value.isString() ? value.toString().toStdString() : defaultValue;
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

double optionalDouble(
  const QJsonObject& params, const QString& key, const double defaultValue)
{
  const auto value = params.value(key);
  return value.isDouble() ? value.toDouble(defaultValue) : defaultValue;
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
  auto material = mcpOptionalString(params, "material");
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

std::optional<QJsonArray> mcpAddNodesWithTransaction(
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

Result<mdl::Brush> createPyramidBrush(
  const mdl::BrushBuilder& builder,
  const vm::bbox3d& bounds,
  const vm::axis::type axis,
  const std::string& material)
{
  const auto& min = bounds.min;
  const auto& max = bounds.max;
  const auto center = bounds.center();

  auto points = std::vector<vm::vec3d>{};
  switch (axis)
  {
  case vm::axis::x:
    points = {
      {min.x(), min.y(), min.z()},
      {min.x(), max.y(), min.z()},
      {min.x(), min.y(), max.z()},
      {min.x(), max.y(), max.z()},
      {max.x(), center.y(), center.z()},
    };
    break;
  case vm::axis::y:
    points = {
      {min.x(), min.y(), min.z()},
      {max.x(), min.y(), min.z()},
      {min.x(), min.y(), max.z()},
      {max.x(), min.y(), max.z()},
      {center.x(), max.y(), center.z()},
    };
    break;
  case vm::axis::z:
    points = {
      {min.x(), min.y(), min.z()},
      {max.x(), min.y(), min.z()},
      {min.x(), max.y(), min.z()},
      {max.x(), max.y(), min.z()},
      {center.x(), center.y(), max.z()},
    };
    break;
  }

  return builder.createBrush(points, material);
}

Result<mdl::Brush> createTetrahedronBrush(
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
      {max.x(), max.y(), max.z()},
    };
    break;
  case vm::axis::y:
    points = {
      {min.x(), min.y(), min.z()},
      {max.x(), min.y(), min.z()},
      {min.x(), min.y(), max.z()},
      {max.x(), max.y(), max.z()},
    };
    break;
  case vm::axis::z:
    points = {
      {min.x(), min.y(), min.z()},
      {max.x(), min.y(), min.z()},
      {min.x(), max.y(), min.z()},
      {max.x(), max.y(), max.z()},
    };
    break;
  }

  return builder.createBrush(points, material);
}

std::optional<mdl::Brush> createBrushFromPlaneTriples(
  const mdl::Map& map,
  const QJsonObject& params,
  const std::string& material,
  QString& error)
{
  const auto planesValue = params.value("planes");
  if (!planesValue.isArray())
  {
    error = "planes must be an array";
    return std::nullopt;
  }

  const auto planes = planesValue.toArray();
  if (planes.size() < 4)
  {
    error = "planes must contain at least four plane definitions";
    return std::nullopt;
  }

  auto faces = std::vector<mdl::BrushFace>{};
  faces.reserve(static_cast<size_t>(planes.size()));
  auto planeIndex = 0;
  for (const auto& planeValue : planes)
  {
    if (!planeValue.isArray())
    {
      error = QString{"planes[%1] must be an array of three points"}.arg(planeIndex);
      return std::nullopt;
    }

    const auto pointArray = planeValue.toArray();
    if (pointArray.size() != 3)
    {
      error = QString{"planes[%1] must contain exactly three points"}.arg(planeIndex);
      return std::nullopt;
    }

    auto points = std::array<vm::vec3d, 3>{};
    for (auto pointIndex = 0; pointIndex < 3; ++pointIndex)
    {
      if (!pointArray[pointIndex].isArray())
      {
        error = QString{"planes[%1][%2] must be an array of three numbers"}
                  .arg(planeIndex)
                  .arg(pointIndex);
        return std::nullopt;
      }

      auto pointParams = QJsonObject{};
      pointParams.insert("point", pointArray[pointIndex].toArray());
      const auto point = mcpVec3FromJson(pointParams, "point", error);
      if (!point)
      {
        error = QString{"planes[%1][%2]: %3"}.arg(planeIndex).arg(pointIndex).arg(error);
        return std::nullopt;
      }
      points[static_cast<size_t>(pointIndex)] = *point;
    }

    auto face = mdl::BrushFace::create(
      points[0],
      points[1],
      points[2],
      mdl::BrushFaceAttributes{material},
      map.worldNode().mapFormat());
    if (face.is_error())
    {
      error = QString{"planes[%1] does not define a valid plane"}.arg(planeIndex);
      return std::nullopt;
    }
    faces.push_back(std::move(face.value()));
    ++planeIndex;
  }

  auto brush = mdl::Brush::create(map.worldBounds(), std::move(faces));
  if (brush.is_error())
  {
    error = "planes do not form a valid closed convex brush";
    return std::nullopt;
  }
  return std::move(brush.value());
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
    material = mcpOptionalString(params, "material", "sky");
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

  const auto changedObjectIds = mcpAddNodesWithTransaction(
    map, transactionName, nodes, mcpOptionalBool(params, "select", true));
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
  mcpRecordOperation(
    history, nextOperationIndex, toolName, transactionName, *changedObjectIds, result);
  result.insert("brushCount", static_cast<int>(nodes.size()));
  result.insert("material", QString::fromStdString(material));
  return McpBridgeToolResult::success(std::move(result));
}

QJsonObject brushTypeJson(
  const QString& name,
  const bool supported,
  const QString& description,
  const bool createsMultipleBrushes = false)
{
  auto result = QJsonObject{
    {"name", name},
    {"supported", supported},
    {"description", description},
    {"createsMultipleBrushes", createsMultipleBrushes},
  };
  if (!supported)
  {
    result.insert("reason", "No stable native MCP primitive generator yet");
  }
  return result;
}

McpBridgeToolResult brushTypesListResult()
{
  return McpBridgeToolResult::success(QJsonObject{
    {"types",
     QJsonArray{
       brushTypeJson("box", true, "Single convex cuboid brush."),
       brushTypeJson("wedge", true, "Single convex wedge brush."),
       brushTypeJson("cylinder", true, "Single convex cylinder brush."),
       brushTypeJson("cone", true, "Single convex cone brush."),
       brushTypeJson(
         "pipe", true, "Hollow cylinder made from convex brush segments.", true),
       brushTypeJson("sphere", true, "Single convex UV or ico sphere brush."),
       brushTypeJson("pyramid", true, "Single convex square pyramid brush."),
       brushTypeJson("tetrahedron", true, "Single convex tetrahedron brush."),
       brushTypeJson("from_planes", true, "Expert brush from plane point triples."),
       brushTypeJson(
         "arch", false, "Arch primitives need a dedicated stable generator.", true),
       brushTypeJson(
         "torus",
         false,
         "Torus geometry cannot be represented as one convex BSP brush.",
         true),
     }},
  });
}

QString brushTypeFromToolName(const QString& toolName, const QJsonObject& params)
{
  if (toolName == "brush_create")
  {
    return params.value("type").toString().trimmed().toLower();
  }

  static const auto Prefix = QString{"brush_create_"};
  return toolName.startsWith(Prefix) ? toolName.mid(Prefix.size()) : QString{};
}

size_t clampedSizeParam(
  const QJsonObject& params,
  const QString& key,
  const size_t defaultValue,
  const size_t minValue,
  const size_t maxValue)
{
  return std::clamp(optionalSize(params, key, defaultValue), minValue, maxValue);
}

std::optional<std::vector<mdl::Brush>> createBrushesForType(
  const mdl::Map& map,
  const mdl::BrushBuilder& builder,
  const QString& type,
  const QJsonObject& params,
  const std::string& material,
  QString& error)
{
  if (type.isEmpty())
  {
    error = "brush_create requires type";
    return std::nullopt;
  }

  if (type == "from_planes")
  {
    auto brush = createBrushFromPlaneTriples(map, params, material, error);
    if (!brush)
    {
      return std::nullopt;
    }
    return std::vector<mdl::Brush>{std::move(*brush)};
  }

  const auto bounds = boundsFromJson(params, error);
  if (!bounds)
  {
    return std::nullopt;
  }

  auto brush = Result<mdl::Brush>{Error{"Unsupported brush type"}};
  if (type == "box")
  {
    brush = builder.createCuboid(*bounds, material);
  }
  else if (type == "wedge")
  {
    const auto axis = axisFromJson(params, "axis", vm::axis::x, error);
    if (!axis)
    {
      return std::nullopt;
    }
    brush = createWedgeBrush(builder, *bounds, *axis, material);
  }
  else if (type == "cylinder")
  {
    const auto axis = axisFromJson(params, "axis", vm::axis::z, error);
    if (!axis)
    {
      return std::nullopt;
    }
    const auto sides = clampedSizeParam(params, "sides", 16, 3, 128);
    brush =
      builder.createCylinder(*bounds, mdl::EdgeAlignedCircle{sides}, *axis, material);
  }
  else if (type == "cone")
  {
    const auto axis = axisFromJson(params, "axis", vm::axis::z, error);
    if (!axis)
    {
      return std::nullopt;
    }
    const auto sides = clampedSizeParam(params, "sides", 16, 3, 128);
    brush = builder.createCone(*bounds, mdl::EdgeAlignedCircle{sides}, *axis, material);
  }
  else if (type == "pipe")
  {
    const auto axis = axisFromJson(params, "axis", vm::axis::z, error);
    if (!axis)
    {
      return std::nullopt;
    }
    const auto sides = clampedSizeParam(params, "sides", 16, 3, 128);
    const auto thickness = optionalDouble(params, "thickness", 16.0);
    if (!validThickness(thickness))
    {
      error = "thickness must be greater than zero";
      return std::nullopt;
    }
    auto brushes = builder.createHollowCylinder(
      *bounds, thickness, mdl::EdgeAlignedCircle{sides}, *axis, material);
    if (brushes.is_error())
    {
      error = "Could not create pipe brushes from the given bounds";
      return std::nullopt;
    }
    return std::move(brushes.value());
  }
  else if (type == "sphere")
  {
    if (params.value("iterations").isDouble())
    {
      const auto iterations = clampedSizeParam(params, "iterations", 1, 1, 4);
      brush = builder.createIcoSphere(*bounds, iterations, material);
    }
    else
    {
      const auto axis = axisFromJson(params, "axis", vm::axis::z, error);
      if (!axis)
      {
        return std::nullopt;
      }
      const auto sides = clampedSizeParam(params, "sides", 12, 3, 64);
      const auto rings = clampedSizeParam(params, "rings", 6, 1, 64);
      brush = builder.createUVSphere(
        *bounds, mdl::EdgeAlignedCircle{sides}, rings, *axis, material);
    }
  }
  else if (type == "pyramid")
  {
    const auto axis = axisFromJson(params, "axis", vm::axis::z, error);
    if (!axis)
    {
      return std::nullopt;
    }
    brush = createPyramidBrush(builder, *bounds, *axis, material);
  }
  else if (type == "tetrahedron")
  {
    const auto axis = axisFromJson(params, "axis", vm::axis::z, error);
    if (!axis)
    {
      return std::nullopt;
    }
    brush = createTetrahedronBrush(builder, *bounds, *axis, material);
  }
  else if (type == "arch" || type == "torus")
  {
    error = QString{"Brush primitive type is not supported yet: %1"}.arg(type);
    return std::nullopt;
  }
  else
  {
    error = QString{"Unknown brush primitive type: %1"}.arg(type);
    return std::nullopt;
  }

  if (brush.is_error())
  {
    error = QString{"Could not create %1 brush from the given parameters"}.arg(type);
    return std::nullopt;
  }
  return std::vector<mdl::Brush>{std::move(brush.value())};
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
  auto& map = mapWindow->document().map();
  const auto material = materialNameFromParams(map, params);
  const auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};

  const auto type = brushTypeFromToolName(toolName, params);
  auto brushes = createBrushesForType(map, builder, type, params, material, error);
  if (!brushes)
  {
    return invalidParamsFailure(error);
  }

  auto nodes = std::vector<mdl::Node*>{};
  nodes.reserve(brushes->size());
  for (auto& brush : *brushes)
  {
    nodes.push_back(new mdl::BrushNode{std::move(brush)});
  }

  const auto transactionName = QString{"MCP: Create %1 brush"}.arg(type);
  const auto changedObjectIds = mcpAddNodesWithTransaction(
    map, transactionName, nodes, mcpOptionalBool(params, "select", true));
  if (!changedObjectIds)
  {
    for (auto* node : nodes)
    {
      delete node;
    }
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not add brush to the active document");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history, nextOperationIndex, toolName, transactionName, *changedObjectIds, result);
  auto brushJson = QJsonArray{};
  for (const auto* node : nodes)
  {
    brushJson.push_back(mcpNodeSummaryJson(*node, map.worldNode()));
  }
  result.insert("type", type);
  result.insert("brushes", brushJson);
  result.insert("brushCount", brushJson.size());
  if (nodes.size() == 1)
  {
    result.insert("brush", mcpNodeSummaryJson(*nodes.front(), map.worldNode()));
  }
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
    nodes.push_back(mcpNodeSummaryJson(*node, worldNode));
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
  const mdl::Node& node, const QString& classname, const QString& targetname)
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

  if (!entityPropertyMatches(
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
    worldNode,
    worldNode,
    params,
    queryBounds,
    optionalSize(params, "limit", 100),
    matches);

  if (mcpOptionalBool(params, "select", false))
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
    results.push_back(mcpNodeSummaryJson(*node, worldNode));
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
    return invalidParamsFailure(
      "selection_grow mode must be parents, children, or siblings");
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
    const auto selectionResult =
      selectionSetResult(appController, QJsonObject{{"objectIds", objectIds.toArray()}});
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

  if (mcpOptionalBool(params, "clearSelection", false))
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
    {"selectionCleared", mcpOptionalBool(params, "clearSelection", false)},
  });
}

QString makeCaptureFilePath()
{
  const auto captureDir = SystemPaths::tempDirectory() / "TrenchBroomMCP";
  auto error = std::error_code{};
  std::filesystem::create_directories(captureDir, error);
  if (error)
  {
    return {};
  }

  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  return pathToQString(captureDir / QString{"viewport-%1.png"}.arg(millis).toStdString());
}

McpBridgeToolResult viewportCaptureCurrentResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }
  if (!mapWindow->isVisible())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Active map window is not visible");
  }

  const auto pixmap = mapWindow->grab();
  if (pixmap.isNull())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not capture active map window");
  }

  if (mcpOptionalBool(params, "returnBase64", false))
  {
    auto bytes = QByteArray{};
    auto buffer = QBuffer{&bytes};
    buffer.open(QIODevice::WriteOnly);
    if (!pixmap.save(&buffer, "PNG"))
    {
      return McpBridgeToolResult::failure(
        mcp::McpErrorCode::InternalError, "Could not encode capture as PNG");
    }
    return McpBridgeToolResult::success(QJsonObject{
      {"format", "png"},
      {"base64", QString::fromLatin1(bytes.toBase64())},
      {"width", pixmap.width()},
      {"height", pixmap.height()},
      {"scope", "window"},
    });
  }

  const auto filePath = makeCaptureFilePath();
  if (filePath.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not create MCP capture directory");
  }
  if (!pixmap.save(filePath, "PNG"))
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not save capture PNG");
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"format", "png"},
    {"path", filePath},
    {"width", pixmap.width()},
    {"height", pixmap.height()},
    {"scope", "window"},
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
    return invalidParamsFailure(
      QString{"Document does not exist: %1"}.arg(pathToQString(path)));
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
  const auto savePath = pathValue.isString() && !pathValue.toString().trimmed().isEmpty()
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

  if (
    mapWindow->document().map().modified()
    && !mcpOptionalBool(params, "discardChanges", false))
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden,
      "Document has unsaved changes; pass discardChanges=true to close it");
  }

  const auto document = documentJson(*mapWindow, 0);
  mapWindow->closeDocument(mcpOptionalBool(params, "discardChanges", false));

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

  const auto stripTbProperties = mcpOptionalBool(params, "stripTbProperties", true);
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

} // namespace

McpBridgeToolResult noActiveDocumentFailure()
{
  return McpBridgeToolResult::failure(
    mcp::McpErrorCode::NoActiveDocument, "No active document");
}

McpBridgeToolResult invalidParamsFailure(const QString& message)
{
  return McpBridgeToolResult::failure(mcp::McpErrorCode::InvalidParams, message);
}

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
        if (toolName == "viewport_capture_current")
        {
          return viewportCaptureCurrentResult(appController, params);
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
        if (toolName == "brush_types_list")
        {
          return brushTypesListResult();
        }
        if (
          toolName == "brush_create" || toolName == "brush_create_box"
          || toolName == "brush_create_wedge" || toolName == "brush_create_cylinder"
          || toolName == "brush_create_cone" || toolName == "brush_create_pipe"
          || toolName == "brush_create_sphere" || toolName == "brush_create_pyramid"
          || toolName == "brush_create_tetrahedron"
          || toolName == "brush_create_from_planes")
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
        if (toolName == "textures_list" || toolName == "texture_search")
        {
          return textureSearchResult(appController, params);
        }
        if (toolName == "texture_apply")
        {
          return textureApplyResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "texture_replace")
        {
          return textureReplaceResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "texture_align_face")
        {
          return textureAlignFaceResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "texture_copy_from_face")
        {
          return textureCopyFromFaceResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "face_list")
        {
          return faceListResult(appController, params);
        }
        if (toolName == "face_select")
        {
          return faceSelectResult(appController, params);
        }
        if (toolName == "face_texture_set")
        {
          return faceTextureSetResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "objects_delete")
        {
          return deleteObjectsResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "objects_transform")
        {
          return transformObjectsResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "map_validate")
        {
          return mapValidateResult(appController, params);
        }
        if (toolName == "problems_check")
        {
          return problemsCheckResult(appController, params);
        }
        if (toolName == "problems_fix")
        {
          return problemsFixResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "map_fix_all_safe")
        {
          return mapFixAllSafeResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "compile_profiles_list")
        {
          return compileProfilesListResult(appController);
        }
        if (toolName == "compile_run")
        {
          return compileRunResult(appController, params);
        }
        if (toolName == "compile_log_tail")
        {
          return compileLogTailResult(appController, params);
        }
        if (toolName == "leaks_load_pointfile")
        {
          return leaksLoadPointfileResult(appController, params);
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

} // namespace tb::ui
