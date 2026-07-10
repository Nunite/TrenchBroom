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
#include <QFileInfo>
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
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
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
constexpr auto DefaultContactSheetMaxCaptures = 2;

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
  QString materialName;
  double depth = 0.0;
  double centerZ = 0.0;
  int brushIndex = 0;
};

struct RenderEdge
{
  vm::vec3d a;
  vm::vec3d b;
};

struct RenderLabel
{
  vm::vec3d position;
  QString text;
  QString kind;
  std::optional<vm::vec3d> direction;
};

struct RenderGeometry
{
  std::vector<RenderFace> faces;
  std::vector<RenderEdge> edges;
  std::vector<RenderLabel> labels;
  std::vector<mdl::Node*> unsupportedNodes;
  int targetBrushCount = 0;
  int targetObjectCount = 0;
  int entityLabelCount = 0;
  int orderLabelCount = 0;
  int partLabelCount = 0;
  bool simplified = false;
  std::set<QString> materialNames;
};

struct ProjectedPoint
{
  QPointF point;
  double u = 0.0;
  double v = 0.0;
};

void appendWarning(QJsonArray& warnings, const QString& warning);

enum class ReviewStyle
{
  WhiteboxEdges,
  MaterialTintEdges,
  HeightHeatmapEdges,
};

enum class ReviewEdgeMode
{
  Auto,
  All,
  Minimal,
  Silhouette,
  None,
};

QString reviewEdgeModeName(const ReviewEdgeMode edgeMode)
{
  switch (edgeMode)
  {
  case ReviewEdgeMode::All:
    return "all";
  case ReviewEdgeMode::Minimal:
    return "minimal";
  case ReviewEdgeMode::Silhouette:
    return "silhouette";
  case ReviewEdgeMode::None:
    return "none";
  case ReviewEdgeMode::Auto:
    return "auto";
  }
  return "auto";
}

QString reviewStyleName(const ReviewStyle style)
{
  switch (style)
  {
  case ReviewStyle::MaterialTintEdges:
    return "material_tint_edges";
  case ReviewStyle::HeightHeatmapEdges:
    return "height_heatmap_edges";
  case ReviewStyle::WhiteboxEdges:
    return "whitebox_edges";
  }
  return "whitebox_edges";
}

ReviewStyle reviewStyleFromParams(const QJsonObject& params, QJsonArray& warnings)
{
  const auto styleName =
    params.value("style").toString("whitebox_edges").trimmed().toLower();
  if (styleName.isEmpty() || styleName == "whitebox_edges" || styleName == "whitebox")
  {
    return ReviewStyle::WhiteboxEdges;
  }
  if (
    styleName == "material_tint_edges" || styleName == "material_tint"
    || styleName == "material")
  {
    return ReviewStyle::MaterialTintEdges;
  }
  if (
    styleName == "height_heatmap_edges" || styleName == "height_heatmap"
    || styleName == "terrain_heightmap")
  {
    return ReviewStyle::HeightHeatmapEdges;
  }
  appendWarning(
    warnings,
    QString{"unknownReviewStyle: '%1' falls back to whitebox_edges."}.arg(styleName));
  return ReviewStyle::WhiteboxEdges;
}

ReviewEdgeMode reviewEdgeModeFromParams(const QJsonObject& params, QJsonArray& warnings)
{
  const auto edgeMode = params.value("edgeMode").toString("auto").trimmed().toLower();
  if (edgeMode.isEmpty() || edgeMode == "auto")
  {
    return ReviewEdgeMode::Auto;
  }
  if (edgeMode == "all")
  {
    return ReviewEdgeMode::All;
  }
  if (edgeMode == "minimal" || edgeMode == "sparse")
  {
    return ReviewEdgeMode::Minimal;
  }
  if (edgeMode == "silhouette" || edgeMode == "outline")
  {
    return ReviewEdgeMode::Silhouette;
  }
  if (edgeMode == "none" || edgeMode == "off")
  {
    return ReviewEdgeMode::None;
  }
  appendWarning(
    warnings, QString{"unknownReviewEdgeMode: '%1' falls back to auto."}.arg(edgeMode));
  return ReviewEdgeMode::Auto;
}

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

QString scopedMetadataKey(const QString& documentFingerprint, const QString& objectId)
{
  return documentFingerprint.isEmpty()
           ? objectId
           : QString{"%1|%2"}.arg(documentFingerprint, objectId);
}

std::optional<QJsonObject> metadataForNode(
  mdl::Map& map,
  const mdl::Node& node,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore,
  const McpObjectRegistry* objectRegistry)
{
  if (metadataStore == nullptr)
  {
    return std::nullopt;
  }

  const auto documentFingerprint = documentFingerprintForMap(map, objectRegistry);
  const auto legacyId = nodePathId(node, map.worldNode());
  auto candidates = QStringList{legacyId};
  if (objectRegistry != nullptr)
  {
    candidates.push_front(objectRegistry->externalIdForLegacy(map, legacyId));
  }

  for (const auto& objectId : candidates)
  {
    for (const auto& key :
         QStringList{scopedMetadataKey(documentFingerprint, objectId), objectId})
    {
      const auto it = metadataStore->find(key);
      if (
        it != metadataStore->end() && !it->second.stale
        && (it->second.documentFingerprint.isEmpty() || it->second.documentFingerprint == documentFingerprint))
      {
        return it->second.metadata;
      }
    }
  }
  return std::nullopt;
}

QStringList labelPartsFromParams(const QJsonObject& params)
{
  auto result = QStringList{};
  const auto value = params.value("labelParts");
  if (value.isString())
  {
    const auto text = value.toString().trimmed();
    if (!text.isEmpty())
    {
      result.push_back(text);
    }
  }
  else if (value.isArray())
  {
    for (const auto& entry : value.toArray())
    {
      const auto text = entry.toString().trimmed();
      if (!text.isEmpty())
      {
        result.push_back(text);
      }
    }
  }
  result.removeDuplicates();
  return result;
}

struct ReviewLabelOptions
{
  bool includeEntityLabels = true;
  bool includeOrderLabels = false;
  bool includeDirectionLabels = false;
  QStringList labelParts;
  int labelStride = 1;
};

