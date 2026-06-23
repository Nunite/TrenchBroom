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
#include "mcp/McpError.h"
#include "mdl/AddRemoveNodesCommand.h"
#include "mdl/Brush.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/CircleShape.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Map_World.h"
#include "mdl/Node.h"
#include "mdl/PatchNode.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"

#include "vm/bbox.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
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

} // namespace

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

  const auto changedObjectIds = addNodesWithTransaction(
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
  const auto changedObjectIds = addNodesWithTransaction(
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
    brushJson.push_back(nodeSummaryJson(*node, map.worldNode()));
  }
  result.insert("type", type);
  result.insert("brushes", brushJson);
  result.insert("brushCount", brushJson.size());
  if (nodes.size() == 1)
  {
    result.insert("brush", nodeSummaryJson(*nodes.front(), map.worldNode()));
  }
  return McpBridgeToolResult::success(std::move(result));
}

} // namespace tb::ui
