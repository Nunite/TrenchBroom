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

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QUuid>

#include "McpBridgeServerTools.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Node.h"
#include "mdl/PatchNode.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"
#include "ui/SystemPaths.h"
#include "ui/mcp/McpObjectRegistry.h"

#include "vm/bbox.h"
#include "vm/vec.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <set>
#include <vector>

namespace tb::ui
{
namespace mcp = tb::mcp;

namespace
{

constexpr auto DefaultWidth = 1400;
constexpr auto DefaultHeight = 1000;
constexpr auto MinWidth = 900;
constexpr auto MinHeight = 650;
constexpr auto MaxDetailedFaceCount = 20000;

struct ReviewView
{
  QString name;
  QString projection;
  vm::vec3d right;
  vm::vec3d up;
  vm::vec3d view;
  QString rightLabel;
  QString upLabel;
  bool iso = false;
};

struct RenderFace
{
  std::vector<vm::vec3d> vertices;
  vm::vec3d normal;
  double depth = 0.0;
  int brushIndex = 0;
};

struct RenderEdge
{
  vm::vec3d a;
  vm::vec3d b;
};

struct RenderGeometry
{
  std::vector<RenderFace> faces;
  std::vector<RenderEdge> edges;
  std::vector<mdl::Node*> unsupportedNodes;
  int targetBrushCount = 0;
  int targetObjectCount = 0;
  bool simplified = false;
};

struct ProjectedPoint
{
  QPointF point;
  double u = 0.0;
  double v = 0.0;
};

QString pathToQString(const std::filesystem::path& path)
{
  return path.empty() ? QString{} : pathAsQString(path);
}

QString sanitizeFileComponent(QString value, const QString& fallback)
{
  value = value.trimmed();
  if (value.isEmpty())
  {
    value = fallback;
  }

  for (auto i = 0; i < value.size(); ++i)
  {
    const auto ch = value.at(i);
    if (!ch.isLetterOrNumber() && ch != '-' && ch != '_' && ch != '.')
    {
      value[i] = '_';
    }
  }
  return value.isEmpty() ? fallback : value;
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

std::optional<vm::vec3d> vecFromJson(const QJsonValue& value)
{
  if (!value.isArray())
  {
    return std::nullopt;
  }
  const auto array = value.toArray();
  if (array.size() != 3)
  {
    return std::nullopt;
  }
  for (const auto& entry : array)
  {
    if (!entry.isDouble())
    {
      return std::nullopt;
    }
  }
  return vm::vec3d{array[0].toDouble(), array[1].toDouble(), array[2].toDouble()};
}

std::optional<vm::bbox3d> boundsFromJson(const QJsonObject& value)
{
  const auto min = vecFromJson(value.value("min"));
  const auto max = vecFromJson(value.value("max"));
  if (!min || !max)
  {
    return std::nullopt;
  }
  return vm::bbox3d{*min, *max};
}

bool optionalBool(const QJsonObject& params, const QString& key, const bool fallback)
{
  const auto value = params.value(key);
  return value.isBool() ? value.toBool() : fallback;
}

int optionalIntClamped(
  const QJsonObject& params,
  const QString& key,
  const int fallback,
  const int minValue,
  const int maxValue)
{
  const auto value = params.value(key);
  const auto parsed = value.isDouble() ? value.toInt(fallback) : fallback;
  return std::clamp(parsed, minValue, maxValue);
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

mdl::Node* resolveLegacyNodeId(mdl::Map& map, const QString& objectId)
{
  const auto path = McpObjectRegistry::parseLegacyObjectId(objectId);
  if (!path)
  {
    return nullptr;
  }
  return map.worldNode().resolvePath(*path);
}

void appendWarning(QJsonArray& warnings, const QString& warning)
{
  if (!warning.isEmpty())
  {
    warnings.push_back(warning);
  }
}

template <typename NodeT>
std::vector<NodeT*> dedupeNodes(std::vector<NodeT*> nodes)
{
  std::ranges::sort(nodes);
  nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
  return nodes;
}

std::vector<mdl::Node*> resolveReviewTargetNodes(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry,
  QJsonArray& warnings,
  QStringList& resolvedObjectIds)
{
  auto externalObjectIds = QStringList{};
  if (const auto operationIdsValue = params.value("operationIds");
      operationIdsValue.isArray())
  {
    for (const auto& operationIdValue : operationIdsValue.toArray())
    {
      if (!operationIdValue.isString())
      {
        appendWarning(warnings, "operationIds must contain only strings");
        continue;
      }
      const auto operationId = operationIdValue.toString();
      const auto it = std::ranges::find_if(history, [&](const auto& operation) {
        return operation.operationId == operationId;
      });
      if (it == history.end())
      {
        appendWarning(
          warnings,
          QString{"Unknown MCP operation id '%1' for review."}.arg(operationId));
        continue;
      }
      if (it->undone)
      {
        appendWarning(
          warnings,
          QString{"MCP operation '%1' is undone; changed objects may be stale."}.arg(
            operationId));
      }
      externalObjectIds.append(it->changedObjectIds);
    }
  }

  if (const auto objectIdsValue = params.value("objectIds"); objectIdsValue.isArray())
  {
    for (const auto& objectIdValue : objectIdsValue.toArray())
    {
      if (objectIdValue.isString())
      {
        externalObjectIds.push_back(objectIdValue.toString());
      }
      else
      {
        appendWarning(warnings, "objectIds must contain only strings");
      }
    }
  }

  auto nodes = std::vector<mdl::Node*>{};
  auto seenExternalIds = std::set<QString>{};
  for (const auto& objectId : externalObjectIds)
  {
    if (!seenExternalIds.insert(objectId).second)
    {
      continue;
    }

    auto legacyPathId = objectId;
    if (objectRegistry != nullptr)
    {
      const auto resolved = objectRegistry->resolveExternalId(map, objectId);
      if (!resolved.ok)
      {
        appendWarning(warnings, resolved.error);
        continue;
      }
      legacyPathId = resolved.legacyPathId;
    }

    auto* node = resolveLegacyNodeId(map, legacyPathId);
    if (node == nullptr)
    {
      appendWarning(
        warnings, QString{"Could not resolve review target object '%1'."}.arg(objectId));
      continue;
    }
    nodes.push_back(node);
    resolvedObjectIds.push_back(
      objectRegistry != nullptr ? objectRegistry->externalIdForLegacy(map, legacyPathId)
                                : legacyPathId);
  }

  return dedupeNodes(std::move(nodes));
}

void collectBrushNodes(mdl::Node& node, std::vector<mdl::BrushNode*>& brushes)
{
  if (auto* brushNode = dynamic_cast<mdl::BrushNode*>(&node))
  {
    brushes.push_back(brushNode);
    return;
  }

  for (auto* child : node.children())
  {
    if (child != nullptr)
    {
      collectBrushNodes(*child, brushes);
    }
  }
}

vm::bbox3d boundsForNodes(const std::vector<mdl::Node*>& nodes)
{
  auto builder = vm::bbox3d::builder{};
  for (const auto* node : nodes)
  {
    if (node != nullptr)
    {
      builder.add(node->logicalBounds());
    }
  }
  return builder.bounds();
}

vm::bbox3d boundsForGeometry(const RenderGeometry& geometry)
{
  auto builder = vm::bbox3d::builder{};
  for (const auto& face : geometry.faces)
  {
    for (const auto& vertex : face.vertices)
    {
      builder.add(vertex);
    }
  }
  for (const auto& edge : geometry.edges)
  {
    builder.add(edge.a);
    builder.add(edge.b);
  }
  return builder.bounds();
}

std::vector<RenderEdge> bboxEdges(const vm::bbox3d& bounds)
{
  const auto min = bounds.min;
  const auto max = bounds.max;
  const auto v = std::array{
    vm::vec3d{min.x(), min.y(), min.z()},
    vm::vec3d{max.x(), min.y(), min.z()},
    vm::vec3d{max.x(), max.y(), min.z()},
    vm::vec3d{min.x(), max.y(), min.z()},
    vm::vec3d{min.x(), min.y(), max.z()},
    vm::vec3d{max.x(), min.y(), max.z()},
    vm::vec3d{max.x(), max.y(), max.z()},
    vm::vec3d{min.x(), max.y(), max.z()},
  };
  return {
    {v[0], v[1]},
    {v[1], v[2]},
    {v[2], v[3]},
    {v[3], v[0]},
    {v[4], v[5]},
    {v[5], v[6]},
    {v[6], v[7]},
    {v[7], v[4]},
    {v[0], v[4]},
    {v[1], v[5]},
    {v[2], v[6]},
    {v[3], v[7]},
  };
}

void addBboxToGeometry(RenderGeometry& geometry, const vm::bbox3d& bounds)
{
  auto edges = bboxEdges(bounds);
  geometry.edges.insert(geometry.edges.end(), edges.begin(), edges.end());
}

std::optional<vm::vec3d> faceNormal(const std::vector<vm::vec3d>& vertices)
{
  if (vertices.size() < 3)
  {
    return std::nullopt;
  }
  const auto normal = vm::cross(vertices[1] - vertices[0], vertices[2] - vertices[0]);
  if (vm::is_zero(vm::squared_length(normal), vm::Cd::almost_zero()))
  {
    return std::nullopt;
  }
  return vm::normalize(normal);
}

RenderGeometry buildRenderGeometry(
  const std::vector<mdl::Node*>& nodes, QJsonArray& warnings, const int maxFaces)
{
  auto geometry = RenderGeometry{};
  geometry.targetObjectCount = static_cast<int>(nodes.size());

  auto brushNodes = std::vector<mdl::BrushNode*>{};
  for (auto* node : nodes)
  {
    if (node == nullptr)
    {
      continue;
    }
    collectBrushNodes(*node, brushNodes);
  }
  brushNodes = dedupeNodes(std::move(brushNodes));
  geometry.targetBrushCount = static_cast<int>(brushNodes.size());

  auto totalFaceCount = 0;
  for (const auto* brushNode : brushNodes)
  {
    totalFaceCount += static_cast<int>(brushNode->brush().faceCount());
  }

  if (totalFaceCount > maxFaces)
  {
    geometry.simplified = true;
    appendWarning(
      warnings,
      QString{"geometrySimplified: %1 faces exceeds maxDetailedFaces=%2; review uses "
              "object bounding boxes."}
        .arg(totalFaceCount)
        .arg(maxFaces));
    for (const auto* node : nodes)
    {
      addBboxToGeometry(geometry, node->logicalBounds());
    }
    return geometry;
  }

  auto brushIndex = 0;
  for (const auto* brushNode : brushNodes)
  {
    const auto& brush = brushNode->brush();
    for (const auto& face : brush.faces())
    {
      const auto vertices = face.vertexPositions();
      if (vertices.size() < 3)
      {
        continue;
      }
      auto normal = faceNormal(vertices);
      if (!normal)
      {
        normal = face.normal();
      }

      auto renderFace = RenderFace{};
      renderFace.vertices = vertices;
      renderFace.normal = *normal;
      renderFace.brushIndex = brushIndex;
      geometry.faces.push_back(std::move(renderFace));

      for (auto i = size_t{0}; i < vertices.size(); ++i)
      {
        geometry.edges.push_back({vertices[i], vertices[(i + 1u) % vertices.size()]});
      }
    }
    ++brushIndex;
  }

  for (auto* node : nodes)
  {
    if (node == nullptr)
    {
      continue;
    }
    auto childBrushes = std::vector<mdl::BrushNode*>{};
    collectBrushNodes(*node, childBrushes);
    if (childBrushes.empty())
    {
      geometry.unsupportedNodes.push_back(node);
      addBboxToGeometry(geometry, node->logicalBounds());
    }
  }

  if (!geometry.unsupportedNodes.empty())
  {
    appendWarning(
      warnings,
      QString{"unsupportedObjectPlaceholder: %1 non-brush targets rendered as bounds "
              "markers."}
        .arg(geometry.unsupportedNodes.size()));
  }

  return geometry;
}

ReviewView reviewViewForName(QString name)
{
  name = name.trimmed().toLower();
  if (name == "overview_3d")
  {
    name = "iso_overview_ne";
  }
  else if (name == "detail_3d")
  {
    name = "iso_overview_sw";
  }
  else if (name == "top_2d_fit")
  {
    name = "top_plan";
  }
  else if (name == "side_2d_fit")
  {
    name = "side_elevation_long";
  }

  auto makeIso = [&](const QString& viewName, const vm::vec3d& cameraDirection) {
    const auto view = vm::normalize(cameraDirection);
    auto right = vm::normalize(vm::cross(vm::vec3d{0, 0, 1}, view));
    if (vm::is_zero(vm::squared_length(right), vm::Cd::almost_zero()))
    {
      right = vm::vec3d{1, 0, 0};
    }
    const auto up = vm::normalize(vm::cross(view, right));
    return ReviewView{viewName, "orthographic_iso", right, up, view, "X", "Z", true};
  };

  if (name == "iso_overview_sw")
  {
    return makeIso(name, vm::vec3d{-1, 1, -0.72});
  }
  if (name == "top_plan")
  {
    return ReviewView{
      name,
      "orthographic_top",
      vm::vec3d{1, 0, 0},
      vm::vec3d{0, 1, 0},
      vm::vec3d{0, 0, -1},
      "X",
      "Y",
      false,
    };
  }
  if (name == "side_elevation_long")
  {
    return ReviewView{
      name,
      "orthographic_side",
      vm::vec3d{1, 0, 0},
      vm::vec3d{0, 0, 1},
      vm::vec3d{0, -1, 0},
      "X",
      "Z",
      false,
    };
  }
  if (name == "front_elevation_cross")
  {
    return ReviewView{
      name,
      "orthographic_front",
      vm::vec3d{0, 1, 0},
      vm::vec3d{0, 0, 1},
      vm::vec3d{-1, 0, 0},
      "Y",
      "Z",
      false,
    };
  }
  return makeIso("iso_overview_ne", vm::vec3d{1, -1, -0.72});
}

QJsonArray defaultViews()
{
  return QJsonArray{
    "iso_overview_ne",
    "iso_overview_sw",
    "top_plan",
    "side_elevation_long",
    "front_elevation_cross",
  };
}

QJsonArray viewsFromParams(const QJsonObject& params)
{
  const auto value = params.value("views");
  if (!value.isArray() || value.toArray().isEmpty())
  {
    return defaultViews();
  }
  return value.toArray();
}

ProjectedPoint projectPoint(
  const vm::vec3d& point,
  const ReviewView& view,
  const double minU,
  const double maxU,
  const double minV,
  const double maxV,
  const QSize& size,
  const double padding)
{
  const auto u = vm::dot(point, view.right);
  const auto v = vm::dot(point, view.up);
  const auto usableWidth = std::max(1.0, size.width() * (1.0 - 2.0 * padding));
  const auto usableHeight = std::max(1.0, size.height() * (1.0 - 2.0 * padding));
  const auto scale = std::min(
    usableWidth / std::max(1.0, maxU - minU), usableHeight / std::max(1.0, maxV - minV));
  const auto contentWidth = (maxU - minU) * scale;
  const auto contentHeight = (maxV - minV) * scale;
  const auto originX = (size.width() - contentWidth) / 2.0;
  const auto originY = (size.height() - contentHeight) / 2.0;
  return ProjectedPoint{
    QPointF{originX + (u - minU) * scale, originY + contentHeight - (v - minV) * scale},
    u,
    v,
  };
}

std::vector<vm::vec3d> referencePoints(
  const RenderGeometry& geometry, const vm::bbox3d& targetBounds)
{
  auto points = std::vector<vm::vec3d>{};
  for (const auto& face : geometry.faces)
  {
    points.insert(points.end(), face.vertices.begin(), face.vertices.end());
  }
  for (const auto& edge : geometry.edges)
  {
    points.push_back(edge.a);
    points.push_back(edge.b);
  }
  if (points.empty())
  {
    const auto min = targetBounds.min;
    const auto max = targetBounds.max;
    points = {
      {min.x(), min.y(), min.z()},
      {max.x(), min.y(), min.z()},
      {max.x(), max.y(), min.z()},
      {min.x(), max.y(), min.z()},
      {min.x(), min.y(), max.z()},
      {max.x(), min.y(), max.z()},
      {max.x(), max.y(), max.z()},
      {min.x(), max.y(), max.z()},
    };
  }
  return points;
}

QColor faceColorForNormal(
  const vm::vec3d& normal, const ReviewView& view, const int brushIndex)
{
  const auto light = vm::normalize(vm::vec3d{0.35, -0.45, 0.82});
  const auto normalLight = std::abs(vm::dot(normal, light));
  const auto viewLight = std::max(0.0, vm::dot(normal, -view.view));
  const auto shade =
    std::clamp(164.0 + normalLight * 52.0 + viewLight * 22.0, 120.0, 238.0);
  const auto tint = (brushIndex % 7) * 5;
  return QColor{
    std::clamp(static_cast<int>(shade + tint), 0, 255),
    std::clamp(static_cast<int>(shade + 2), 0, 255),
    std::clamp(static_cast<int>(shade - tint / 2), 0, 255),
    255,
  };
}

double polygonArea(const QPolygonF& polygon)
{
  auto area = 0.0;
  for (auto i = 0; i < polygon.size(); ++i)
  {
    const auto& a = polygon[i];
    const auto& b = polygon[(i + 1) % polygon.size()];
    area += a.x() * b.y() - b.x() * a.y();
  }
  return std::abs(area) * 0.5;
}

void drawAxes(
  QPainter& painter, const QSize& size, const QString& rightLabel, const QString& upLabel)
{
  const auto origin = QPointF{54.0, static_cast<double>(size.height()) - 54.0};
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(QPen{QColor{180, 60, 60}, 3});
  painter.drawLine(origin, origin + QPointF{52, 0});
  painter.drawText(origin + QPointF{58, 5}, rightLabel);
  painter.setPen(QPen{QColor{60, 150, 75}, 3});
  painter.drawLine(origin, origin + QPointF{0, -52});
  painter.drawText(origin + QPointF{-6, -60}, upLabel);
}

QJsonObject qualityForCapture(
  const QString& viewName,
  const QString& path,
  const int width,
  const int height,
  const qint64 fileSize,
  const double targetCoverage,
  const double targetWidthRatio,
  const double targetHeightRatio,
  const double edgeDensity,
  const bool isoView)
{
  auto warnings = QJsonArray{};
  if (path.isEmpty() || fileSize <= 0)
  {
    warnings.push_back("missingImage");
  }
  if (width < MinWidth || height < MinHeight)
  {
    warnings.push_back("imageTooSmall");
  }
  const auto minCoverage = isoView ? 0.35 : 0.45;
  const auto maxCoverage = isoView ? 0.90 : 0.92;
  const auto majorAxisFill = std::max(targetWidthRatio, targetHeightRatio);
  if (targetCoverage < minCoverage && majorAxisFill < 0.60)
  {
    warnings.push_back("targetCoverageTooLow");
  }
  if (targetCoverage > maxCoverage)
  {
    warnings.push_back("targetCoverageTooHigh");
  }
  if (edgeDensity < 0.002)
  {
    warnings.push_back("edgeDensityTooLow");
  }
  return QJsonObject{
    {"view", viewName},
    {"valid", warnings.isEmpty()},
    {"warnings", warnings},
    {"width", width},
    {"height", height},
    {"fileSize", static_cast<double>(fileSize)},
    {"targetCoverage", targetCoverage},
    {"targetWidthRatio", targetWidthRatio},
    {"targetHeightRatio", targetHeightRatio},
    {"edgeDensity", edgeDensity},
  };
}

QJsonObject renderCapture(
  const RenderGeometry& geometry,
  const vm::bbox3d& targetBounds,
  const ReviewView& view,
  const QSize& imageSize,
  const QString& outputPath,
  const bool includeAxes,
  const bool includeBoundsBox)
{
  const auto padding = 0.12;
  const auto points = referencePoints(geometry, targetBounds);
  auto minU = std::numeric_limits<double>::max();
  auto maxU = std::numeric_limits<double>::lowest();
  auto minV = std::numeric_limits<double>::max();
  auto maxV = std::numeric_limits<double>::lowest();
  for (const auto& point : points)
  {
    const auto u = vm::dot(point, view.right);
    const auto v = vm::dot(point, view.up);
    minU = std::min(minU, u);
    maxU = std::max(maxU, u);
    minV = std::min(minV, v);
    maxV = std::max(maxV, v);
  }

  auto image = QImage{imageSize, QImage::Format_ARGB32_Premultiplied};
  image.fill(QColor{248, 248, 246});

  auto painter = QPainter{&image};
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.fillRect(image.rect(), QColor{248, 248, 246});

  auto faces = geometry.faces;
  for (auto& face : faces)
  {
    face.depth = 0.0;
    for (const auto& vertex : face.vertices)
    {
      face.depth += vm::dot(vertex, view.view);
    }
    face.depth /= std::max<size_t>(1, face.vertices.size());
  }
  std::ranges::sort(
    faces, [](const auto& lhs, const auto& rhs) { return lhs.depth < rhs.depth; });

  auto targetPixelBounds = QRectF{};
  auto totalFaceArea = 0.0;
  painter.setPen(QPen{QColor{95, 95, 92}, 1.25});
  for (const auto& face : faces)
  {
    auto polygon = QPolygonF{};
    for (const auto& vertex : face.vertices)
    {
      const auto projected =
        projectPoint(vertex, view, minU, maxU, minV, maxV, imageSize, padding);
      polygon.push_back(projected.point);
      targetPixelBounds = targetPixelBounds.united(QRectF{projected.point, QSizeF{1, 1}});
    }
    if (polygon.size() >= 3)
    {
      totalFaceArea += polygonArea(polygon);
      painter.setBrush(faceColorForNormal(face.normal, view, face.brushIndex));
      painter.drawPolygon(polygon);
    }
  }

  if (includeBoundsBox)
  {
    auto boundsGeometry = RenderGeometry{};
    addBboxToGeometry(boundsGeometry, targetBounds);
    painter.setPen(QPen{QColor{80, 120, 180, 120}, 2, Qt::DashLine});
    for (const auto& edge : boundsGeometry.edges)
    {
      const auto a =
        projectPoint(edge.a, view, minU, maxU, minV, maxV, imageSize, padding);
      const auto b =
        projectPoint(edge.b, view, minU, maxU, minV, maxV, imageSize, padding);
      painter.drawLine(a.point, b.point);
    }
  }

  auto edgeLength = 0.0;
  painter.setPen(QPen{QColor{26, 26, 24}, 2.0});
  for (const auto& edge : geometry.edges)
  {
    const auto a = projectPoint(edge.a, view, minU, maxU, minV, maxV, imageSize, padding);
    const auto b = projectPoint(edge.b, view, minU, maxU, minV, maxV, imageSize, padding);
    painter.drawLine(a.point, b.point);
    edgeLength += std::hypot(a.point.x() - b.point.x(), a.point.y() - b.point.y());
    targetPixelBounds = targetPixelBounds.united(QRectF{a.point, QSizeF{1, 1}});
    targetPixelBounds = targetPixelBounds.united(QRectF{b.point, QSizeF{1, 1}});
  }

  painter.setPen(QPen{QColor{70, 70, 66}, 1});
  painter.drawText(QPointF{18.0, 28.0}, view.name);
  if (includeAxes)
  {
    drawAxes(painter, imageSize, view.rightLabel, view.upLabel);
  }
  painter.end();

  auto dir = QDir{};
  dir.mkpath(QFileInfo{outputPath}.absolutePath());
  const auto saved = image.save(outputPath, "PNG");
  const auto fileInfo = QFileInfo{outputPath};
  const auto fileSize = saved && fileInfo.exists() ? fileInfo.size() : 0;
  const auto imageArea = static_cast<double>(imageSize.width() * imageSize.height());
  const auto coverage =
    targetPixelBounds.isValid()
      ? std::clamp(
          targetPixelBounds.width() * targetPixelBounds.height() / imageArea, 0.0, 1.0)
      : 0.0;
  const auto widthRatio =
    targetPixelBounds.isValid()
      ? std::clamp(
          targetPixelBounds.width() / static_cast<double>(imageSize.width()), 0.0, 1.0)
      : 0.0;
  const auto heightRatio =
    targetPixelBounds.isValid()
      ? std::clamp(
          targetPixelBounds.height() / static_cast<double>(imageSize.height()), 0.0, 1.0)
      : 0.0;
  const auto edgeDensity = std::clamp(edgeLength / imageArea, 0.0, 1.0);

  const auto quality = qualityForCapture(
    view.name,
    outputPath,
    imageSize.width(),
    imageSize.height(),
    fileSize,
    coverage,
    widthRatio,
    heightRatio,
    edgeDensity,
    view.iso);
  return QJsonObject{
    {"view", view.name},
    {"path", outputPath},
    {"width", imageSize.width()},
    {"height", imageSize.height()},
    {"fileSize", static_cast<double>(fileSize)},
    {"format", "png"},
    {"projection", view.projection},
    {"targetCoverage", coverage},
    {"targetWidthRatio", widthRatio},
    {"targetHeightRatio", heightRatio},
    {"edgeDensity", edgeDensity},
    {"facePixelArea", totalFaceArea},
    {"valid", quality.value("valid").toBool()},
    {"quality", quality},
  };
}

std::filesystem::path reviewOutputDir(const QJsonObject& params, const QString& reviewId)
{
  if (const auto outputDir = params.value("outputDir").toString().trimmed();
      !outputDir.isEmpty())
  {
    const auto path = std::filesystem::path{outputDir.toStdString()};
    if (path.filename().string() == reviewId.toStdString())
    {
      return path;
    }
    return path / reviewId.toStdString();
  }
  return SystemPaths::tempDirectory() / "TrenchBroomMCP" / "reviews"
         / reviewId.toStdString();
}

bool writeManifest(const std::filesystem::path& path, const QJsonObject& manifest)
{
  auto file = QFile{pathToQString(path)};
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
  {
    return false;
  }
  file.write(QJsonDocument{manifest}.toJson(QJsonDocument::Indented));
  return true;
}

} // namespace

McpBridgeToolResult renderReviewTargetsForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry)
{
  auto warnings = QJsonArray{};
  auto resolvedObjectIds = QStringList{};
  auto nodes = resolveReviewTargetNodes(
    map, params, history, objectRegistry, warnings, resolvedObjectIds);

  auto targetBounds = std::optional<vm::bbox3d>{};
  if (!nodes.empty())
  {
    targetBounds = boundsForNodes(nodes);
  }
  else if (const auto boundsValue = params.value("bounds"); boundsValue.isObject())
  {
    targetBounds = boundsFromJson(boundsValue.toObject());
    if (!targetBounds)
    {
      appendWarning(warnings, "Invalid bounds object; expected min/max vec3 arrays.");
    }
  }

  if (!targetBounds)
  {
    return McpBridgeToolResult::success(QJsonObject{
      {"tool", "render_review_targets"},
      {"renderer", "geometry_cpu"},
      {"reviewId", QString{}},
      {"targetObjectCount", 0},
      {"targetBrushCount", 0},
      {"unsupportedObjectCount", 0},
      {"captureCount", 0},
      {"captures", QJsonArray{}},
      {"quality", QJsonArray{}},
      {"qualityValid", false},
      {"warnings", warnings},
      {"note", "render_review_targets requires live operationIds, objectIds, or bounds."},
    });
  }

  const auto maxFaces =
    optionalIntClamped(params, "maxDetailedFaces", MaxDetailedFaceCount, 100, 100000);
  auto geometry = buildRenderGeometry(nodes, warnings, maxFaces);
  if (geometry.faces.empty() && geometry.edges.empty())
  {
    addBboxToGeometry(geometry, *targetBounds);
    appendWarning(warnings, "emptyGeometryFallback: target rendered as bounds only.");
  }
  const auto geometryBounds = boundsForGeometry(geometry);
  if (!geometry.faces.empty() || !geometry.edges.empty())
  {
    targetBounds = geometryBounds;
  }

  const auto requestedWidth =
    optionalIntClamped(params, "imageWidth", DefaultWidth, MinWidth, 4096);
  const auto requestedHeight =
    optionalIntClamped(params, "imageHeight", DefaultHeight, MinHeight, 4096);
  auto imageSize = QSize{requestedWidth, requestedHeight};
  if (const auto imageSizeValue = params.value("imageSize"); imageSizeValue.isArray())
  {
    const auto array = imageSizeValue.toArray();
    if (array.size() == 2 && array[0].isDouble() && array[1].isDouble())
    {
      imageSize = QSize{
        std::clamp(array[0].toInt(DefaultWidth), MinWidth, 4096),
        std::clamp(array[1].toInt(DefaultHeight), MinHeight, 4096),
      };
    }
  }

  const auto reviewId =
    params.value("reviewId").toString().trimmed().isEmpty()
      ? QString{"review-%1"}.arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
      : sanitizeFileComponent(params.value("reviewId").toString(), "review");
  const auto outputDir = reviewOutputDir(params, reviewId);
  auto error = std::error_code{};
  std::filesystem::create_directories(outputDir, error);
  if (error)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError,
      QString{"Could not create review output directory: %1"}.arg(
        pathToQString(outputDir)));
  }

