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

QString makeOperationId(int& nextOperationIndex)
{
  return QString{"mcp-op-%1"}.arg(nextOperationIndex++);
}

QJsonObject mutationResultJson(const McpOperationRecord& operation)
{
  auto result = QJsonObject{};
  result.insert("operationId", operation.operationId);
  result.insert("transactionName", operation.transactionName);
  result.insert("changedObjectCount", operation.changedObjectIds.size());
  result.insert(
    "resourceUri", QString{"tbmcp://operation/%1"}.arg(operation.operationId));
  return result;
}

void mcpRecordOperation(
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const QString& toolName,
  const QString& transactionName,
  const QJsonArray& changedObjectIds,
  QJsonObject& result,
  const QJsonObject& detail = {})
{
  auto operation = McpOperationRecord{};
  operation.operationId = makeOperationId(nextOperationIndex);
  operation.toolName = toolName;
  operation.transactionName = transactionName;
  operation.setChangedObjectIds(changedObjectIds);
  result = mutationResultJson(operation);
  operation.setSummary(result);
  operation.setDetail(detail);
  history.push_back(std::move(operation));
}

void applyDetailLevel(
  QJsonObject& result,
  const QJsonArray& changedObjectIds,
  const QString& detail,
  const QJsonArray& fullResults = {})
{
  const auto normalized = detail.trimmed().toLower();
  if (normalized == "ids")
  {
    result.insert("changedObjectIds", changedObjectIds);
  }
  else if (normalized == "full")
  {
    result.insert("changedObjectIds", changedObjectIds);
    if (!fullResults.isEmpty())
    {
      result.insert("results", fullResults);
    }
  }
  result.insert(
    "detail",
    normalized == "full"  ? "full"
    : normalized == "ids" ? "ids"
                          : "summary");
}