ReviewLabelOptions labelOptionsFromParams(
  const QJsonObject& params, const int targetObjectCount, QJsonArray& warnings)
{
  auto options = ReviewLabelOptions{};
  const auto autoHideLabelsThreshold =
    optionalIntClamped(params, "autoHideLabelsThreshold", 120, 0, 10000);
  options.includeEntityLabels = optionalBool(params, "includeEntityLabels", true);
  options.includeOrderLabels = optionalBool(params, "includeOrderLabels", false);
  options.includeDirectionLabels = optionalBool(params, "includeDirectionLabels", false);
  options.labelParts = labelPartsFromParams(params);
  options.labelStride = optionalIntClamped(params, "labelStride", 1, 1, 1000);

  if (autoHideLabelsThreshold > 0 && targetObjectCount > autoHideLabelsThreshold)
  {
    if (options.includeOrderLabels)
    {
      options.includeOrderLabels = false;
      warnings.push_back(
        QString{"orderLabelsAutoHidden: targetObjectCount=%1 exceeds threshold=%2"}
          .arg(targetObjectCount)
          .arg(autoHideLabelsThreshold));
    }
    if (options.includeEntityLabels)
    {
      options.includeEntityLabels = false;
      warnings.push_back(
        QString{"entityLabelsAutoHidden: targetObjectCount=%1 exceeds threshold=%2; "
                "entity glyph markers are still rendered."}
          .arg(targetObjectCount)
          .arg(autoHideLabelsThreshold));
    }
    if (!options.labelParts.isEmpty())
    {
      options.labelParts.clear();
      warnings.push_back(
        QString{"partLabelsAutoHidden: targetObjectCount=%1 exceeds threshold=%2"}
          .arg(targetObjectCount)
          .arg(autoHideLabelsThreshold));
    }
  }

  return options;
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

std::vector<mdl::Node*> nodesFromMcpHistory(
  mdl::Map& map,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry,
  QJsonArray& warnings)
{
  auto nodes = std::vector<mdl::Node*>{};
  auto seenObjectIds = std::set<QString>{};
  auto unresolvedCount = 0;
  auto unresolvedSamples = QStringList{};
  for (const auto& operation : history)
  {
    if (operation.undone)
    {
      continue;
    }
    for (const auto& objectId : operation.changedObjectIds)
    {
      if (!seenObjectIds.insert(objectId).second)
      {
        continue;
      }
      auto legacyPathId = objectId;
      if (objectRegistry != nullptr)
      {
        const auto resolved = objectRegistry->resolveExternalId(map, objectId);
        if (!resolved.ok)
        {
          ++unresolvedCount;
          if (unresolvedSamples.size() < 5)
          {
            unresolvedSamples.push_back(objectId);
          }
          continue;
        }
        legacyPathId = resolved.legacyPathId;
      }
      if (auto* node = resolveLegacyNodeId(map, legacyPathId))
      {
        nodes.push_back(node);
      }
    }
  }
  if (unresolvedCount > 0)
  {
    appendWarning(
      warnings,
      QString{
        "mcpHistoryUnresolvedObjects: %1 stale or document-switched history objects "
        "were omitted%2."}
        .arg(unresolvedCount)
        .arg(
          unresolvedSamples.isEmpty()
            ? QString{}
            : QString{"; sample=%1"}.arg(unresolvedSamples.join(','))));
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
  for (const auto& label : geometry.labels)
  {
    builder.add(label.position);
    if (label.direction)
    {
      builder.add(label.position + *label.direction);
    }
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

void addCrossToGeometry(
  RenderGeometry& geometry, const vm::vec3d& center, const double radius)
{
  const auto r = std::max(8.0, radius);
  geometry.edges.push_back({center - vm::vec3d{r, 0, 0}, center + vm::vec3d{r, 0, 0}});
  geometry.edges.push_back({center - vm::vec3d{0, r, 0}, center + vm::vec3d{0, r, 0}});
  geometry.edges.push_back({center - vm::vec3d{0, 0, r}, center + vm::vec3d{0, 0, r}});
}

std::optional<vm::vec3d> faceNormal(const std::vector<vm::vec3d>& vertices);

double verticalExaggerationFromParams(const QJsonObject& params, QJsonArray& warnings)
{
  const auto value = params.value("verticalExaggeration");
  if (value.isUndefined())
  {
    return 1.0;
  }
  if (!value.isDouble())
  {
    appendWarning(warnings, "verticalExaggeration must be a number; using 1.0.");
    return 1.0;
  }
  const auto requested = value.toDouble();
  const auto clamped = std::clamp(requested, 0.1, 10.0);
  if (std::abs(requested - clamped) > 0.001)
  {
    appendWarning(
      warnings,
      QString{"verticalExaggeration clamped from %1 to %2."}.arg(requested).arg(clamped));
  }
  return clamped;
}

vm::vec3d scaleZAround(const vm::vec3d& point, const double originZ, const double factor)
{
  return vm::vec3d{point.x(), point.y(), originZ + (point.z() - originZ) * factor};
}

vm::bbox3d scaleBoundsZ(
  const vm::bbox3d& bounds, const double originZ, const double factor)
{
  auto builder = vm::bbox3d::builder{};
  const auto min = bounds.min;
  const auto max = bounds.max;
  for (const auto& point : std::array{
         vm::vec3d{min.x(), min.y(), min.z()},
         vm::vec3d{max.x(), min.y(), min.z()},
         vm::vec3d{max.x(), max.y(), min.z()},
         vm::vec3d{min.x(), max.y(), min.z()},
         vm::vec3d{min.x(), min.y(), max.z()},
         vm::vec3d{max.x(), min.y(), max.z()},
         vm::vec3d{max.x(), max.y(), max.z()},
         vm::vec3d{min.x(), max.y(), max.z()},
       })
  {
    builder.add(scaleZAround(point, originZ, factor));
  }
  return builder.bounds();
}

void applyVerticalExaggerationToGeometry(
  RenderGeometry& geometry, vm::bbox3d& targetBounds, const double factor)
{
  if (std::abs(factor - 1.0) < 0.001)
  {
    return;
  }

  const auto originZ = targetBounds.min.z();
  for (auto& face : geometry.faces)
  {
    for (auto& vertex : face.vertices)
    {
      vertex = scaleZAround(vertex, originZ, factor);
    }
    auto normal = faceNormal(face.vertices);
    if (normal)
    {
      face.normal = *normal;
    }
    face.centerZ = 0.0;
    for (const auto& vertex : face.vertices)
    {
      face.centerZ += vertex.z();
    }
    face.centerZ /= static_cast<double>(std::max<size_t>(1, face.vertices.size()));
  }
  for (auto& edge : geometry.edges)
  {
    edge.a = scaleZAround(edge.a, originZ, factor);
    edge.b = scaleZAround(edge.b, originZ, factor);
  }
  for (auto& label : geometry.labels)
  {
    const auto originalPosition = label.position;
    label.position = scaleZAround(label.position, originZ, factor);
    if (label.direction)
    {
      const auto endpoint =
        scaleZAround(originalPosition + *label.direction, originZ, factor);
      label.direction = endpoint - label.position;
    }
  }
  targetBounds = scaleBoundsZ(targetBounds, originZ, factor);
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
  mdl::Map& map,
  const std::vector<mdl::Node*>& nodes,
  QJsonArray& warnings,
  const int maxFaces,
  const ReviewLabelOptions& labelOptions,
  const McpObjectRegistry* objectRegistry,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore)
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
      renderFace.materialName = QString::fromStdString(face.attributes().materialName());
      renderFace.centerZ = 0.0;
      for (const auto& vertex : vertices)
      {
        renderFace.centerZ += vertex.z();
      }
      renderFace.centerZ /= static_cast<double>(vertices.size());
      renderFace.brushIndex = brushIndex;
      geometry.materialNames.insert(renderFace.materialName);
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
      if (const auto* entityNode = dynamic_cast<const mdl::EntityNodeBase*>(node))
      {
        const auto origin = entityNode->entity().origin();
        addCrossToGeometry(geometry, origin, 16.0);
        if (labelOptions.includeEntityLabels)
        {
          geometry.labels.push_back(RenderLabel{
            origin + vm::vec3d{0, 0, 18},
            QString::fromStdString(entityNode->entity().classname()),
            "entity",
            std::nullopt,
          });
          ++geometry.entityLabelCount;
        }
      }
      else
      {
        addBboxToGeometry(geometry, node->logicalBounds());
      }
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

  if (!labelOptions.labelParts.isEmpty())
  {
    const auto labelPartsLower = [&]() {
      auto result = QStringList{};
      for (const auto& part : labelOptions.labelParts)
      {
        result.push_back(part.trimmed().toLower());
      }
      return result;
    }();
    auto partIndex = 0;
    for (const auto* node : nodes)
    {
      if (node == nullptr)
      {
        continue;
      }
      ++partIndex;
      if (
        labelOptions.labelStride > 1 && ((partIndex - 1) % labelOptions.labelStride) != 0)
      {
        continue;
      }
      const auto metadata = metadataForNode(map, *node, metadataStore, objectRegistry);
      if (!metadata)
      {
        continue;
      }
      const auto part = metadata->value("part").toString().trimmed();
      if (part.isEmpty() || !labelPartsLower.contains(part.toLower()))
      {
        continue;
      }
      const auto bounds = node->logicalBounds();
      geometry.labels.push_back(RenderLabel{
        bounds.center() + vm::vec3d{0, 0, std::max(12.0, bounds.size().z() * 0.2)},
        part,
        "part",
        std::nullopt,
      });
      ++geometry.partLabelCount;
    }
  }

  if (labelOptions.includeOrderLabels)
  {
    auto order = 1;
    for (const auto* node : nodes)
    {
      if (node == nullptr)
      {
        continue;
      }
      if (labelOptions.labelStride > 1 && ((order - 1) % labelOptions.labelStride) != 0)
      {
        ++order;
        continue;
      }
      const auto center = node->logicalBounds().center();
      auto direction = std::optional<vm::vec3d>{};
      if (labelOptions.includeDirectionLabels && order < static_cast<int>(nodes.size()))
      {
        const auto* nextNode = nodes[static_cast<size_t>(order)];
        if (nextNode != nullptr)
        {
          const auto delta = nextNode->logicalBounds().center() - center;
          if (!vm::is_zero(vm::squared_length(delta), vm::Cd::almost_zero()))
          {
            const auto boundsSize = node->logicalBounds().size();
            const auto arrowLength = std::max(
              32.0, std::max({boundsSize.x(), boundsSize.y(), boundsSize.z()}) * 0.7);
            direction = vm::normalize(delta) * arrowLength;
          }
        }
      }
      geometry.labels.push_back(RenderLabel{
        center + vm::vec3d{0, 0, std::max(12.0, node->logicalBounds().size().z() * 0.15)},
        QString::number(order),
        "order",
        direction,
      });
      ++geometry.orderLabelCount;
      ++order;
    }
  }

  return geometry;
}

ReviewView reviewViewForName(QString name)
{
  name = name.trimmed().toLower();
  if (name == "overview_3d" || name == "iso" || name == "isometric" || name == "3d")
  {
    name = "iso_overview_ne";
  }
  else if (name == "detail_3d" || name == "iso_sw" || name == "isometric_sw")
  {
    name = "iso_overview_sw";
  }
  else if (name == "top_2d_fit" || name == "top" || name == "plan")
  {
    name = "top_plan";
  }
  else if (
    name == "side_2d_fit" || name == "side" || name == "side_profile"
    || name == "elevation")
  {
    name = "side_elevation_long";
  }
  else if (name == "front" || name == "front_profile" || name == "cross")
  {
    name = "front_elevation_cross";
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
  for (const auto& label : geometry.labels)
  {
    points.push_back(label.position);
    if (label.direction)
    {
      points.push_back(label.position + *label.direction);
    }
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

double normalizedHeight(const double z, const vm::bbox3d& targetBounds)
{
  const auto minZ = targetBounds.min.z();
  const auto maxZ = targetBounds.max.z();
  if (std::abs(maxZ - minZ) < 0.001)
  {
    return 0.5;
  }
  return std::clamp((z - minZ) / (maxZ - minZ), 0.0, 1.0);
}

QColor mixColor(const QColor& a, const QColor& b, const double t)
{
  const auto clamped = std::clamp(t, 0.0, 1.0);
  const auto mix = [&](const int lhs, const int rhs) {
    return static_cast<int>(std::round(lhs + (rhs - lhs) * clamped));
  };
  return QColor{
    mix(a.red(), b.red()),
    mix(a.green(), b.green()),
    mix(a.blue(), b.blue()),
    mix(a.alpha(), b.alpha()),
  };
}

QColor shadeColor(const QColor& color, const double factor)
{
  const auto scale = std::clamp(factor, 0.35, 1.45);
  const auto channel = [&](const int value) {
    return std::clamp(static_cast<int>(std::round(value * scale)), 0, 255);
  };
  return QColor{
    channel(color.red()), channel(color.green()), channel(color.blue()), color.alpha()};
}

QColor colorForMaterialName(const QString& materialName)
{
  const auto key = materialName.trimmed().toLower();
  if (key.isEmpty() || key == "__tb_empty")
  {
    return QColor{200, 200, 194};
  }
  if (key.contains("floor") || key.contains("road") || key.contains("track"))
  {
    return QColor{118, 138, 154};
  }
  if (
    key.contains("terrain") || key.contains("grass") || key.contains("dirt")
    || key.contains("rock") || key.contains("stone"))
  {
    return QColor{143, 152, 118};
  }
  if (key.contains("wall") || key.contains("concrete") || key.contains("brick"))
  {
    return QColor{178, 169, 158};
  }
  if (key.contains("water"))
  {
    return QColor{91, 150, 190};
  }
  if (key.contains("sky"))
  {
    return QColor{156, 188, 226};
  }
  if (key.contains("clip") || key.contains("trigger") || key.contains("hint"))
  {
    return QColor{214, 120, 168};
  }

  const auto hash = qHash(key);
  const auto hue = static_cast<int>(hash % 360u);
  auto color = QColor{};
  color.setHsv(hue, 72, 204);
  return color;
}

QColor heightHeatmapColor(const double t)
{
  const auto low = QColor{110, 150, 184};
  const auto mid = QColor{190, 194, 184};
  const auto high = QColor{215, 154, 105};
  if (t < 0.5)
  {
    return mixColor(low, mid, t * 2.0);
  }
  return mixColor(mid, high, (t - 0.5) * 2.0);
}

QColor faceColorForStyle(
  const RenderFace& face,
  const ReviewView& view,
  const vm::bbox3d& targetBounds,
  const ReviewStyle style)
{
  const auto light = vm::normalize(vm::vec3d{0.35, -0.45, 0.82});
  const auto normalLight = std::abs(vm::dot(face.normal, light));
  const auto viewLight = std::max(0.0, vm::dot(face.normal, -view.view));
  const auto shadeFactor =
    std::clamp(0.74 + normalLight * 0.22 + viewLight * 0.12, 0.62, 1.12);

  switch (style)
  {
  case ReviewStyle::MaterialTintEdges:
    return shadeColor(colorForMaterialName(face.materialName), shadeFactor);
  case ReviewStyle::HeightHeatmapEdges:
    return shadeColor(
      heightHeatmapColor(normalizedHeight(face.centerZ, targetBounds)), shadeFactor);
  case ReviewStyle::WhiteboxEdges: {
    const auto shade =
      std::clamp(164.0 + normalLight * 52.0 + viewLight * 22.0, 120.0, 238.0);
    const auto tint = (face.brushIndex % 7) * 5;
    return QColor{
      std::clamp(static_cast<int>(shade + tint), 0, 255),
      std::clamp(static_cast<int>(shade + 2), 0, 255),
      std::clamp(static_cast<int>(shade - tint / 2), 0, 255),
      255,
    };
  }
  }
  return QColor{200, 200, 196};
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

void drawReviewLabels(
  QPainter& painter,
  QRectF& targetPixelBounds,
  double& edgeLength,
  const std::vector<RenderLabel>& labels,
  const ReviewView& view,
  const double minU,
  const double maxU,
  const double minV,
  const double maxV,
  const QSize& imageSize,
  const double padding)
{
  painter.setRenderHint(QPainter::Antialiasing, true);
  for (const auto& label : labels)
  {
    const auto projected =
      projectPoint(label.position, view, minU, maxU, minV, maxV, imageSize, padding);
    const auto point = projected.point;
    const auto isOrder = label.kind == "order";
    const auto radius = isOrder ? 11.0 : 8.0;
    const auto text = label.text.left(32);
    auto textRect = QRectF{};

    if (isOrder)
    {
      const auto bubble = QRectF{
        point.x() - radius,
        point.y() - radius,
        radius * 2.0,
        radius * 2.0,
      };
      painter.setPen(QPen{QColor{20, 20, 20}, 1.4});
      painter.setBrush(QColor{255, 245, 180, 235});
      painter.drawEllipse(bubble);
      painter.setPen(QPen{QColor{20, 20, 20}, 1.0});
      painter.drawText(bubble, Qt::AlignCenter, text);
      textRect = bubble;
    }
    else
    {
      const auto metrics = painter.fontMetrics();
      textRect = QRectF{
        point.x() + 8.0,
        point.y() - 10.0,
        static_cast<double>(std::max(46, metrics.horizontalAdvance(text) + 12)),
        22.0,
      };
      painter.setPen(QPen{QColor{35, 35, 35}, 1.0});
      painter.setBrush(QColor{226, 238, 255, 230});
      painter.drawRoundedRect(textRect, 4.0, 4.0);
      painter.drawText(textRect, Qt::AlignCenter, text);
      painter.setPen(QPen{QColor{42, 82, 150}, 2.2});
      painter.drawLine(point + QPointF{-radius, 0}, point + QPointF{radius, 0});
      painter.drawLine(point + QPointF{0, -radius}, point + QPointF{0, radius});
    }

    targetPixelBounds = targetPixelBounds.united(textRect);
    if (label.direction)
    {
      const auto endpoint = projectPoint(
        label.position + *label.direction,
        view,
        minU,
        maxU,
        minV,
        maxV,
        imageSize,
        padding);
      painter.setPen(QPen{QColor{40, 60, 170, 210}, 2.2});
      painter.drawLine(point, endpoint.point);
      const auto vector = endpoint.point - point;
      const auto length = std::hypot(vector.x(), vector.y());
      if (length > 1.0)
      {
        const auto unit = QPointF{vector.x() / length, vector.y() / length};
        const auto side = QPointF{-unit.y(), unit.x()};
        const auto arrowA = endpoint.point - unit * 10.0 + side * 5.0;
        const auto arrowB = endpoint.point - unit * 10.0 - side * 5.0;
        painter.drawLine(endpoint.point, arrowA);
        painter.drawLine(endpoint.point, arrowB);
        edgeLength += length;
      }
      targetPixelBounds = targetPixelBounds.united(QRectF{endpoint.point, QSizeF{1, 1}});
    }
  }
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
  const bool isoView,
  const bool requireEdges,
  const bool sparseGlyphTarget)
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
  const auto minMajorAxisFill = sparseGlyphTarget ? 0.10 : 0.60;
  if (targetCoverage < minCoverage && majorAxisFill < minMajorAxisFill)
  {
    warnings.push_back("targetCoverageTooLow");
  }
  if (targetCoverage > maxCoverage)
  {
    warnings.push_back("targetCoverageTooHigh");
  }
  const auto minEdgeDensity = sparseGlyphTarget ? 0.0008 : 0.002;
  if (requireEdges && edgeDensity < minEdgeDensity)
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
    {"sparseGlyphTarget", sparseGlyphTarget},
  };
}

QJsonObject renderCapture(
  const RenderGeometry& geometry,
  const vm::bbox3d& targetBounds,
  const ReviewView& view,
  const ReviewStyle style,
  const ReviewEdgeMode requestedEdgeMode,
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
  auto projectedFacePolygons = std::vector<QPolygonF>{};
  if (style == ReviewStyle::HeightHeatmapEdges)
  {
    painter.setPen(Qt::NoPen);
  }
  else
  {
    painter.setPen(QPen{QColor{95, 95, 92}, 1.25});
  }
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
      projectedFacePolygons.push_back(polygon);
      totalFaceArea += polygonArea(polygon);
      painter.setBrush(faceColorForStyle(face, view, targetBounds, style));
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
  auto edgePixelCount = 0;
  const auto effectiveEdgeMode =
    requestedEdgeMode == ReviewEdgeMode::Auto
      ? (style == ReviewStyle::HeightHeatmapEdges ? ReviewEdgeMode::Minimal
                                                  : ReviewEdgeMode::All)
      : requestedEdgeMode;
  if (effectiveEdgeMode == ReviewEdgeMode::Silhouette)
  {
    auto mask = QImage{imageSize, QImage::Format_Grayscale8};
    mask.fill(0);
    auto maskPainter = QPainter{&mask};
    maskPainter.setRenderHint(QPainter::Antialiasing, false);
    maskPainter.setPen(Qt::NoPen);
    maskPainter.setBrush(Qt::white);
    for (const auto& polygon : projectedFacePolygons)
    {
      maskPainter.drawPolygon(polygon);
    }
    maskPainter.end();

    painter.setPen(QPen{QColor{26, 26, 24}, 1.5});
    const auto covered = [&](const int x, const int y) {
      return x >= 0 && y >= 0 && x < mask.width() && y < mask.height()
             && qGray(mask.pixel(x, y)) > 127;
    };
    for (auto y = 0; y < mask.height(); ++y)
    {
      for (auto x = 0; x < mask.width(); ++x)
      {
        if (
          covered(x, y)
          && (!covered(x - 1, y) || !covered(x + 1, y) || !covered(x, y - 1) || !covered(x, y + 1)))
        {
          painter.drawPoint(x, y);
          ++edgePixelCount;
        }
      }
    }
    edgeLength = edgePixelCount;
  }
  for (const auto& edge : geometry.edges)
  {
    if (
      effectiveEdgeMode == ReviewEdgeMode::None
      || effectiveEdgeMode == ReviewEdgeMode::Silhouette)
    {
      continue;
    }

    if (style == ReviewStyle::HeightHeatmapEdges)
    {
      const auto zDelta = std::abs(edge.a.z() - edge.b.z());
      const auto strongHeightEdge = zDelta > 1.0;
      if (effectiveEdgeMode == ReviewEdgeMode::Minimal && !strongHeightEdge)
      {
        continue;
      }
      painter.setPen(QPen{
        strongHeightEdge ? QColor{28, 28, 26, 135} : QColor{36, 36, 34, 38},
        strongHeightEdge ? 0.85 : 0.4});
    }
    else
    {
      painter.setPen(QPen{QColor{26, 26, 24}, 2.0});
    }
    const auto a = projectPoint(edge.a, view, minU, maxU, minV, maxV, imageSize, padding);
    const auto b = projectPoint(edge.b, view, minU, maxU, minV, maxV, imageSize, padding);
    painter.drawLine(a.point, b.point);
    edgeLength += std::hypot(a.point.x() - b.point.x(), a.point.y() - b.point.y());
    ++edgePixelCount;
    targetPixelBounds = targetPixelBounds.united(QRectF{a.point, QSizeF{1, 1}});
    targetPixelBounds = targetPixelBounds.united(QRectF{b.point, QSizeF{1, 1}});
  }

  drawReviewLabels(
    painter,
    targetPixelBounds,
    edgeLength,
    geometry.labels,
    view,
    minU,
    maxU,
    minV,
    maxV,
    imageSize,
    padding);

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
  const auto sparseGlyphTarget =
    geometry.faces.empty() && !geometry.labels.empty() && !geometry.edges.empty();

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
    view.iso,
    effectiveEdgeMode != ReviewEdgeMode::None,
    sparseGlyphTarget);
  return QJsonObject{
    {"view", view.name},
    {"path", outputPath},
    {"width", imageSize.width()},
    {"height", imageSize.height()},
    {"fileSize", static_cast<double>(fileSize)},
    {"format", "png"},
    {"projection", view.projection},
    {"style", reviewStyleName(style)},
    {"edgeMode", reviewEdgeModeName(effectiveEdgeMode)},
    {"edgeInterpretation",
     effectiveEdgeMode == ReviewEdgeMode::Silhouette ? "silhouette"
     : effectiveEdgeMode == ReviewEdgeMode::None     ? "none"
                                                     : "construction"},
    {"internalBrushEdgesDrawn",
     effectiveEdgeMode != ReviewEdgeMode::Silhouette
       && effectiveEdgeMode != ReviewEdgeMode::None},
    {"edgePixelCount", edgePixelCount},
    {"targetCoverage", coverage},
    {"targetWidthRatio", widthRatio},
    {"targetHeightRatio", heightRatio},
    {"edgeDensity", edgeDensity},
    {"facePixelArea", totalFaceArea},
    {"valid", quality.value("valid").toBool()},
    {"quality", quality},
  };
}

QJsonObject writeContactSheet(
  const QJsonArray& captures,
  const QString& outputPath,
  const QString& title,
  const QSize& requestedSize)
{
  auto loaded = std::vector<std::pair<QString, QImage>>{};
  for (const auto& captureValue : captures)
  {
    const auto capture = captureValue.toObject();
    const auto path = capture.value("path").toString();
    auto image = QImage{path};
    if (!image.isNull())
    {
      loaded.emplace_back(capture.value("view").toString(), std::move(image));
    }
  }

  auto warnings = QJsonArray{};
  if (loaded.empty())
  {
    warnings.push_back("contactSheetNoSourceImages");
    return QJsonObject{
      {"path", outputPath},
      {"width", 0},
      {"height", 0},
      {"fileSize", 0},
      {"valid", false},
      {"warnings", warnings},
    };
  }

  const auto width = std::clamp(requestedSize.width(), MinWidth, 4096);
  const auto height = std::clamp(requestedSize.height(), MinHeight, 4096);
  auto image = QImage{QSize{width, height}, QImage::Format_ARGB32_Premultiplied};
  image.fill(QColor{246, 246, 244});

  auto painter = QPainter{&image};
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.fillRect(image.rect(), QColor{246, 246, 244});
  painter.setPen(QPen{QColor{38, 38, 36}, 1.0});
  painter.drawText(QPointF{20.0, 28.0}, title);

  const auto count = static_cast<int>(loaded.size());
  const auto columns = count <= 2 ? count : 2;
  const auto rows = static_cast<int>(std::ceil(count / static_cast<double>(columns)));
  const auto margin = 18;
  const auto titleHeight = 42;
  const auto gutter = 14;
  const auto cellWidth =
    (width - margin * 2 - gutter * std::max(0, columns - 1)) / std::max(1, columns);
  const auto cellHeight =
    (height - titleHeight - margin * 2 - gutter * std::max(0, rows - 1))
    / std::max(1, rows);

  for (auto i = 0; i < count; ++i)
  {
    const auto column = i % columns;
    const auto row = i / columns;
    const auto cell = QRect{
      margin + column * (cellWidth + gutter),
      titleHeight + margin + row * (cellHeight + gutter),
      cellWidth,
      cellHeight,
    };
    painter.setPen(QPen{QColor{198, 198, 192}, 1});
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(cell.adjusted(0, 0, -1, -1));

    const auto labelHeight = 22;
    painter.setPen(QPen{QColor{56, 56, 52}, 1});
    painter.drawText(
      cell.adjusted(8, 2, -8, 0), Qt::AlignLeft | Qt::AlignTop, loaded[i].first);

    const auto imageRect = cell.adjusted(8, labelHeight, -8, -8);
    const auto scaled = loaded[i].second.scaled(
      imageRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const auto target = QRect{
      imageRect.x() + (imageRect.width() - scaled.width()) / 2,
      imageRect.y() + (imageRect.height() - scaled.height()) / 2,
      scaled.width(),
      scaled.height(),
    };
    painter.drawImage(target, scaled);
  }
  painter.end();

  auto dir = QDir{};
  dir.mkpath(QFileInfo{outputPath}.absolutePath());
  const auto saved = image.save(outputPath, "PNG");
  const auto fileInfo = QFileInfo{outputPath};
  if (!saved || !fileInfo.exists() || fileInfo.size() <= 0)
  {
    warnings.push_back("contactSheetWriteFailed");
  }

  return QJsonObject{
    {"path", outputPath},
    {"width", width},
    {"height", height},
    {"fileSize", static_cast<double>(fileInfo.exists() ? fileInfo.size() : 0)},
    {"format", "png"},
    {"sourceCaptureCount", count},
    {"valid", warnings.isEmpty()},
    {"warnings", warnings},
  };
}

QJsonArray capturesForContactSheet(const QJsonArray& captures, const int maxCaptures)
{
  if (maxCaptures <= 0 || captures.size() <= maxCaptures)
  {
    return captures;
  }

  auto result = QJsonArray{};
  for (auto i = 0; i < maxCaptures; ++i)
  {
    result.push_back(captures[i]);
  }
  return result;
}

void annotateContactSheet(
  QJsonObject& contactSheet, const int totalCaptureCount, const int maxCaptures)
{
  if (contactSheet.isEmpty())
  {
    return;
  }

  const auto includedCaptureCount =
    std::min(totalCaptureCount, maxCaptures <= 0 ? totalCaptureCount : maxCaptures);
  contactSheet.insert("sourceCaptureCount", totalCaptureCount);
  contactSheet.insert("includedCaptureCount", includedCaptureCount);
  contactSheet.insert("omittedCaptureCount", totalCaptureCount - includedCaptureCount);
  contactSheet.insert("maxCaptures", maxCaptures);
}

std::filesystem::path reviewOutputDir(const QJsonObject& params, const QString& reviewId)
{
  auto outputRoot = std::filesystem::path{};
  if (const auto outputDir = params.value("outputDir").toString().trimmed();
      !outputDir.isEmpty())
  {
    outputRoot = std::filesystem::path{outputDir.toStdString()};
  }
  else
  {
    outputRoot = SystemPaths::tempDirectory() / "TrenchBroomMCP" / "reviews";
  }

  auto error = std::error_code{};
  auto absoluteRoot =
    outputRoot.is_absolute() ? outputRoot : std::filesystem::absolute(outputRoot, error);
  if (error)
  {
    absoluteRoot = outputRoot;
  }
  absoluteRoot = absoluteRoot.lexically_normal();
  if (absoluteRoot.filename().string() == reviewId.toStdString())
  {
    return absoluteRoot;
  }
  return absoluteRoot / reviewId.toStdString();
}

void addAbsoluteReviewPaths(QJsonObject& result)
{
  const auto addAbsolutePath = [&](const QString& sourceKey, const QString& targetKey) {
    const auto path = result.value(sourceKey).toString();
    if (path.isEmpty())
    {
      return;
    }
    result.insert(targetKey, QFileInfo{path}.absoluteFilePath());
  };

  addAbsolutePath("outputDir", "absoluteOutputDir");
  addAbsolutePath("manifestPath", "absoluteManifestPath");
  addAbsolutePath("preferredCapturePath", "absolutePreferredCapturePath");

  auto contactSheet = result.value("contactSheet").toObject();
  if (!contactSheet.isEmpty())
  {
    const auto path = contactSheet.value("path").toString();
    if (!path.isEmpty())
    {
      contactSheet.insert("absolutePath", QFileInfo{path}.absoluteFilePath());
      result.insert("contactSheet", contactSheet);
    }
  }

  auto captures = result.value("captures").toArray();
  if (!captures.isEmpty())
  {
    auto updatedCaptures = QJsonArray{};
    for (const auto& value : captures)
    {
      auto capture = value.toObject();
      const auto path = capture.value("path").toString();
      if (!path.isEmpty())
      {
        capture.insert("absolutePath", QFileInfo{path}.absoluteFilePath());
      }
      updatedCaptures.push_back(capture);
    }
    result.insert("captures", updatedCaptures);
  }
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

QJsonObject reviewSemanticAcceptance()
{
  return QJsonObject{
    {"required", true},
    {"automated", false},
    {"status", "requires_human_or_skill_review"},
    {"hint",
     "qualityValid only checks render readability; inspect preferredCapturePath "
     "or contactSheet against the requested scene intent before accepting the result."},
  };
}

QJsonObject compactReviewResult(const QJsonObject& result, const QString& toolName)
{
  auto compact = QJsonObject{
    {"tool", toolName},
    {"renderer", result.value("renderer")},
    {"style", result.value("style")},
    {"edgeMode", result.value("edgeMode")},
    {"edgeInterpretation", result.value("edgeInterpretation")},
    {"internalBrushEdgesDrawn", result.value("internalBrushEdgesDrawn")},
    {"visualReviewRequired", result.value("visualReviewRequired")},
    {"reviewId", result.value("reviewId")},
    {"resourceUri", result.value("resourceUri")},
    {"preferredCapturePath", result.value("preferredCapturePath")},
    {"absolutePreferredCapturePath", result.value("absolutePreferredCapturePath")},
    {"outputDir", result.value("outputDir")},
    {"absoluteOutputDir", result.value("absoluteOutputDir")},
    {"manifestPath", result.value("manifestPath")},
    {"absoluteManifestPath", result.value("absoluteManifestPath")},
    {"targetObjectCount", result.value("targetObjectCount")},
    {"targetObjectIdsCount", result.value("targetObjectIdsCount")},
    {"targetObjectIdsSample", result.value("targetObjectIdsSample")},
    {"idsMode", result.value("idsMode")},
    {"targetBrushCount", result.value("targetBrushCount")},
    {"unsupportedObjectCount", result.value("unsupportedObjectCount")},
    {"targetBounds", result.value("targetBounds")},
    {"captureCount", result.value("captureCount")},
    {"qualityValid", result.value("qualityValid")},
    {"semanticAcceptance", result.value("semanticAcceptance")},
    {"warnings", result.value("warnings")},
    {"faceCount", result.value("faceCount")},
    {"edgeCount", result.value("edgeCount")},
    {"labelCount", result.value("labelCount")},
    {"entityLabelCount", result.value("entityLabelCount")},
    {"orderLabelCount", result.value("orderLabelCount")},
    {"partLabelCount", result.value("partLabelCount")},
    {"simplified", result.value("simplified")},
    {"verticalExaggeration", result.value("verticalExaggeration")},
  };

  const auto contactSheet = result.value("contactSheet").toObject();
  if (!contactSheet.isEmpty())
  {
    compact.insert(
      "contactSheet",
      QJsonObject{
        {"path", contactSheet.value("path")},
        {"absolutePath", contactSheet.value("absolutePath")},
        {"width", contactSheet.value("width")},
        {"height", contactSheet.value("height")},
        {"fileSize", contactSheet.value("fileSize")},
        {"valid", contactSheet.value("valid")},
      });
  }
  return compact;
}

QString idsModeFromParams(const QJsonObject& params)
{
  const auto mode = params.value("idsMode")
                      .toString(params.value("detail").toString("summary"))
                      .trimmed()
                      .toLower();
  if (mode == "full" || mode == "ids")
  {
    return "full";
  }
  if (mode == "sample")
  {
    return "sample";
  }
  if (mode == "none")
  {
    return "none";
  }
  return "count";
}

QJsonObject normalizedReviewParams(QJsonObject params)
{
  const auto preset = params.value("preset").toString().trimmed().toLower();
  if (preset == "route_platform")
  {
    if (!params.contains("style"))
    {
      params.insert("style", "whitebox_edges");
    }
    if (!params.contains("edgeMode"))
    {
      params.insert("edgeMode", "all");
    }
    if (!params.contains("verticalExaggeration"))
    {
      params.insert("verticalExaggeration", 1.6);
    }
    if (!params.contains("views"))
    {
      params.insert(
        "views",
        QJsonArray{
          "iso_overview_ne",
          "iso_overview_sw",
          "top_plan",
          "side_elevation_long",
        });
    }
    if (!params.contains("includeOrderLabels"))
    {
      params.insert("includeOrderLabels", true);
    }
    if (!params.contains("includeDirectionLabels"))
    {
      params.insert("includeDirectionLabels", true);
    }
  }
  return params;
}

void applyIdsMode(
  QJsonObject& result,
  const QString& fieldName,
  const QStringList& ids,
  const QString& idsMode)
{
  result.insert(fieldName + "Count", ids.size());
  result.insert("idsMode", idsMode);
  if (idsMode == "full")
  {
    result.insert(fieldName, QJsonArray::fromStringList(ids));
    return;
  }
  result.remove(fieldName);
  if (idsMode == "sample")
  {
    constexpr auto SampleLimit = qsizetype{12};
    result.insert(
      fieldName + "Sample",
      QJsonArray::fromStringList(ids.mid(0, std::min(ids.size(), SampleLimit))));
  }
}

McpBridgeToolResult renderReviewNodesForMapResult(
  const QString& toolName,
  mdl::Map& map,
  const std::vector<mdl::Node*>& nodes,
  const QJsonObject& rawParams,
  const McpObjectRegistry* objectRegistry,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore)
{
  const auto params = normalizedReviewParams(rawParams);
  auto warnings = QJsonArray{};
  if (nodes.empty())
  {
    return McpBridgeToolResult::success(QJsonObject{
      {"tool", toolName},
      {"renderer", "geometry_cpu"},
      {"reviewId", QString{}},
      {"targetObjectCount", 0},
      {"targetBrushCount", 0},
      {"unsupportedObjectCount", 0},
      {"captureCount", 0},
      {"qualityValid", false},
      {"edgeInterpretation", "none"},
      {"internalBrushEdgesDrawn", false},
      {"visualReviewRequired", false},
      {"semanticAcceptance", reviewSemanticAcceptance()},
      {"warnings", QJsonArray{"noReviewTargets"}},
      {"note", "No live map objects were available for review."},
    });
  }

  auto targetBounds = boundsForNodes(nodes);
  const auto maxFaces =
    optionalIntClamped(params, "maxDetailedFaces", MaxDetailedFaceCount, 100, 100000);
  const auto style = reviewStyleFromParams(params, warnings);
  const auto edgeMode = reviewEdgeModeFromParams(params, warnings);
  const auto verticalExaggeration = verticalExaggerationFromParams(params, warnings);
  const auto labelOptions =
    labelOptionsFromParams(params, static_cast<int>(nodes.size()), warnings);
  auto geometry = buildRenderGeometry(
    map, nodes, warnings, maxFaces, labelOptions, objectRegistry, metadataStore);
  if (geometry.faces.empty() && geometry.edges.empty())
  {
    addBboxToGeometry(geometry, targetBounds);
    appendWarning(warnings, "emptyGeometryFallback: target rendered as bounds only.");
  }
  applyVerticalExaggerationToGeometry(geometry, targetBounds, verticalExaggeration);
  if (edgeMode == ReviewEdgeMode::Silhouette && geometry.simplified)
  {
    appendWarning(
      warnings,
      "silhouetteLowConfidence: detailed geometry exceeded the face budget and was "
      "simplified to bounds.");
  }
  if (!geometry.faces.empty() || !geometry.edges.empty())
  {
    targetBounds = boundsForGeometry(geometry);
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
  const auto combineViews = optionalBool(params, "combineViews", true);
  const auto contactSheetMaxCaptures = optionalIntClamped(
    params, "contactSheetMaxCaptures", DefaultContactSheetMaxCaptures, 1, 16);
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
      targetBounds,
      view,
      style,
      edgeMode,
      imageSize,
      outputPath,
      includeAxes,
      includeBoundsBox);
    captures.push_back(capture);
    quality.push_back(capture.value("quality").toObject());
  }

  auto contactSheet = QJsonObject{};
  auto contactSheetPath = QString{};
  if (combineViews && !captures.isEmpty())
  {
    auto contactSheetSize = QSize{
      std::clamp(imageSize.width() * 2, MinWidth, 4096),
      std::clamp(
        static_cast<int>(std::round(imageSize.height() * 1.55)), MinHeight, 4096),
    };
    if (const auto contactSizeValue = params.value("contactSheetSize");
        contactSizeValue.isArray())
    {
      const auto array = contactSizeValue.toArray();
      if (array.size() == 2 && array[0].isDouble() && array[1].isDouble())
      {
        contactSheetSize = QSize{
          std::clamp(array[0].toInt(contactSheetSize.width()), MinWidth, 4096),
          std::clamp(array[1].toInt(contactSheetSize.height()), MinHeight, 4096),
        };
      }
    }

    contactSheetPath = pathToQString(outputDir / "contact_sheet.png");
    const auto contactSheetCaptures =
      capturesForContactSheet(captures, contactSheetMaxCaptures);
    contactSheet = writeContactSheet(
      contactSheetCaptures,
      contactSheetPath,
      QString{"%1  |  %2  |  z:%3x  |  edges:%4  |  %5 brushes"}
        .arg(reviewId)
        .arg(reviewStyleName(style))
        .arg(verticalExaggeration)
        .arg(reviewEdgeModeName(edgeMode))
        .arg(geometry.targetBrushCount),
      contactSheetSize);
    annotateContactSheet(contactSheet, captures.size(), contactSheetMaxCaptures);
    if (!contactSheet.value("valid").toBool(false))
    {
      warnings.push_back("contactSheetInvalid");
    }
  }

  auto materialNames = QJsonArray{};
  for (const auto& materialName : geometry.materialNames)
  {
    materialNames.push_back(materialName);
  }

  const auto qualityValid =
    !captures.isEmpty() && std::ranges::all_of(quality, [](const auto& entry) {
      return entry.toObject().value("valid").toBool(false);
    });

  auto result = QJsonObject{
    {"tool", toolName},
    {"renderer", "geometry_cpu"},
    {"style", reviewStyleName(style)},
    {"verticalExaggeration", verticalExaggeration},
    {"edgeMode", reviewEdgeModeName(edgeMode)},
    {"edgeInterpretation",
     edgeMode == ReviewEdgeMode::Silhouette ? "silhouette"
     : edgeMode == ReviewEdgeMode::None     ? "none"
                                            : "construction"},
    {"internalBrushEdgesDrawn",
     edgeMode != ReviewEdgeMode::Silhouette && edgeMode != ReviewEdgeMode::None},
    {"visualReviewRequired", false},
    {"reviewId", reviewId},
    {"resourceUri", QString{"tbmcp://review/%1"}.arg(reviewId)},
    {"outputDir", pathToQString(outputDir)},
    {"manifestPath", pathToQString(outputDir / "manifest.json")},
    {"preferredCapturePath",
     contactSheetPath.isEmpty()
       ? (captures.isEmpty() ? QString{}
                             : captures.first().toObject().value("path").toString())
       : contactSheetPath},
    {"contactSheet", contactSheet},
    {"targetObjectCount", geometry.targetObjectCount},
    {"targetBrushCount", geometry.targetBrushCount},
    {"unsupportedObjectCount", static_cast<int>(geometry.unsupportedNodes.size())},
    {"targetBounds", boundsToJson(targetBounds)},
    {"captureCount", captures.size()},
    {"captures", captures},
    {"quality", quality},
    {"qualityValid", qualityValid},
    {"semanticAcceptance", reviewSemanticAcceptance()},
    {"warnings", warnings},
    {"materials", materialNames},
    {"maxDetailedFaces", maxFaces},
    {"simplified", geometry.simplified},
    {"faceCount", static_cast<int>(geometry.faces.size())},
    {"edgeCount", static_cast<int>(geometry.edges.size())},
    {"labelCount", static_cast<int>(geometry.labels.size())},
    {"entityLabelCount", geometry.entityLabelCount},
    {"orderLabelCount", geometry.orderLabelCount},
    {"partLabelCount", geometry.partLabelCount},
  };
  addAbsoluteReviewPaths(result);
  if (!writeManifest(outputDir / "manifest.json", result))
  {
    auto updatedWarnings = result.value("warnings").toArray();
    updatedWarnings.push_back("manifestWriteFailed");
    result.insert("warnings", updatedWarnings);
    result.insert("qualityValid", false);
  }

  if (params.value("detail").toString("summary").trimmed().toLower() != "full")
  {
    result = compactReviewResult(result, toolName);
  }
  return McpBridgeToolResult::success(result);
}

} // namespace

McpBridgeToolResult renderReviewTargetsForMapResult(
  mdl::Map& map,
  const QJsonObject& rawParams,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore)
{
  const auto params = normalizedReviewParams(rawParams);
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
      {"edgeInterpretation", "none"},
      {"internalBrushEdgesDrawn", false},
      {"visualReviewRequired", false},
      {"semanticAcceptance", reviewSemanticAcceptance()},
      {"warnings", warnings},
      {"note", "render_review_targets requires live operationIds, objectIds, or bounds."},
    });
  }

  const auto maxFaces =
    optionalIntClamped(params, "maxDetailedFaces", MaxDetailedFaceCount, 100, 100000);
  const auto style = reviewStyleFromParams(params, warnings);
  const auto edgeMode = reviewEdgeModeFromParams(params, warnings);
  const auto verticalExaggeration = verticalExaggerationFromParams(params, warnings);
  const auto labelOptions =
    labelOptionsFromParams(params, static_cast<int>(nodes.size()), warnings);
  auto geometry = buildRenderGeometry(
    map, nodes, warnings, maxFaces, labelOptions, objectRegistry, metadataStore);
  if (geometry.faces.empty() && geometry.edges.empty())
  {
    addBboxToGeometry(geometry, *targetBounds);
    appendWarning(warnings, "emptyGeometryFallback: target rendered as bounds only.");
  }
  applyVerticalExaggerationToGeometry(geometry, *targetBounds, verticalExaggeration);
  if (edgeMode == ReviewEdgeMode::Silhouette && geometry.simplified)
  {
    appendWarning(
      warnings,
      "silhouetteLowConfidence: detailed geometry exceeded the face budget and was "
      "simplified to bounds.");
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
  const auto combineViews = optionalBool(params, "combineViews", true);
  const auto contactSheetMaxCaptures = optionalIntClamped(
    params, "contactSheetMaxCaptures", DefaultContactSheetMaxCaptures, 1, 16);
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
      style,
      edgeMode,
      imageSize,
      outputPath,
      includeAxes,
      includeBoundsBox);
    captures.push_back(capture);
    quality.push_back(capture.value("quality").toObject());
  }

  auto contactSheet = QJsonObject{};
  auto contactSheetPath = QString{};
  if (combineViews && !captures.isEmpty())
  {
    auto contactSheetSize = QSize{
      std::clamp(imageSize.width() * 2, MinWidth, 4096),
      std::clamp(
        static_cast<int>(std::round(imageSize.height() * 1.55)), MinHeight, 4096),
    };
    if (const auto contactSizeValue = params.value("contactSheetSize");
        contactSizeValue.isArray())
    {
      const auto array = contactSizeValue.toArray();
      if (array.size() == 2 && array[0].isDouble() && array[1].isDouble())
      {
        contactSheetSize = QSize{
          std::clamp(array[0].toInt(contactSheetSize.width()), MinWidth, 4096),
          std::clamp(array[1].toInt(contactSheetSize.height()), MinHeight, 4096),
        };
      }
    }

    contactSheetPath = pathToQString(outputDir / "contact_sheet.png");
    const auto contactSheetCaptures =
      capturesForContactSheet(captures, contactSheetMaxCaptures);
    contactSheet = writeContactSheet(
      contactSheetCaptures,
      contactSheetPath,
      QString{"%1  |  %2  |  z:%3x  |  edges:%4  |  %5 brushes"}
        .arg(reviewId)
        .arg(reviewStyleName(style))
        .arg(verticalExaggeration)
        .arg(reviewEdgeModeName(edgeMode))
        .arg(geometry.targetBrushCount),
      contactSheetSize);
    annotateContactSheet(contactSheet, captures.size(), contactSheetMaxCaptures);
    if (!contactSheet.value("valid").toBool(false))
    {
      warnings.push_back("contactSheetInvalid");
    }
  }

  auto materialNames = QJsonArray{};
  for (const auto& materialName : geometry.materialNames)
  {
    materialNames.push_back(materialName);
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
    {"style", reviewStyleName(style)},
    {"verticalExaggeration", verticalExaggeration},
    {"edgeMode", reviewEdgeModeName(edgeMode)},
    {"edgeInterpretation",
     edgeMode == ReviewEdgeMode::Silhouette ? "silhouette"
     : edgeMode == ReviewEdgeMode::None     ? "none"
                                            : "construction"},
    {"internalBrushEdgesDrawn",
     edgeMode != ReviewEdgeMode::Silhouette && edgeMode != ReviewEdgeMode::None},
    {"visualReviewRequired", false},
    {"reviewId", reviewId},
    {"resourceUri", QString{"tbmcp://review/%1"}.arg(reviewId)},
    {"outputDir", pathToQString(outputDir)},
    {"manifestPath", pathToQString(outputDir / "manifest.json")},
    {"preferredCapturePath",
     contactSheetPath.isEmpty()
       ? (captures.isEmpty() ? QString{}
                             : captures.first().toObject().value("path").toString())
       : contactSheetPath},
    {"contactSheet", contactSheet},
    {"targetObjectIds", QJsonArray::fromStringList(resolvedObjectIds)},
    {"targetObjectCount", geometry.targetObjectCount},
    {"targetBrushCount", geometry.targetBrushCount},
    {"unsupportedObjectCount", static_cast<int>(geometry.unsupportedNodes.size())},
    {"targetBounds", boundsToJson(*targetBounds)},
    {"captureCount", captures.size()},
    {"captures", captures},
    {"quality", quality},
    {"qualityValid", qualityValid},
    {"semanticAcceptance", reviewSemanticAcceptance()},
    {"warnings", warnings},
    {"materials", materialNames},
    {"maxDetailedFaces", maxFaces},
    {"simplified", geometry.simplified},
    {"faceCount", static_cast<int>(geometry.faces.size())},
    {"edgeCount", static_cast<int>(geometry.edges.size())},
    {"labelCount", static_cast<int>(geometry.labels.size())},
    {"entityLabelCount", geometry.entityLabelCount},
    {"orderLabelCount", geometry.orderLabelCount},
    {"partLabelCount", geometry.partLabelCount},
  };
  addAbsoluteReviewPaths(result);

  if (!writeManifest(outputDir / "manifest.json", result))
  {
    auto updatedWarnings = result.value("warnings").toArray();
    updatedWarnings.push_back("manifestWriteFailed");
    result.insert("warnings", updatedWarnings);
    result.insert("qualityValid", false);
  }

  applyIdsMode(result, "targetObjectIds", resolvedObjectIds, idsModeFromParams(params));
  if (params.value("detail").toString().trimmed().toLower() == "summary")
  {
    result = compactReviewResult(result, "render_review_targets");
  }
  return McpBridgeToolResult::success(result);
}