  const auto includeAxes = optionalBool(params, "includeAxes", true);
  const auto includeBoundsBox = optionalBool(params, "includeBoundsBox", false);
  const auto views = viewsFromParams(params);
  auto captures = QJsonArray{};
  auto quality = QJsonArray{};
  for (const auto& viewValue : views)
  {
    if (!viewValue.isString())
    {
      appendWarning(warnings, "views must contain only strings");
      continue;
    }
    const auto view = reviewViewForName(viewValue.toString());
    const auto captureName =
      sanitizeFileComponent(view.name, "review-view") + QString{".png"};
    const auto outputPath = pathToQString(outputDir / captureName.toStdString());
    auto capture = renderCapture(
      geometry,
      *targetBounds,
      view,
      imageSize,
      outputPath,
      includeAxes,
      includeBoundsBox);
    captures.push_back(capture);
    quality.push_back(capture.value("quality").toObject());
  }

  const auto qualityValid =
    !captures.isEmpty()
    && std::ranges::all_of(
      quality,
      [](const auto& entry) { return entry.toObject().value("valid").toBool(false); })
    && (!nodes.empty() || params.value("bounds").isObject());

  auto result = QJsonObject{
    {"tool", "render_review_targets"},
    {"renderer", "geometry_cpu"},
    {"style", params.value("style").toString("whitebox_edges")},
    {"reviewId", reviewId},
    {"resourceUri", QString{"tbmcp://review/%1"}.arg(reviewId)},
    {"outputDir", pathToQString(outputDir)},
    {"manifestPath", pathToQString(outputDir / "manifest.json")},
    {"targetObjectIds", QJsonArray::fromStringList(resolvedObjectIds)},
    {"targetObjectCount", geometry.targetObjectCount},
    {"targetBrushCount", geometry.targetBrushCount},
    {"unsupportedObjectCount", static_cast<int>(geometry.unsupportedNodes.size())},
    {"targetBounds", boundsToJson(*targetBounds)},
    {"captureCount", captures.size()},
    {"captures", captures},
    {"quality", quality},
    {"qualityValid", qualityValid},
    {"warnings", warnings},
    {"maxDetailedFaces", maxFaces},
    {"simplified", geometry.simplified},
    {"faceCount", static_cast<int>(geometry.faces.size())},
    {"edgeCount", static_cast<int>(geometry.edges.size())},
  };

  if (!writeManifest(outputDir / "manifest.json", result))
  {
    auto updatedWarnings = result.value("warnings").toArray();
    updatedWarnings.push_back("manifestWriteFailed");
    result.insert("warnings", updatedWarnings);
    result.insert("qualityValid", false);
  }

  return McpBridgeToolResult::success(result);
}

McpBridgeToolResult renderReviewTargetsResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }
  return renderReviewTargetsForMapResult(
    mapWindow->document().map(), params, history, objectRegistry);
}

} // namespace tb::ui