vm::bbox3d boundsForNodes(const std::vector<mdl::Node*>& nodes)
{
  auto result = vm::bbox3d{};
  auto first = true;
  for (const auto* node : nodes)
  {
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

QJsonArray nodeSummariesJson(
  const std::vector<mdl::Node*>& nodes, const mdl::WorldNode& worldNode)
{
  auto result = QJsonArray{};
  for (const auto* node : nodes)
  {
    result.push_back(nodeSummaryJson(*node, worldNode));
  }
  return result;
}

QJsonObject pendingNodeSummaryJson(const mdl::Node& node)
{
  auto result = QJsonObject{
    {"type", nodeTypeName(node)},
    {"name", QString::fromStdString(node.name())},
    {"childCount", static_cast<int>(node.childCount())},
    {"descendantCount", static_cast<int>(node.descendantCount())},
    {"logicalBounds", boundsToJson(node.logicalBounds())},
  };

  if (const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(&node))
  {
    result.insert("faceCount", static_cast<int>(brushNode->brush().faceCount()));
  }

  return result;
}

QJsonArray pendingNodeSummariesJson(const std::vector<mdl::Node*>& nodes)
{
  auto result = QJsonArray{};
  for (const auto* node : nodes)
  {
    result.push_back(pendingNodeSummaryJson(*node));
  }
  return result;
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

std::optional<vm::vec2d> mcpVec2FromJsonValue(
  const QJsonValue& value, const QString& key, QString& error)
{
  if (!value.isArray())
  {
    error = QString{"%1 must be an array of two numbers"}.arg(key);
    return std::nullopt;
  }

  const auto array = value.toArray();
  if (array.size() != 2)
  {
    error = QString{"%1 must contain exactly two numbers"}.arg(key);
    return std::nullopt;
  }

  if (!array[0].isDouble() || !array[1].isDouble())
  {
    error = QString{"%1 values must be numbers"}.arg(key);
    return std::nullopt;
  }

  const auto x = array[0].toDouble();
  const auto y = array[1].toDouble();
  if (!std::isfinite(x) || !std::isfinite(y))
  {
    error = QString{"%1 values must be finite"}.arg(key);
    return std::nullopt;
  }
  return vm::vec2d{x, y};
}

std::optional<std::vector<vm::vec2d>> points2DFromJson(
  const QJsonObject& params,
  const QString& key,
  const size_t minPointCount,
  QString& error)
{
  const auto value = params.value(key);
  if (!value.isArray())
  {
    error = QString{"%1 must be an array of [x,y] points"}.arg(key);
    return std::nullopt;
  }

  const auto array = value.toArray();
  if (static_cast<size_t>(array.size()) < minPointCount)
  {
    error = QString{"%1 must contain at least %2 points"}.arg(key).arg(minPointCount);
    return std::nullopt;
  }

  auto result = std::vector<vm::vec2d>{};
  result.reserve(static_cast<size_t>(array.size()));
  for (auto i = 0; i < array.size(); ++i)
  {
    auto point = mcpVec2FromJsonValue(array[i], QString{"%1[%2]"}.arg(key).arg(i), error);
    if (!point)
    {
      return std::nullopt;
    }
    result.push_back(*point);
  }
  return result;
}

std::optional<std::vector<vm::vec2d>> points2DFromJson(
  const QJsonObject& params, const QString& key, QString& error)
{
  return points2DFromJson(params, key, 3u, error);
}

std::optional<vm::vec3d> mcpVec3FromJsonValue(
  const QJsonValue& value, const QString& key, QString& error)
{
  auto object = QJsonObject{};
  object.insert(key, value);
  return mcpVec3FromJson(object, key, error);
}

std::optional<std::vector<vm::vec3d>> points3DFromJson(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto value = params.value(key);
  if (!value.isArray())
  {
    error = QString{"%1 must be an array of [x,y,z] points"}.arg(key);
    return std::nullopt;
  }

  const auto array = value.toArray();
  if (array.size() < 4)
  {
    error = QString{"%1 must contain at least four points"}.arg(key);
    return std::nullopt;
  }

  auto result = std::vector<vm::vec3d>{};
  result.reserve(static_cast<size_t>(array.size()));
  for (auto i = 0; i < array.size(); ++i)
  {
    auto point = mcpVec3FromJsonValue(array[i], QString{"%1[%2]"}.arg(key).arg(i), error);
    if (!point)
    {
      return std::nullopt;
    }
    result.push_back(*point);
  }
  return result;
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

constexpr double Pi = 3.14159265358979323846;
constexpr double GeometryEpsilon = 0.001;

enum class SectorSnapMode
{
  Grid,
  Radial,
  None,
};

double degreesToRadians(const double degrees)
{
  return degrees * Pi / 180.0;
}

bool finitePositive(const double value)
{
  return std::isfinite(value) && value > 0.0;
}

double polygonSignedArea(const std::vector<vm::vec2d>& points)
{
  auto area = 0.0;
  for (size_t i = 0; i < points.size(); ++i)
  {
    const auto& current = points[i];
    const auto& next = points[(i + 1) % points.size()];
    area += current.x() * next.y() - next.x() * current.y();
  }
  return area * 0.5;
}

bool isStrictlyConvexPolygon(const std::vector<vm::vec2d>& points)
{
  if (points.size() < 3)
  {
    return false;
  }

  auto sign = 0;
  for (size_t i = 0; i < points.size(); ++i)
  {
    const auto& a = points[i];
    const auto& b = points[(i + 1) % points.size()];
    const auto& c = points[(i + 2) % points.size()];
    const auto ab = b - a;
    const auto bc = c - b;
    const auto cross = ab.x() * bc.y() - ab.y() * bc.x();
    if (std::abs(cross) <= GeometryEpsilon)
    {
      return false;
    }
    const auto currentSign = cross > 0.0 ? 1 : -1;
    if (sign == 0)
    {
      sign = currentSign;
    }
    else if (sign != currentSign)
    {
      return false;
    }
  }
  return true;
}

bool nearlyEqual(
  const double lhs, const double rhs, const double epsilon = GeometryEpsilon)
{
  return std::abs(lhs - rhs) <= epsilon;
}

bool gridAligned(const double value, const double grid)
{
  if (!finitePositive(grid))
  {
    return false;
  }
  return nearlyEqual(value / grid, std::round(value / grid), 0.01);
}

bool gridAligned(const vm::vec3d& value, const double grid)
{
  return gridAligned(value.x(), grid) && gridAligned(value.y(), grid)
         && gridAligned(value.z(), grid);
}

QJsonArray vertexPositionsToJson(const std::vector<vm::vec3d>& vertices)
{
  auto result = QJsonArray{};
  for (const auto& vertex : vertices)
  {
    result.push_back(vecToJson(vertex));
  }
  return result;
}

QStringList brushMaterials(const mdl::Brush& brush)
{
  auto materials = QStringList{};
  for (const auto& face : brush.faces())
  {
    const auto material = QString::fromStdString(face.attributes().materialName());
    if (!materials.contains(material))
    {
      materials.push_back(material);
    }
  }
  return materials;
}

QStringList brushMaterialsForObjectIds(mdl::Map& map, const QJsonArray& objectIds)
{
  auto materials = QStringList{};
  for (const auto& value : objectIds)
  {
    auto* node = resolveNodeId(map.worldNode(), value.toString());
    const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(node);
    if (brushNode == nullptr)
    {
      continue;
    }
    for (const auto& material : brushMaterials(brushNode->brush()))
    {
      if (!materials.contains(material))
      {
        materials.push_back(material);
      }
    }
  }
  return materials;
}

QJsonArray stringListToJsonArray(const QStringList& values)
{
  auto result = QJsonArray{};
  for (const auto& value : values)
  {
    result.push_back(value);
  }
  return result;
}

bool brushGridAligned(const mdl::Brush& brush, const double grid)
{
  return std::ranges::all_of(brush.vertexPositions(), [&](const auto& vertex) {
    return gridAligned(vertex, grid);
  });
}

std::vector<mdl::BrushNode*> selectedBrushNodes(mdl::Map& map)
{
  auto result = std::vector<mdl::BrushNode*>{};
  for (auto* node : map.selection().nodes)
  {
    if (auto* brushNode = dynamic_cast<mdl::BrushNode*>(node))
    {
      result.push_back(brushNode);
    }
  }
  return result;
}

std::vector<mdl::BrushNode*> brushNodesFromOperationId(
  mdl::Map& map,
  const QString& operationId,
  const std::vector<McpOperationRecord>& history,
  QString& error)
{
  const auto operationIt = std::ranges::find_if(
    history, [&](const auto& operation) { return operation.operationId == operationId; });
  if (operationIt == history.end())
  {
    error = QString{"Unknown MCP operation id: %1"}.arg(operationId);
    return {};
  }

  auto result = std::vector<mdl::BrushNode*>{};
  for (const auto& id : operationIt->changedObjectIds)
  {
    auto* node = resolveNodeId(map.worldNode(), id);
    if (auto* brushNode = dynamic_cast<mdl::BrushNode*>(node))
    {
      result.push_back(brushNode);
    }
  }
  return result;
}

vm::vec2d polarPoint(
  const vm::vec2d& center, const double radius, const double angleDegrees)
{
  const auto radians = degreesToRadians(angleDegrees);
  return center + vm::vec2d{std::cos(radians) * radius, std::sin(radians) * radius};
}

double snapToGrid(const double value, const double grid = 1.0)
{
  return finitePositive(grid) ? std::round(value / grid) * grid : value;
}

vm::vec2d snapToGrid(const vm::vec2d& value, const double grid = 1.0)
{
  return vm::vec2d{snapToGrid(value.x(), grid), snapToGrid(value.y(), grid)};
}

vm::vec3d snapToGrid(const vm::vec3d& value, const double grid = 1.0)
{
  return vm::vec3d{
    snapToGrid(value.x(), grid),
    snapToGrid(value.y(), grid),
    snapToGrid(value.z(), grid)};
}

std::vector<vm::vec2d> snapPointsToGrid(
  const std::vector<vm::vec2d>& points, const double grid)
{
  auto result = std::vector<vm::vec2d>{};
  result.reserve(points.size());
  for (const auto& point : points)
  {
    result.push_back(snapToGrid(point, grid));
  }
  return result;
}

std::vector<vm::vec3d> snapPointsToGrid(
  const std::vector<vm::vec3d>& points, const double grid)
{
  auto result = std::vector<vm::vec3d>{};
  result.reserve(points.size());
  for (const auto& point : points)
  {
    result.push_back(snapToGrid(point, grid));
  }
  return result;
}

std::optional<SectorSnapMode> sectorSnapModeFromJson(
  const QJsonObject& params,
  const QString& key,
  const SectorSnapMode defaultValue,
  QString& error)
{
  const auto value = params.value(key);
  if (value.isUndefined())
  {
    return defaultValue;
  }
  if (!value.isString())
  {
    error = QString{"%1 must be grid, radial, or none"}.arg(key);
    return std::nullopt;
  }

  const auto mode = value.toString().trimmed().toLower();
  if (mode == "grid")
  {
    return SectorSnapMode::Grid;
  }
  if (mode == "radial")
  {
    return SectorSnapMode::Radial;
  }
  if (mode == "none")
  {
    return SectorSnapMode::None;
  }

  error = QString{"%1 must be grid, radial, or none"}.arg(key);
  return std::nullopt;
}

std::optional<vm::bbox3d> snapBoundsToGrid(
  const vm::bbox3d& bounds, const double grid, QString& error)
{
  const auto min = snapToGrid(bounds.min, grid);
  const auto max = snapToGrid(bounds.max, grid);
  if (min.x() >= max.x() || min.y() >= max.y() || min.z() >= max.z())
  {
    error =
      "bounds collapse after grid snapping; use larger dimensions or a smaller grid";
    return std::nullopt;
  }
  return vm::bbox3d{min, max};
}

std::optional<mdl::Brush> createPrismBrush(
  const mdl::BrushBuilder& builder,
  std::vector<vm::vec2d> points,
  const double minZ,
  const double maxZ,
  const std::string& material,
  QString& error)
{
  if (!std::isfinite(minZ) || !std::isfinite(maxZ) || minZ >= maxZ)
  {
    error = "minZ must be smaller than maxZ";
    return std::nullopt;
  }
  if (!isStrictlyConvexPolygon(points))
  {
    error = "points2d must form a strictly convex polygon";
    return std::nullopt;
  }

  if (polygonSignedArea(points) < 0.0)
  {
    std::reverse(points.begin(), points.end());
  }

  auto vertices = std::vector<vm::vec3d>{};
  vertices.reserve(points.size() * 2u);
  for (const auto& point : points)
  {
    vertices.emplace_back(point.x(), point.y(), minZ);
  }
  for (const auto& point : points)
  {
    vertices.emplace_back(point.x(), point.y(), maxZ);
  }

  auto brush = builder.createBrush(vertices, material);
  if (brush.is_error())
  {
    error = "Could not create prism brush from points2d";
    return std::nullopt;
  }
  return std::move(brush.value());
}

std::optional<std::vector<vm::vec2d>> sectorPolygon(
  const vm::vec2d& center,
  const double innerRadius,
  const double outerRadius,
  const double startAngle,
  const double endAngle,
  QString& error)
{
  if (!finitePositive(innerRadius) || !finitePositive(outerRadius))
  {
    error = "innerRadius and outerRadius must be greater than zero";
    return std::nullopt;
  }
  if (innerRadius >= outerRadius)
  {
    error = "innerRadius must be smaller than outerRadius";
    return std::nullopt;
  }
  if (!std::isfinite(startAngle) || !std::isfinite(endAngle))
  {
    error = "startAngle and endAngle must be finite";
    return std::nullopt;
  }

  const auto span = endAngle - startAngle;
  if (std::abs(span) <= GeometryEpsilon || std::abs(span) > 180.0)
  {
    error = "sector angle must be greater than zero and at most 180 degrees";
    return std::nullopt;
  }

  if (span > 0.0)
  {
    return std::vector<vm::vec2d>{
      polarPoint(center, innerRadius, startAngle),
      polarPoint(center, outerRadius, startAngle),
      polarPoint(center, outerRadius, endAngle),
      polarPoint(center, innerRadius, endAngle),
    };
  }

  return std::vector<vm::vec2d>{
    polarPoint(center, innerRadius, endAngle),
    polarPoint(center, outerRadius, endAngle),
    polarPoint(center, outerRadius, startAngle),
    polarPoint(center, innerRadius, startAngle),
  };
}

vm::vec2d snapSectorPoint(
  const vm::vec2d& center,
  const double radius,
  const double angleDegrees,
  const SectorSnapMode snapMode,
  const double grid)
{
  const auto point = polarPoint(center, radius, angleDegrees);
  if (snapMode == SectorSnapMode::Radial || snapMode == SectorSnapMode::None)
  {
    return point;
  }

  const auto snapped = snapToGrid(point, grid);
  return snapped;
}

std::optional<std::vector<vm::vec2d>> sectorPolygon(
  const vm::vec2d& center,
  const double innerRadius,
  const double outerRadius,
  const double startAngle,
  const double endAngle,
  const SectorSnapMode snapMode,
  const double grid,
  QString& error)
{
  auto polygon =
    sectorPolygon(center, innerRadius, outerRadius, startAngle, endAngle, error);
  if (!polygon || snapMode == SectorSnapMode::None)
  {
    return polygon;
  }

  if (snapMode == SectorSnapMode::Grid)
  {
    return snapPointsToGrid(*polygon, grid);
  }

  const auto span = endAngle - startAngle;
  if (span > 0.0)
  {
    return std::vector<vm::vec2d>{
      snapSectorPoint(center, innerRadius, startAngle, snapMode, grid),
      snapSectorPoint(center, outerRadius, startAngle, snapMode, grid),
      snapSectorPoint(center, outerRadius, endAngle, snapMode, grid),
      snapSectorPoint(center, innerRadius, endAngle, snapMode, grid),
    };
  }

  return std::vector<vm::vec2d>{
    snapSectorPoint(center, innerRadius, endAngle, snapMode, grid),
    snapSectorPoint(center, outerRadius, endAngle, snapMode, grid),
    snapSectorPoint(center, outerRadius, startAngle, snapMode, grid),
    snapSectorPoint(center, innerRadius, startAngle, snapMode, grid),
  };
}

std::optional<mdl::Brush> createCylinderSectorBrush(
  const mdl::BrushBuilder& builder,
  const vm::vec2d& center,
  const double innerRadius,
  const double outerRadius,
  const double startAngle,
  const double endAngle,
  const double minZ,
  const double maxZ,
  const std::string& material,
  QString& error,
  const double grid = 1.0,
  const SectorSnapMode snapMode = SectorSnapMode::Grid)
{
  auto polygon = sectorPolygon(
    center, innerRadius, outerRadius, startAngle, endAngle, snapMode, grid, error);
  if (!polygon)
  {
    return std::nullopt;
  }
  return createPrismBrush(
    builder,
    std::move(*polygon),
    snapToGrid(minZ, grid),
    snapToGrid(maxZ, grid),
    material,
    error);
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

struct SpiralStairsParams
{
  vm::vec3d center = vm::vec3d{0, 0, 0};
  double innerRadius = 32.0;
  double outerRadius = 128.0;
  size_t steps = 24;
  double stepHeight = 8.0;
  double startAngle = 0.0;
  double turnDegrees = 360.0;
  bool clockwise = false;
  double baseZ = 0.0;
  bool column = true;
  bool landing = true;
};

std::optional<SpiralStairsParams> spiralStairsParamsFromJson(
  const QJsonObject& params, QString& error)
{
  auto result = SpiralStairsParams{};
  if (params.contains("center"))
  {
    const auto center = mcpVec3FromJson(params, "center", error);
    if (!center)
    {
      return std::nullopt;
    }
    result.center = *center;
  }
  result.innerRadius = optionalDouble(params, "innerRadius", result.innerRadius);
  result.outerRadius = optionalDouble(params, "outerRadius", result.outerRadius);
  result.steps = optionalSize(params, "steps", result.steps);
  result.stepHeight = optionalDouble(params, "stepHeight", result.stepHeight);
  result.startAngle = optionalDouble(params, "startAngle", result.startAngle);
  result.turnDegrees = optionalDouble(params, "turnDegrees", result.turnDegrees);
  result.clockwise = mcpOptionalBool(params, "clockwise", result.clockwise);
  result.baseZ = optionalDouble(params, "baseZ", result.center.z());
  result.column = mcpOptionalBool(params, "column", result.column);
  result.landing = mcpOptionalBool(params, "landing", result.landing);

  if (!finitePositive(result.innerRadius) || !finitePositive(result.outerRadius))
  {
    error = "innerRadius and outerRadius must be greater than zero";
    return std::nullopt;
  }
  if (result.innerRadius >= result.outerRadius)
  {
    error = "innerRadius must be smaller than outerRadius";
    return std::nullopt;
  }
  if (result.steps < 3 || result.steps > 128)
  {
    error = "steps must be between 3 and 128";
    return std::nullopt;
  }
  if (!finitePositive(result.stepHeight))
  {
    error = "stepHeight must be greater than zero";
    return std::nullopt;
  }
  if (!std::isfinite(result.startAngle) || !std::isfinite(result.turnDegrees))
  {
    error = "startAngle and turnDegrees must be finite";
    return std::nullopt;
  }
  if (
    std::abs(result.turnDegrees) <= GeometryEpsilon
    || std::abs(result.turnDegrees) > 360.0)
  {
    error = "turnDegrees must be greater than zero and at most 360 degrees";
    return std::nullopt;
  }
  const auto stepAngle = std::abs(result.turnDegrees) / static_cast<double>(result.steps);
  if (stepAngle > 180.0)
  {
    error = "per-step angle must be at most 180 degrees";
    return std::nullopt;
  }
  return result;
}

double spiralDirection(const SpiralStairsParams& params)
{
  return params.clockwise ? -1.0 : 1.0;
}

double spiralStepAngle(const SpiralStairsParams& params)
{
  return spiralDirection(params) * std::abs(params.turnDegrees)
         / static_cast<double>(params.steps);
}

std::optional<std::vector<mdl::Brush>> createSpiralStairBrushes(
  const mdl::BrushBuilder& builder,
  const SpiralStairsParams& params,
  const std::string& material,
  QString& error)
{
  auto brushes = std::vector<mdl::Brush>{};
  brushes.reserve(params.steps + (params.column ? 1u : 0u) + (params.landing ? 1u : 0u));

  const auto center2D = vm::vec2d{params.center.x(), params.center.y()};
  const auto stepAngle = spiralStepAngle(params);
  auto innerPoints = std::vector<vm::vec2d>{};
  auto outerPoints = std::vector<vm::vec2d>{};
  innerPoints.reserve(params.steps + 1u);
  outerPoints.reserve(params.steps + 1u);
  for (size_t i = 0; i <= params.steps; ++i)
  {
    const auto angle = params.startAngle + stepAngle * static_cast<double>(i);
    innerPoints.push_back(snapToGrid(polarPoint(center2D, params.innerRadius, angle)));
    outerPoints.push_back(snapToGrid(polarPoint(center2D, params.outerRadius, angle)));
  }

  for (size_t i = 0; i < params.steps; ++i)
  {
    auto brush = createPrismBrush(
      builder,
      std::vector<vm::vec2d>{
        innerPoints[i], outerPoints[i], outerPoints[i + 1u], innerPoints[i + 1u]},
      params.baseZ,
      params.baseZ + params.stepHeight * static_cast<double>(i + 1),
      material,
      error);
    if (!brush)
    {
      return std::nullopt;
    }
    brushes.push_back(std::move(*brush));
  }

  if (params.column)
  {
    const auto columnSides = std::max<size_t>(16, params.steps);
    auto points = std::vector<vm::vec2d>{};
    points.reserve(columnSides);
    for (size_t i = 0; i < columnSides; ++i)
    {
      if (columnSides == params.steps)
      {
        points.push_back(innerPoints[i]);
      }
      else
      {
        points.push_back(snapToGrid(polarPoint(
          center2D,
          params.innerRadius,
          params.startAngle
            + 360.0 * static_cast<double>(i) / static_cast<double>(columnSides))));
      }
    }
    auto brush = createPrismBrush(
      builder,
      std::move(points),
      params.baseZ,
      params.baseZ + params.stepHeight * static_cast<double>(params.steps),
      material,
      error);
    if (!brush)
    {
      return std::nullopt;
    }
    brushes.push_back(std::move(*brush));
  }

  if (params.landing)
  {
    const auto innerA = innerPoints.back();
    const auto outerA = outerPoints.back();
    auto radial = outerA - innerA;
    if (vm::is_zero(radial, GeometryEpsilon))
    {
      error = "Could not determine spiral stair landing direction";
      return std::nullopt;
    }
    radial = vm::normalize(radial);
    const auto tangentSign = stepAngle >= 0.0 ? 1.0 : -1.0;
    const auto tangent = vm::vec2d{-radial.y() * tangentSign, radial.x() * tangentSign};
    const auto width = params.outerRadius - params.innerRadius;
    const auto length = std::max(width, 64.0);
    const auto innerB = snapToGrid(innerA + tangent * length);
    const auto outerB = snapToGrid(outerA + tangent * length);
    auto brush = createPrismBrush(
      builder,
      std::vector<vm::vec2d>{innerA, outerA, outerB, innerB},
      params.baseZ + params.stepHeight * static_cast<double>(params.steps),
      params.baseZ + params.stepHeight * static_cast<double>(params.steps + 1),
      material,
      error);
    if (!brush)
    {
      return std::nullopt;
    }
    brushes.push_back(std::move(*brush));
  }

  return brushes;
}

QJsonObject spiralValidationJson(
  const SpiralStairsParams& params,
  const size_t brushCount,
  const int invalidBrushCount = 0)
{
  return QJsonObject{
    {"valid", invalidBrushCount == 0},
    {"gapCount", 0},
    {"radiusMismatch", false},
    {"columnFits", params.column},
    {"landingConnected", params.landing},
    {"invalidBrushCount", invalidBrushCount},
    {"expectedSteps", static_cast<int>(params.steps)},
    {"brushCount", static_cast<int>(brushCount)},
    {"innerRadius", params.innerRadius},
    {"outerRadius", params.outerRadius},
    {"stepHeight", params.stepHeight},
    {"turnDegrees", params.turnDegrees},
  };
}

struct SpiralGeometryChecks
{
  int gapCount = 0;
  bool radiusMismatch = false;
  bool columnFits = true;
  bool landingConnected = true;
  bool zProgression = true;
  QJsonArray errors;
};

double distance2D(const vm::vec3d& point, const vm::vec2d& center)
{
  const auto x = point.x() - center.x();
  const auto y = point.y() - center.y();
  return std::sqrt(x * x + y * y);
}

bool pointRadiusWithin(
  const vm::vec3d& point,
  const vm::vec2d& center,
  const double minRadius,
  const double maxRadius,
  const double epsilon)
{
  const auto radius = distance2D(point, center);
  return radius >= minRadius - epsilon && radius <= maxRadius + epsilon;
}

SpiralGeometryChecks analyzeSpiralGeometry(
  const std::vector<mdl::BrushNode*>& brushes, const SpiralStairsParams& params)
{
  auto checks = SpiralGeometryChecks{};
  const auto center2D = vm::vec2d{params.center.x(), params.center.y()};
  const auto radiusEpsilon = 2.0;
  const auto zEpsilon = 0.01;

  if (brushes.size() < params.steps)
  {
    checks.errors.push_back("Not enough brush nodes to contain all spiral steps");
    checks.gapCount = static_cast<int>(params.steps - brushes.size());
    checks.zProgression = false;
    return checks;
  }

  for (size_t i = 0; i < params.steps; ++i)
  {
    const auto& brush = brushes[i]->brush();
    const auto& bounds = brush.bounds();
    const auto expectedMaxZ =
      params.baseZ + params.stepHeight * static_cast<double>(i + 1);
    if (
      !nearlyEqual(bounds.min.z(), params.baseZ, zEpsilon)
      || !nearlyEqual(bounds.max.z(), expectedMaxZ, zEpsilon))
    {
      checks.zProgression = false;
      checks.errors.push_back(QString{"Step %1 has z range [%2,%3], expected [%4,%5]"}
                                .arg(static_cast<int>(i))
                                .arg(bounds.min.z())
                                .arg(bounds.max.z())
                                .arg(params.baseZ)
                                .arg(expectedMaxZ));
    }

    const auto vertices = brush.vertexPositions();
    if (!std::ranges::all_of(vertices, [&](const auto& vertex) {
          return pointRadiusWithin(
            vertex, center2D, params.innerRadius, params.outerRadius, radiusEpsilon);
        }))
    {
      checks.radiusMismatch = true;
      checks.errors.push_back(
        QString{"Step %1 has vertices outside expected inner/outer radii"}.arg(
          static_cast<int>(i)));
    }
  }

  auto nextBrushIndex = params.steps;
  if (params.column)
  {
    if (brushes.size() <= nextBrushIndex)
    {
      checks.columnFits = false;
      checks.errors.push_back("Missing center column brush");
    }
    else
    {
      const auto& brush = brushes[nextBrushIndex]->brush();
      const auto& bounds = brush.bounds();
      if (
        !nearlyEqual(bounds.min.z(), params.baseZ, zEpsilon)
        || !nearlyEqual(
          bounds.max.z(),
          params.baseZ + params.stepHeight * static_cast<double>(params.steps),
          zEpsilon))
      {
        checks.columnFits = false;
        checks.errors.push_back(
          "Center column height does not match spiral stair height");
      }

      const auto vertices = brush.vertexPositions();
      if (!std::ranges::all_of(vertices, [&](const auto& vertex) {
            return pointRadiusWithin(
              vertex, center2D, 0.0, params.innerRadius, radiusEpsilon);
          }))
      {
        checks.columnFits = false;
        checks.errors.push_back("Center column vertices exceed inner radius");
      }
      ++nextBrushIndex;
    }
  }

  if (params.landing)
  {
    if (brushes.size() <= nextBrushIndex)
    {
      checks.landingConnected = false;
      checks.errors.push_back("Missing spiral stair landing brush");
    }
    else
    {
      const auto& landingBounds = brushes[nextBrushIndex]->brush().bounds();
      const auto expectedMinZ =
        params.baseZ + params.stepHeight * static_cast<double>(params.steps);
      const auto expectedMaxZ =
        params.baseZ + params.stepHeight * static_cast<double>(params.steps + 1);
      if (
        !nearlyEqual(landingBounds.min.z(), expectedMinZ, zEpsilon)
        || !nearlyEqual(landingBounds.max.z(), expectedMaxZ, zEpsilon))
      {
        checks.landingConnected = false;
        checks.errors.push_back(QString{"Landing has z range [%1,%2], expected [%3,%4]"}
                                  .arg(landingBounds.min.z())
                                  .arg(landingBounds.max.z())
                                  .arg(expectedMinZ)
                                  .arg(expectedMaxZ));
      }
    }
  }

  return checks;
}

std::optional<QJsonObject> validateBlockoutParams(
  const QString& type, const QJsonObject& params, QString& error)
{
  if (type == "spiral_stairs")
  {
    const auto spiralParams = spiralStairsParamsFromJson(params, error);
    if (!spiralParams)
    {
      return std::nullopt;
    }
    return QJsonObject{
      {"valid", true},
      {"type", type},
      {"steps", static_cast<int>(spiralParams->steps)},
      {"innerRadius", spiralParams->innerRadius},
      {"outerRadius", spiralParams->outerRadius},
      {"stepHeight", spiralParams->stepHeight},
      {"turnDegrees", spiralParams->turnDegrees},
    };
  }

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

void deleteNodes(std::vector<mdl::Node*>& nodes)
{
  for (auto* node : nodes)
  {
    delete node;
  }
  nodes.clear();
}

QJsonObject batchValidationJson(
  const bool valid,
  const QJsonArray& errors,
  const int operationCount,
  const int brushCount)
{
  return QJsonObject{
    {"valid", valid},
    {"errors", errors},
    {"operationCount", operationCount},
    {"brushCount", brushCount},
  };
}

QJsonObject batchOperationPreviewJson(const QJsonObject& operation)
{
  auto result = QJsonObject{};
  result.insert("type", operation.value("type").toString());

  for (const auto& key :
       {"min",
        "max",
        "points2d",
        "points",
        "center",
        "minZ",
        "maxZ",
        "axis",
        "sides",
        "width",
        "height",
        "segments",
        "startAngle",
        "endAngle",
        "turnDegrees"})
  {
    if (operation.contains(key))
    {
      result.insert(key, operation.value(key));
    }
  }

  return result;
}

QJsonArray translatedVec3Array(
  const QJsonValue& value, const vm::vec3d& delta, const QString& key, QString& error)
{
  const auto point = mcpVec3FromJsonValue(value, key, error);
  if (!point)
  {
    return {};
  }
  return vecToJson(*point + delta);
}

QJsonArray translatedVec2Array(
  const QJsonValue& value, const vm::vec2d& delta, const QString& key, QString& error)
{
  const auto point = mcpVec2FromJsonValue(value, key, error);
  if (!point)
  {
    return {};
  }
  return QJsonArray{point->x() + delta.x(), point->y() + delta.y()};
}

QJsonArray translatedVec2PointsArray(
  const QJsonValue& value, const vm::vec2d& delta, const QString& key, QString& error)
{
  if (!value.isArray())
  {
    error = QString{"%1 must be an array of [x,y] points"}.arg(key);
    return {};
  }

  auto result = QJsonArray{};
  const auto points = value.toArray();
  for (auto i = 0; i < points.size(); ++i)
  {
    if (!error.isEmpty())
    {
      return {};
    }
    result.push_back(
      translatedVec2Array(points[i], delta, QString{"%1[%2]"}.arg(key).arg(i), error));
  }
  return result;
}

QJsonArray translatedVec3PointsArray(
  const QJsonValue& value, const vm::vec3d& delta, const QString& key, QString& error)
{
  if (!value.isArray())
  {
    error = QString{"%1 must be an array of [x,y,z] points"}.arg(key);
    return {};
  }

  auto result = QJsonArray{};
  const auto points = value.toArray();
  for (auto i = 0; i < points.size(); ++i)
  {
    if (!error.isEmpty())
    {
      return {};
    }
    result.push_back(
      translatedVec3Array(points[i], delta, QString{"%1[%2]"}.arg(key).arg(i), error));
  }
  return result;
}

std::optional<QJsonObject> translatedBatchOperation(
  const QJsonObject& operation, const vm::vec3d& delta, QString& error)
{
  auto result = operation;
  const auto delta2D = vm::vec2d{delta.x(), delta.y()};

  for (const auto& key : {"min", "max", "center", "doorMin", "doorMax"})
  {
    if (result.contains(key))
    {
      result.insert(key, translatedVec3Array(result.value(key), delta, key, error));
      if (!error.isEmpty())
      {
        return std::nullopt;
      }
    }
  }

  if (result.contains("points2d"))
  {
    result.insert(
      "points2d",
      translatedVec2PointsArray(result.value("points2d"), delta2D, "points2d", error));
    if (!error.isEmpty())
    {
      return std::nullopt;
    }
  }

  if (result.contains("points"))
  {
    result.insert(
      "points",
      translatedVec3PointsArray(result.value("points"), delta, "points", error));
    if (!error.isEmpty())
    {
      return std::nullopt;
    }
  }

  for (const auto& key : {"minZ", "maxZ"})
  {
    if (result.value(key).isDouble())
    {
      result.insert(key, result.value(key).toDouble() + delta.z());
    }
  }

  return result;
}

std::vector<QJsonObject> curvedCorridorOperationsFromParams(const QJsonObject& params)
{
  const auto center = params.value("center").toArray();
  const auto innerRadius = optionalDouble(params, "innerRadius", 128.0);
  const auto outerRadius = optionalDouble(params, "outerRadius", 224.0);
  const auto startAngle = optionalDouble(params, "startAngle", -90.0);
  const auto turnDegrees = optionalDouble(params, "turnDegrees", 180.0);
  const auto height = optionalDouble(params, "height", 128.0);
  const auto segments = std::max<size_t>(1, optionalSize(params, "segments", 12));
  const auto wallThickness = optionalDouble(params, "wallThickness", 16.0);
  const auto floorThickness = optionalDouble(params, "floorThickness", 16.0);
  const auto ceilingThickness = optionalDouble(params, "ceilingThickness", 16.0);
  const auto caps = params.value("caps").toString("none").toLower();
  const auto material = params.value("material").toString();
  const auto snapMode = params.value("snapMode").toString("radial").toLower();

  auto operations = std::vector<QJsonObject>{};
  operations.reserve(segments * 4 + 2);

  const auto step = turnDegrees / static_cast<double>(segments);
  for (size_t i = 0; i < segments; ++i)
  {
    const auto a0 = startAngle + step * static_cast<double>(i);
    const auto a1 = a0 + step;
    const auto addSector =
      [&](const double inner, const double outer, const double minZ, const double maxZ) {
        auto op = QJsonObject{
          {"type", "cylinder_sector"},
          {"center", center},
          {"innerRadius", inner},
          {"outerRadius", outer},
          {"startAngle", a0},
          {"endAngle", a1},
          {"minZ", minZ},
          {"maxZ", maxZ},
          {"snapMode", snapMode},
        };
        if (!material.isEmpty())
        {
          op.insert("material", material);
        }
        operations.push_back(std::move(op));
      };

    addSector(innerRadius, outerRadius, 0.0, floorThickness);
    addSector(innerRadius, outerRadius, height, height + ceilingThickness);
    addSector(innerRadius - wallThickness, innerRadius, floorThickness, height);
    addSector(outerRadius, outerRadius + wallThickness, floorThickness, height);
  }

  const auto addCap = [&](const double angle) {
    const auto half = std::abs(step) / 2.0;
    auto op = QJsonObject{
      {"type", "cylinder_sector"},
      {"center", center},
      {"innerRadius", innerRadius - wallThickness},
      {"outerRadius", outerRadius + wallThickness},
      {"startAngle", angle - half},
      {"endAngle", angle + half},
      {"minZ", 0.0},
      {"maxZ", height + ceilingThickness},
      {"snapMode", snapMode},
    };
    if (!material.isEmpty())
    {
      op.insert("material", material);
    }
    operations.push_back(std::move(op));
  };

  if (caps == "start" || caps == "both")
  {
    addCap(startAngle);
  }
  if (caps == "end" || caps == "both")
  {
    addCap(startAngle + turnDegrees);
  }
  return operations;
}

std::optional<std::vector<std::vector<vm::vec2d>>> pathRibbonPolygonsFromParams(
  const QJsonObject& params, const double grid, QString& error)
{
  const auto points = points2DFromJson(params, "points2d", 2u, error);
  if (!points)
  {
    return std::nullopt;
  }

  if (points->size() < 2)
  {
    error = "points2d must contain at least two centerline points";
    return std::nullopt;
  }

  const auto width = optionalDouble(params, "width", 128.0);
  if (!finitePositive(width))
  {
    error = "width must be greater than zero";
    return std::nullopt;
  }

  auto snappedPoints = snapPointsToGrid(*points, grid);
  auto normals = std::vector<vm::vec2d>{};
  normals.reserve(snappedPoints.size() - 1);
  for (size_t i = 0; i + 1 < snappedPoints.size(); ++i)
  {
    const auto direction = snappedPoints[i + 1] - snappedPoints[i];
    if (vm::is_zero(direction, GeometryEpsilon))
    {
      error = QString{"points2d[%1] and points2d[%2] collapse after snapping"}.arg(i).arg(
        i + 1);
      return std::nullopt;
    }

    const auto tangent = vm::normalize(direction);
    normals.push_back(vm::vec2d{-tangent.y(), tangent.x()});
  }

  auto polygons = std::vector<std::vector<vm::vec2d>>{};
  polygons.reserve(snappedPoints.size() - 1);
  const auto halfWidth = snapToGrid(width * 0.5, grid);
  const auto miterLimit = optionalDouble(params, "miterLimit", 4.0);
  if (!finitePositive(halfWidth))
  {
    error = "width snaps to zero";
    return std::nullopt;
  }
  if (!finitePositive(miterLimit))
  {
    error = "miterLimit must be greater than zero";
    return std::nullopt;
  }

  auto leftOffsets = std::vector<vm::vec2d>{};
  auto rightOffsets = std::vector<vm::vec2d>{};
  leftOffsets.reserve(snappedPoints.size());
  rightOffsets.reserve(snappedPoints.size());
  for (size_t i = 0; i < snappedPoints.size(); ++i)
  {
    auto offset = vm::vec2d{};
    if (i == 0)
    {
      offset = normals.front() * halfWidth;
    }
    else if (i + 1 == snappedPoints.size())
    {
      offset = normals.back() * halfWidth;
    }
    else
    {
      const auto previousNormal = normals[i - 1];
      const auto nextNormal = normals[i];
      auto miter = previousNormal + nextNormal;
      if (vm::is_zero(miter, GeometryEpsilon))
      {
        error = QString{"path_ribbon turn at points2d[%1] is too sharp"}.arg(i);
        return std::nullopt;
      }
      miter = vm::normalize(miter);
      const auto denominator =
        miter.x() * previousNormal.x() + miter.y() * previousNormal.y();
      if (std::abs(denominator) <= GeometryEpsilon)
      {
        error = QString{"path_ribbon turn at points2d[%1] is too sharp"}.arg(i);
        return std::nullopt;
      }
      const auto miterLength = halfWidth / denominator;
      if (std::abs(miterLength) > halfWidth * miterLimit)
      {
        error = QString{"path_ribbon turn at points2d[%1] exceeds miterLimit"}.arg(i);
        return std::nullopt;
      }
      offset = miter * miterLength;
    }
    leftOffsets.push_back(snapToGrid(snappedPoints[i] + offset, grid));
    rightOffsets.push_back(snapToGrid(snappedPoints[i] - offset, grid));
  }

  for (size_t i = 0; i + 1 < snappedPoints.size(); ++i)
  {
    auto polygon = std::vector<vm::vec2d>{
      leftOffsets[i],
      leftOffsets[i + 1],
      rightOffsets[i + 1],
      rightOffsets[i],
    };

    polygon = snapPointsToGrid(std::move(polygon), grid);
    if (!isStrictlyConvexPolygon(polygon))
    {
      error = QString{"path_ribbon segment %1 is not a convex brush footprint"}.arg(i);
      return std::nullopt;
    }
    polygons.push_back(std::move(polygon));
  }

  if (polygons.empty())
  {
    error = "path_ribbon generated no segments";
    return std::nullopt;
  }
  return polygons;
}

std::vector<mdl::Node*> createPathRibbonNodes(
  const mdl::BrushBuilder& builder,
  const QJsonObject& operation,
  const double grid,
  const std::string& material,
  QString& error)
{
  const auto polygons = pathRibbonPolygonsFromParams(operation, grid, error);
  if (!polygons)
  {
    return {};
  }

  const auto minZ = snapToGrid(optionalDouble(operation, "minZ", 0.0), grid);
  const auto maxZ = snapToGrid(optionalDouble(operation, "maxZ", 16.0), grid);
  if (!(std::isfinite(minZ) && std::isfinite(maxZ)) || minZ >= maxZ)
  {
    error = "minZ must be smaller than maxZ";
    return {};
  }

  auto result = std::vector<mdl::Node*>{};
  result.reserve(polygons->size());
  for (const auto& polygon : *polygons)
  {
    auto brush = createPrismBrush(builder, polygon, minZ, maxZ, material, error);
    if (!brush)
    {
      deleteNodes(result);
      return {};
    }
    result.push_back(new mdl::BrushNode{std::move(*brush)});
  }
  return result;
}

bool validateCurvedCorridorParams(const QJsonObject& params, QString& error)
{
  const auto innerRadius = optionalDouble(params, "innerRadius", 128.0);
  const auto outerRadius = optionalDouble(params, "outerRadius", 224.0);
  const auto turnDegrees = optionalDouble(params, "turnDegrees", 180.0);
  const auto segments = optionalSize(params, "segments", 12);
  const auto height = optionalDouble(params, "height", 128.0);
  const auto wallThickness = optionalDouble(params, "wallThickness", 16.0);
  const auto floorThickness = optionalDouble(params, "floorThickness", 16.0);
  const auto ceilingThickness = optionalDouble(params, "ceilingThickness", 16.0);
  const auto caps = params.value("caps").toString("none").toLower();
  const auto snapMode = params.value("snapMode").toString("radial").toLower();

  if (!params.value("center").isArray())
  {
    error = "center must be provided for curved corridor";
    return false;
  }
  if (
    !finitePositive(innerRadius) || !finitePositive(outerRadius)
    || innerRadius >= outerRadius)
  {
    error = "innerRadius must be greater than zero and smaller than outerRadius";
    return false;
  }
  if (
    !finitePositive(height) || !finitePositive(wallThickness)
    || !finitePositive(floorThickness) || !finitePositive(ceilingThickness))
  {
    error = "height and thickness values must be greater than zero";
    return false;
  }
  if (innerRadius <= wallThickness)
  {
    error = "innerRadius must be larger than wallThickness";
    return false;
  }
  if (segments < 1 || segments > 128)
  {
    error = "segments must be between 1 and 128";
    return false;
  }
  if (
    !std::isfinite(turnDegrees) || std::abs(turnDegrees) <= GeometryEpsilon
    || std::abs(turnDegrees) > 360.0)
  {
    error = "turnDegrees must be greater than zero and at most 360 degrees";
    return false;
  }
  if (std::abs(turnDegrees) / static_cast<double>(segments) > 180.0)
  {
    error = "segments too low; use enough segments so each sector is <= 180 degrees";
    return false;
  }
  if (caps != "none" && caps != "start" && caps != "end" && caps != "both")
  {
    error = "caps must be none, start, end, or both";
    return false;
  }
  if (snapMode != "grid" && snapMode != "radial" && snapMode != "none")
  {
    error = "snapMode must be grid, radial, or none";
    return false;
  }
  return true;
}

std::vector<mdl::Node*> compileBatchOperation(
  const mdl::Map& map,
  const mdl::BrushBuilder& builder,
  const QJsonObject& operation,
  const std::string& defaultMaterial,
  const double grid,
  QString& error)
{
  const auto type = operation.value("type").toString().trimmed().toLower();
  const auto material = mcpOptionalString(operation, "material", defaultMaterial);

  if (type == "repeat_translate")
  {
    if (!operation.value("operation").isObject())
    {
      error = "repeat_translate requires child operation object";
      return {};
    }
    const auto offset = mcpVec3FromJson(operation, "offset", error);
    if (!offset)
    {
      return {};
    }
    const auto count = optionalSize(operation, "count", 0);
    if (count == 0 || count > 256)
    {
      error = "repeat_translate count must be between 1 and 256";
      return {};
    }

    auto result = std::vector<mdl::Node*>{};
    auto childOperation = operation.value("operation").toObject();
    if (childOperation.value("type").toString().trimmed().toLower() == "repeat_translate")
    {
      error = "repeat_translate child operation cannot be another repeat_translate";
      return {};
    }
    for (size_t i = 0; i < count; ++i)
    {
      const auto translatedOperation =
        translatedBatchOperation(childOperation, *offset * static_cast<double>(i), error);
      if (!translatedOperation)
      {
        deleteNodes(result);
        return {};
      }

      auto childNodes =
        compileBatchOperation(map, builder, *translatedOperation, material, grid, error);
      if (!error.isEmpty())
      {
        deleteNodes(result);
        deleteNodes(childNodes);
        return {};
      }
      result.insert(result.end(), childNodes.begin(), childNodes.end());
    }
    return result;
  }

  if (type == "curved_corridor")
  {
    if (!validateCurvedCorridorParams(operation, error))
    {
      return {};
    }
    auto result = std::vector<mdl::Node*>{};
    for (const auto& childOperation : curvedCorridorOperationsFromParams(operation))
    {
      auto childNodes =
        compileBatchOperation(map, builder, childOperation, material, grid, error);
      if (!error.isEmpty())
      {
        deleteNodes(result);
        deleteNodes(childNodes);
        return {};
      }
      result.insert(result.end(), childNodes.begin(), childNodes.end());
    }
    return result;
  }

  if (type == "path_ribbon")
  {
    return createPathRibbonNodes(builder, operation, grid, material, error);
  }

  if (type == "box" || type == "cover")
  {
    const auto bounds = boundsFromJson(operation, error);
    if (!bounds)
    {
      return {};
    }
    const auto snappedBounds = snapBoundsToGrid(*bounds, grid, error);
    if (!snappedBounds)
    {
      return {};
    }
    return brushNodesFromBounds(builder, {*snappedBounds}, material, error);
  }

  if (type == "cylinder")
  {
    const auto bounds = boundsFromJson(operation, error);
    if (!bounds)
    {
      return {};
    }
    const auto snappedBounds = snapBoundsToGrid(*bounds, grid, error);
    if (!snappedBounds)
    {
      return {};
    }
    const auto axis = axisFromJson(operation, "axis", vm::axis::z, error);
    if (!axis)
    {
      return {};
    }
    const auto sides =
      std::clamp(optionalSize(operation, "sides", 16), size_t{3}, size_t{128});
    auto brush = builder.createCylinder(
      *snappedBounds, mdl::EdgeAlignedCircle{sides}, *axis, material);
    if (brush.is_error())
    {
      error = "Could not create cylinder brush from the given bounds";
      return {};
    }
    return {new mdl::BrushNode{std::move(brush.value())}};
  }

  if (type == "room" || type == "corridor")
  {
    const auto bounds = boundsFromJson(operation, error);
    if (!bounds)
    {
      return {};
    }
    const auto snappedBounds = snapBoundsToGrid(*bounds, grid, error);
    if (!snappedBounds)
    {
      return {};
    }
    const auto shellBounds = roomShellBounds(
      *snappedBounds,
      snapToGrid(optionalDouble(operation, "thickness", 16.0), grid),
      error);
    if (!shellBounds)
    {
      return {};
    }
    return brushNodesFromBounds(builder, *shellBounds, material, error);
  }

  if (type == "sky_shell")
  {
    const auto bounds = boundsFromJson(operation, error);
    if (!bounds)
    {
      return {};
    }
    const auto snappedBounds = snapBoundsToGrid(*bounds, grid, error);
    if (!snappedBounds)
    {
      return {};
    }
    return brushNodesFromBounds(
      builder,
      skyShellBounds(
        *snappedBounds, snapToGrid(optionalDouble(operation, "thickness", 16.0), grid)),
      mcpOptionalString(operation, "material", "sky"),
      error);
  }

  if (type == "stairs")
  {
    const auto bounds = boundsFromJson(operation, error);
    if (!bounds)
    {
      return {};
    }
    const auto snappedBounds = snapBoundsToGrid(*bounds, grid, error);
    if (!snappedBounds)
    {
      return {};
    }
    const auto axis = axisFromJson(operation, "axis", vm::axis::x, error);
    if (!axis)
    {
      return {};
    }
    const auto stairBoxes =
      stairsBounds(*snappedBounds, optionalSize(operation, "steps", 8), *axis, error);
    if (!stairBoxes)
    {
      return {};
    }
    return brushNodesFromBounds(builder, *stairBoxes, material, error);
  }

  if (type == "ramp")
  {
    const auto bounds = boundsFromJson(operation, error);
    if (!bounds)
    {
      return {};
    }
    const auto snappedBounds = snapBoundsToGrid(*bounds, grid, error);
    if (!snappedBounds)
    {
      return {};
    }
    const auto axis = axisFromJson(operation, "axis", vm::axis::x, error);
    if (!axis)
    {
      return {};
    }
    auto brush = createWedgeBrush(builder, *snappedBounds, *axis, material);
    if (brush.is_error())
    {
      error = "Could not create ramp brush";
      return {};
    }
    return {new mdl::BrushNode{std::move(brush.value())}};
  }

  if (type == "doorway")
  {
    const auto bounds = boundsFromJson(operation, error);
    if (!bounds)
    {
      return {};
    }
    const auto snappedBounds = snapBoundsToGrid(*bounds, grid, error);
    if (!snappedBounds)
    {
      return {};
    }
    const auto door = boundsFromJson(operation, "doorMin", "doorMax", error);
    if (!door)
    {
      return {};
    }
    const auto snappedDoor = snapBoundsToGrid(*door, grid, error);
    if (!snappedDoor)
    {
      return {};
    }
    const auto segments = doorwayBounds(*snappedBounds, *snappedDoor, error);
    if (!segments)
    {
      return {};
    }
    return brushNodesFromBounds(builder, *segments, material, error);
  }

  if (type == "prism")
  {
    auto points = points2DFromJson(operation, "points2d", error);
    if (!points)
    {
      return {};
    }
    auto brush = createPrismBrush(
      builder,
      snapPointsToGrid(*points, grid),
      snapToGrid(optionalDouble(operation, "minZ", 0.0), grid),
      snapToGrid(optionalDouble(operation, "maxZ", 128.0), grid),
      material,
      error);
    if (!brush)
    {
      return {};
    }
    return {new mdl::BrushNode{std::move(*brush)}};
  }

  if (type == "polyhedron")
  {
    auto points = points3DFromJson(operation, "points", error);
    if (!points)
    {
      return {};
    }
    auto brush = builder.createBrush(snapPointsToGrid(*points, grid), material);
    if (brush.is_error())
    {
      error = "Could not create convex polyhedron brush from points";
      return {};
    }
    return {new mdl::BrushNode{std::move(brush.value())}};
  }

  if (type == "cylinder_sector")
  {
    const auto center = mcpVec3FromJson(operation, "center", error);
    if (!center)
    {
      return {};
    }
    const auto snapMode =
      sectorSnapModeFromJson(operation, "snapMode", SectorSnapMode::Grid, error);
    if (!snapMode)
    {
      return {};
    }
    auto brush = createCylinderSectorBrush(
      builder,
      vm::vec2d{center->x(), center->y()},
      optionalDouble(operation, "innerRadius", 128.0),
      optionalDouble(operation, "outerRadius", 224.0),
      optionalDouble(operation, "startAngle", 0.0),
      optionalDouble(operation, "endAngle", 15.0),
      optionalDouble(operation, "minZ", 0.0),
      optionalDouble(operation, "maxZ", 128.0),
      material,
      error,
      grid,
      *snapMode);
    if (!brush)
    {
      return {};
    }
    return {new mdl::BrushNode{std::move(*brush)}};
  }

  error = QString{"Unsupported batch operation type: %1"}.arg(type);
  return {};
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

  if (type == "prism")
  {
    auto points = points2DFromJson(params, "points2d", error);
    if (!points)
    {
      return std::nullopt;
    }
    auto brush = createPrismBrush(
      builder,
      std::move(*points),
      optionalDouble(params, "minZ", 0.0),
      optionalDouble(params, "maxZ", 64.0),
      material,
      error);
    if (!brush)
    {
      return std::nullopt;
    }
    return std::vector<mdl::Brush>{std::move(*brush)};
  }

  if (type == "cylinder_sector")
  {
    auto center = mcpVec3FromJson(params, "center", error);
    if (!center)
    {
      return std::nullopt;
    }
    const auto snapMode =
      sectorSnapModeFromJson(params, "snapMode", SectorSnapMode::Grid, error);
    if (!snapMode)
    {
      return std::nullopt;
    }
    auto brush = createCylinderSectorBrush(
      builder,
      vm::vec2d{center->x(), center->y()},
      optionalDouble(params, "innerRadius", 32.0),
      optionalDouble(params, "outerRadius", 128.0),
      optionalDouble(params, "startAngle", 0.0),
      optionalDouble(params, "endAngle", 15.0),
      optionalDouble(params, "minZ", center->z()),
      optionalDouble(params, "maxZ", center->z() + 8.0),
      material,
      error,
      optionalDouble(params, "grid", 1.0),
      *snapMode);
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

  if (toolName == "blockout_create_spiral_stairs")
  {
    return blockoutCreateSpiralStairsForMapResult(
      mapWindow->document().map(), params, history, nextOperationIndex);
  }

  auto error = QString{};
  auto& map = mapWindow->document().map();
  const auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
  auto material = materialNameFromParams(map, params);
  auto nodes = std::vector<mdl::Node*>{};
  auto transactionName = QString{};

  const auto bounds = boundsFromJson(params, error);
  if (!bounds)
  {
    return invalidParamsFailure(error);
  }

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

  const auto brushCount = static_cast<int>(nodes.size());
  const auto generatedBounds = boundsForNodes(nodes);
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
  result.insert("brushCount", brushCount);
  result.insert("material", QString::fromStdString(material));
  result.insert(
    "materials",
    stringListToJsonArray(brushMaterialsForObjectIds(map, *changedObjectIds)));
  result.insert("bounds", boundsToJson(generatedBounds));
  applyDetailLevel(result, *changedObjectIds, params.value("detail").toString("summary"));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult blockoutCreateSpiralStairsForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto error = QString{};
  const auto spiralParams = spiralStairsParamsFromJson(params, error);
  if (!spiralParams)
  {
    return invalidParamsFailure(error);
  }

  const auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
  auto material = materialNameFromParams(map, params);
  auto brushes = createSpiralStairBrushes(builder, *spiralParams, material, error);
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

  const auto brushCount = static_cast<int>(nodes.size());
  const auto bounds = boundsForNodes(nodes);
  const auto transactionName = QString{"MCP: Blockout spiral stairs"};
  const auto changedObjectIds = addNodesWithTransaction(
    map, transactionName, nodes, mcpOptionalBool(params, "select", true));
  if (!changedObjectIds)
  {
    for (auto* node : nodes)
    {
      delete node;
    }
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not add spiral stair brushes");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history,
    nextOperationIndex,
    "blockout_create_spiral_stairs",
    transactionName,
    *changedObjectIds,
    result);
  result.insert("brushCount", brushCount);
  result.insert("material", QString::fromStdString(material));
  result.insert(
    "materials",
    stringListToJsonArray(brushMaterialsForObjectIds(map, *changedObjectIds)));
  result.insert("validation", spiralValidationJson(*spiralParams, brushCount));
  result.insert("bounds", boundsToJson(bounds));
  applyDetailLevel(result, *changedObjectIds, params.value("detail").toString("summary"));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult blockoutCreateBatchResult(
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

  return blockoutCreateBatchForMapResult(
    mapWindow->document().map(), toolName, params, history, nextOperationIndex);
}

McpBridgeToolResult createBoxesBatchResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  const auto boxesValue = params.value("boxes");
  if (!boxesValue.isArray())
  {
    return invalidParamsFailure("brush_create_boxes_batch requires boxes array");
  }

  const auto boxes = boxesValue.toArray();
  if (boxes.isEmpty())
  {
    return invalidParamsFailure("boxes must not be empty");
  }

  auto operations = QJsonArray{};
  for (auto i = 0; i < boxes.size(); ++i)
  {
    if (!boxes[i].isObject())
    {
      return invalidParamsFailure(QString{"boxes[%1] must be an object"}.arg(i));
    }
    auto operation = boxes[i].toObject();
    operation.insert("type", "box");
    operations.push_back(operation);
  }

  auto batchParams = QJsonObject{
    {"name", params.value("name").toString("MCP: Create box brush batch")},
    {"operations", operations},
    {"select", params.value("select").toBool(true)},
    {"detail", params.value("detail").toString("summary")},
  };
  if (params.contains("grid"))
  {
    batchParams.insert("grid", params.value("grid"));
  }
  if (params.contains("material"))
  {
    batchParams.insert("material", params.value("material"));
  }

  return blockoutCreateBatchResult(
    appController, toolName, batchParams, history, nextOperationIndex);
}

McpBridgeToolResult blockoutCreateBatchForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto batchParams = params;
  if (toolName == "blockout_create_curved_corridor")
  {
    auto operation = params;
    operation.insert("type", "curved_corridor");
    batchParams = QJsonObject{
      {"name", "MCP: Blockout curved corridor"},
      {"operations", QJsonArray{operation}},
      {"select", params.value("select").toBool(true)},
      {"detail", params.value("detail").toString("summary")},
      {"grid", params.value("grid").toDouble(1.0)},
    };
  }

  const auto operationsValue = batchParams.value("operations");
  if (!operationsValue.isArray())
  {
    return invalidParamsFailure("blockout_create_batch requires operations array");
  }
  const auto operations = operationsValue.toArray();
  if (operations.isEmpty())
  {
    return invalidParamsFailure("operations must not be empty");
  }

  const auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
  const auto defaultMaterial = materialNameFromParams(map, batchParams);
  const auto grid = optionalDouble(batchParams, "grid", 1.0);
  if (!finitePositive(grid))
  {
    return invalidParamsFailure("grid must be greater than zero");
  }

  auto nodes = std::vector<mdl::Node*>{};
  auto errors = QJsonArray{};
  auto failedOperationIndex = -1;
  auto failedOperationType = QString{};
  auto failedOperationPreview = QJsonObject{};
  for (auto i = 0; i < operations.size(); ++i)
  {
    if (!operations[i].isObject())
    {
      errors.push_back(QString{"operations[%1] must be an object"}.arg(i));
      failedOperationIndex = i;
      break;
    }

    auto error = QString{};
    const auto operation = operations[i].toObject();
    auto operationNodes =
      compileBatchOperation(map, builder, operation, defaultMaterial, grid, error);
    if (!error.isEmpty() || operationNodes.empty())
    {
      errors.push_back(
        error.isEmpty() ? QString{"operations[%1] generated no brushes"}.arg(i)
                        : QString{"operations[%1]: %2"}.arg(i).arg(error));
      failedOperationIndex = i;
      failedOperationType = operation.value("type").toString().trimmed().toLower();
      failedOperationPreview = batchOperationPreviewJson(operation);
      deleteNodes(operationNodes);
      break;
    }

    nodes.insert(nodes.end(), operationNodes.begin(), operationNodes.end());
  }

  if (!errors.isEmpty())
  {
    auto validation = batchValidationJson(
      false, errors, operations.size(), static_cast<int>(nodes.size()));
    validation.insert(
      "compiledOperationCount", failedOperationIndex < 0 ? 0 : failedOperationIndex);
    validation.insert("compiledBrushCount", static_cast<int>(nodes.size()));
    if (failedOperationIndex >= 0)
    {
      validation.insert("failedOperationIndex", failedOperationIndex);
    }
    if (!failedOperationType.isEmpty())
    {
      validation.insert("failedOperationType", failedOperationType);
    }
    if (!failedOperationPreview.isEmpty())
    {
      validation.insert("failedOperationPreview", failedOperationPreview);
    }

    deleteNodes(nodes);
    return McpBridgeToolResult::success(QJsonObject{
      {"valid", false},
      {"validation", validation},
    });
  }

  const auto transactionName =
    batchParams.value("name").toString("MCP: Blockout batch").trimmed();
  const auto brushCount = static_cast<int>(nodes.size());
  const auto bounds = boundsForNodes(nodes);
  const auto fullResults = pendingNodeSummariesJson(nodes);
  const auto validation =
    batchValidationJson(true, {}, operations.size(), static_cast<int>(nodes.size()));
  const auto changedObjectIds = addNodesWithTransaction(
    map,
    transactionName.isEmpty() ? QString{"MCP: Blockout batch"} : transactionName,
    nodes,
    batchParams.value("select").toBool(true));
  if (!changedObjectIds)
  {
    deleteNodes(nodes);
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not add blockout batch brushes");
  }

  auto result = QJsonObject{};
  auto detailObject = QJsonObject{
    {"input", batchParams},
    {"grid", grid},
    {"validation", validation},
    {"results", fullResults},
  };
  mcpRecordOperation(
    history,
    nextOperationIndex,
    toolName,
    transactionName.isEmpty() ? QString{"MCP: Blockout batch"} : transactionName,
    *changedObjectIds,
    result,
    detailObject);
  result.insert("brushCount", brushCount);
  result.insert("bounds", boundsToJson(bounds));
  result.insert("validation", validation);
  result.insert("material", QString::fromStdString(defaultMaterial));
  result.insert(
    "materials",
    stringListToJsonArray(brushMaterialsForObjectIds(map, *changedObjectIds)));
  result.insert("grid", grid);
  applyDetailLevel(
    result,
    *changedObjectIds,
    batchParams.value("detail").toString("summary"),
    fullResults);
  if (
    !history.empty()
    && history.back().operationId == result.value("operationId").toString())
  {
    history.back().setSummary(result);
  }
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
       brushTypeJson("prism", true, "Single convex vertical prism from 2D points."),
       brushTypeJson("cylinder_sector", true, "Single convex annular sector brush."),
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

  const auto bounds = boundsForNodes(nodes);
  auto brushJson = pendingNodeSummariesJson(nodes);
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
  result.insert("type", type);
  result.insert("brushCount", brushJson.size());
  result.insert("bounds", boundsToJson(bounds));
  applyDetailLevel(
    result, *changedObjectIds, params.value("detail").toString("summary"), brushJson);
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult geometryAnalyzeSelectionResult(
  mdl::Map& map, const QJsonObject& params)
{
  const auto grid = optionalDouble(params, "grid", 1.0);
  if (!finitePositive(grid))
  {
    return invalidParamsFailure("grid must be greater than zero");
  }
  const auto detail = params.value("detail").toString("summary").trimmed().toLower();
  const auto detailLevel = detail == "full"  ? QString{"full"}
                           : detail == "ids" ? QString{"ids"}
                                             : QString{"summary"};
  const auto maxBrushes =
    std::clamp(optionalSize(params, "maxBrushes", 100), size_t{0}, size_t{1000});
  const auto includeVertices =
    detailLevel == "full" && mcpOptionalBool(params, "includeVertices", false);
  const auto includeBrushEntries = detailLevel == "ids" || detailLevel == "full";

  auto brushes = selectedBrushNodes(map);
  auto brushResults = QJsonArray{};
  auto objectIds = QJsonArray{};
  auto invalidObjectIds = QJsonArray{};
  auto nonGridAlignedObjectIds = QJsonArray{};
  auto materials = QStringList{};
  auto invalidBrushCount = 0;
  auto nonGridAlignedCount = 0;
  auto bounds = vm::bbox3d{};
  auto hasBounds = false;
  auto truncated = false;
  for (const auto* brushNode : brushes)
  {
    const auto& brush = brushNode->brush();
    const auto gridOk = brushGridAligned(brush, grid);
    const auto objectId = nodePathId(*brushNode, map.worldNode());
    if (!gridOk)
    {
      ++nonGridAlignedCount;
      if (nonGridAlignedObjectIds.size() < 10)
      {
        nonGridAlignedObjectIds.push_back(objectId);
      }
    }
    if (!brush.closed() || !brush.fullySpecified())
    {
      ++invalidBrushCount;
      if (invalidObjectIds.size() < 10)
      {
        invalidObjectIds.push_back(objectId);
      }
    }

    bounds = hasBounds ? vm::merge(bounds, brushNode->logicalBounds())
                       : brushNode->logicalBounds();
    hasBounds = true;

    for (const auto& material : brushMaterials(brush))
    {
      if (!materials.contains(material))
      {
        materials.push_back(material);
      }
    }

    if (!includeBrushEntries)
    {
      continue;
    }

    const auto returnedEntryCount = detailLevel == "ids"
                                      ? static_cast<size_t>(objectIds.size())
                                      : static_cast<size_t>(brushResults.size());
    if (returnedEntryCount >= maxBrushes)
    {
      truncated = true;
      continue;
    }

    objectIds.push_back(objectId);
    if (detailLevel == "full")
    {
      auto brushJson = nodeSummaryJson(*brushNode, map.worldNode());
      brushJson.insert("vertexCount", static_cast<int>(brush.vertexCount()));
      brushJson.insert("edgeCount", static_cast<int>(brush.edgeCount()));
      brushJson.insert("closed", brush.closed());
      brushJson.insert("convex", brush.closed() && brush.fullySpecified());
      brushJson.insert("gridAligned", gridOk);
      brushJson.insert("materials", stringListToJsonArray(brushMaterials(brush)));
      if (includeVertices)
      {
        brushJson.insert("vertices", vertexPositionsToJson(brush.vertexPositions()));
      }
      brushResults.push_back(std::move(brushJson));
    }
  }

  auto result = QJsonObject{
    {"brushCount", static_cast<int>(brushes.size())},
    {"invalidBrushCount", invalidBrushCount},
    {"nonGridAlignedCount", nonGridAlignedCount},
    {"grid", grid},
    {"detail", detailLevel},
    {"truncated", truncated},
    {"returnedBrushCount", includeBrushEntries ? brushResults.size() : 0},
    {"materials", stringListToJsonArray(materials)},
  };
  if (hasBounds)
  {
    result.insert("bounds", boundsToJson(bounds));
  }
  if (!invalidObjectIds.isEmpty())
  {
    result.insert("invalidObjectIds", invalidObjectIds);
  }
  if (!nonGridAlignedObjectIds.isEmpty())
  {
    result.insert("nonGridAlignedObjectIds", nonGridAlignedObjectIds);
  }
  if (detailLevel == "ids")
  {
    result.insert("objectIds", objectIds);
    result.insert("returnedBrushCount", objectIds.size());
  }
  else if (detailLevel == "full")
  {
    result.insert("brushes", brushResults);
  }

  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult blockoutValidateSpiralStairsResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history)
{
  auto error = QString{};
  auto expected = spiralStairsParamsFromJson(params, error);
  if (!expected)
  {
    return invalidParamsFailure(error);
  }

  const auto grid = optionalDouble(params, "grid", 1.0);
  if (!finitePositive(grid))
  {
    return invalidParamsFailure("grid must be greater than zero");
  }

  auto source = QString{"selection"};
  auto brushes = std::vector<mdl::BrushNode*>{};
  const auto operationIdValue = params.value("operationId");
  if (operationIdValue.isString() && !operationIdValue.toString().isEmpty())
  {
    source = "operationId";
    const auto operationId = operationIdValue.toString();
    brushes = brushNodesFromOperationId(map, operationId, history, error);
    if (!error.isEmpty())
    {
      return invalidParamsFailure(error);
    }
  }
  else
  {
    brushes = selectedBrushNodes(map);
  }

  auto invalidBrushCount = 0;
  auto nonGridAlignedCount = 0;
  for (const auto* brushNode : brushes)
  {
    const auto& brush = brushNode->brush();
    if (!brush.closed() || !brush.fullySpecified())
    {
      ++invalidBrushCount;
    }
    if (!brushGridAligned(brush, grid))
    {
      ++nonGridAlignedCount;
    }
  }

  const auto expectedBrushCount =
    expected->steps + (expected->column ? 1u : 0u) + (expected->landing ? 1u : 0u);
  const auto brushCountMatches = brushes.size() == expectedBrushCount;
  const auto geometryChecks = analyzeSpiralGeometry(brushes, *expected);
  auto validation = spiralValidationJson(*expected, brushes.size(), invalidBrushCount);
  validation.insert("source", source);
  validation.insert("expectedBrushCount", static_cast<int>(expectedBrushCount));
  validation.insert("brushCountMatches", brushCountMatches);
  validation.insert("nonGridAlignedCount", nonGridAlignedCount);
  validation.insert("gridAligned", nonGridAlignedCount == 0);
  validation.insert("gapCount", geometryChecks.gapCount);
  validation.insert("radiusMismatch", geometryChecks.radiusMismatch);
  validation.insert("columnFits", geometryChecks.columnFits);
  validation.insert("landingConnected", geometryChecks.landingConnected);
  validation.insert("zProgression", geometryChecks.zProgression);
  validation.insert(
    "valid",
    invalidBrushCount == 0 && nonGridAlignedCount == 0 && brushCountMatches
      && geometryChecks.gapCount == 0 && !geometryChecks.radiusMismatch
      && geometryChecks.columnFits && geometryChecks.landingConnected
      && geometryChecks.zProgression);

  auto errors = geometryChecks.errors;
  if (brushes.empty())
  {
    errors.push_back(
      source == "operationId" ? "The referenced operation contains no live brush nodes"
                              : "No selected brush nodes to validate");
  }
  if (!brushCountMatches)
  {
    errors.push_back(QString{"Expected %1 brushes, got %2"}
                       .arg(static_cast<int>(expectedBrushCount))
                       .arg(static_cast<int>(brushes.size())));
  }
  if (invalidBrushCount > 0)
  {
    errors.push_back(QString{"%1 selected brushes are invalid"}.arg(invalidBrushCount));
  }
  if (nonGridAlignedCount > 0)
  {
    errors.push_back(
      QString{"%1 selected brushes are not grid aligned"}.arg(nonGridAlignedCount));
  }
  validation.insert("errors", errors);

  return McpBridgeToolResult::success(std::move(validation));
}

McpBridgeToolResult geometryAnalyzeSelectionResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return geometryAnalyzeSelectionResult(mapWindow->document().map(), params);
}

McpBridgeToolResult blockoutValidateSpiralStairsResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return blockoutValidateSpiralStairsResult(mapWindow->document().map(), params, history);
}

} // namespace tb::ui