McpBridgeToolResult renderReviewTargetsResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }
  return renderReviewTargetsForMapResult(
    mapWindow->document().map(), params, history, objectRegistry, metadataStore);
}

McpBridgeToolResult renderReviewCurrentSceneForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore)
{
  auto warnings = QJsonArray{};
  const auto scope = params.value("scope").toString("all").trimmed().toLower();
  auto nodes = std::vector<mdl::Node*>{};
  if (scope == "mcp_history")
  {
    nodes = nodesFromMcpHistory(map, history, objectRegistry, warnings);
  }
  else if (scope == "selection")
  {
    nodes.reserve(map.selection().nodes.size());
    for (auto* node : map.selection().nodes)
    {
      if (node != nullptr)
      {
        nodes.push_back(node);
      }
    }
    nodes = dedupeNodes(std::move(nodes));
  }
  else if (scope == "all" || scope.isEmpty())
  {
    auto brushNodes = std::vector<mdl::BrushNode*>{};
    collectBrushNodes(map.worldNode(), brushNodes);
    brushNodes = dedupeNodes(std::move(brushNodes));
    nodes.reserve(brushNodes.size());
    for (auto* brushNode : brushNodes)
    {
      nodes.push_back(brushNode);
    }
  }
  else
  {
    return invalidParamsFailure(
      "render_review_current_scene scope must be all, mcp_history, or selection");
  }

  auto reviewParams = params;
  reviewParams.remove("scope");
  const auto targetBounds = nodes.empty()
                              ? std::optional<vm::bbox3d>{}
                              : std::optional<vm::bbox3d>{boundsForNodes(nodes)};
  const auto preset = reviewParams.value("preset").toString("auto").trimmed().toLower();
  const auto boundsSize =
    targetBounds ? targetBounds->max - targetBounds->min : vm::vec3d{};
  const auto brushCount = std::ranges::count_if(nodes, [](const auto* node) {
    return dynamic_cast<const mdl::BrushNode*>(node) != nullptr;
  });
  const auto terrainLike =
    targetBounds
    && (brushCount >= 120
        || (boundsSize.z() > 0.0
            && boundsSize.z() < std::max(boundsSize.x(), boundsSize.y()) * 0.18));

  if (!reviewParams.contains("style"))
  {
    reviewParams.insert(
      "style",
      preset == "whitebox"         ? "whitebox_edges"
      : preset == "route_platform" ? "whitebox_edges"
      : preset == "material"       ? "material_tint_edges"
      : preset == "building"       ? "material_tint_edges"
      : preset == "terrain"        ? "height_heatmap_edges"
      : preset == "terrain_route"  ? "height_heatmap_edges"
      : terrainLike                ? "height_heatmap_edges"
                                   : "material_tint_edges");
  }
  if (!reviewParams.contains("edgeMode"))
  {
    reviewParams.insert(
      "edgeMode",
      preset == "route_platform" ? "all"
      : reviewParams.value("style").toString().trimmed().toLower()
          == "height_heatmap_edges"
        ? "none"
        : "minimal");
  }
  if (!reviewParams.contains("views"))
  {
    reviewParams.insert(
      "views",
      preset == "route_platform"
        ? QJsonArray{
            "iso_overview_ne",
            "iso_overview_sw",
            "top_plan",
            "side_elevation_long",
          }
        : QJsonArray{
            "iso_overview_ne",
            "top_plan",
            "side_elevation_long",
            "front_elevation_cross",
          });
  }
  if (!reviewParams.contains("verticalExaggeration") && preset == "route_platform")
  {
    reviewParams.insert("verticalExaggeration", 1.6);
  }
  if (!reviewParams.contains("imageSize"))
  {
    reviewParams.insert("imageSize", QJsonArray{1200, 850});
  }
  if (!reviewParams.contains("contactSheetSize"))
  {
    reviewParams.insert("contactSheetSize", QJsonArray{1800, 1400});
  }
  if (!reviewParams.contains("combineViews"))
  {
    reviewParams.insert("combineViews", true);
  }
  if (!reviewParams.contains("detail"))
  {
    reviewParams.insert("detail", "summary");
  }

  auto result = renderReviewNodesForMapResult(
    "render_review_current_scene",
    map,
    nodes,
    reviewParams,
    objectRegistry,
    metadataStore);
  if (result.ok)
  {
    result.result.insert("preset", preset);
    result.result.insert("autoPreset", terrainLike ? "terrain_route" : "building");
    result.result.insert("scope", scope.isEmpty() ? "all" : scope);
    if (!warnings.isEmpty())
    {
      auto combinedWarnings = result.result.value("warnings").toArray();
      for (const auto& warning : warnings)
      {
        combinedWarnings.push_back(warning);
      }
      result.result.insert("warnings", combinedWarnings);
    }
  }
  return result;
}

McpBridgeToolResult renderReviewCurrentSceneResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr)
  {
    return noActiveDocumentFailure();
  }

  return renderReviewCurrentSceneForMapResult(
    mapWindow->document().map(), params, history, objectRegistry, metadataStore);
}

} // namespace tb::ui
