/*
 Copyright (C) 2026 XiangXtreme

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
#include "McpToolSupport.h"
#include "mcp/McpError.h"
#include "mdl/BrushNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Selection.h"
#include "mdl/Node.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/mcp/McpObjectRegistry.h"

#include "vm/bbox.h"
#include "vm/vec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <vector>

namespace tb::ui
{
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

std::optional<std::vector<QString>> stringListFromJson(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto value = params.value(key);
  if (!value.isArray())
  {
    error = QString{"%1 must be an array of strings"}.arg(key);
    return std::nullopt;
  }

  auto result = std::vector<QString>{};
  for (const auto& entry : value.toArray())
  {
    if (!entry.isString())
    {
      error = QString{"%1 must contain only strings"}.arg(key);
      return std::nullopt;
    }
    result.push_back(entry.toString());
  }
  return result;
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

  auto values = std::array<double, 3>{};
  for (auto i = 0; i < 3; ++i)
  {
    if (!array[i].isDouble())
    {
      error = QString{"%1[%2] must be a number"}.arg(key).arg(i);
      return std::nullopt;
    }
    values[static_cast<size_t>(i)] = array[i].toDouble();
  }
  return vm::vec3d{values[0], values[1], values[2]};
}

std::optional<vm::vec3d> optionalVec3FromJson(
  const QJsonObject& params,
  const QString& key,
  const vm::vec3d& defaultValue,
  QString& error)
{
  if (!params.contains(key))
  {
    return defaultValue;
  }
  return vec3FromJson(params, key, error);
}

std::optional<vm::vec2d> vec2FromJsonValue(const QJsonValue& value)
{
  if (!value.isArray())
  {
    return std::nullopt;
  }
  const auto array = value.toArray();
  if (array.size() < 2 || !array[0].isDouble() || !array[1].isDouble())
  {
    return std::nullopt;
  }
  const auto x = array[0].toDouble();
  const auto y = array[1].toDouble();
  if (!std::isfinite(x) || !std::isfinite(y))
  {
    return std::nullopt;
  }
  auto result = vm::vec2d{x, y};
  if (vm::is_zero(result, vm::Cd::almost_zero()))
  {
    return std::nullopt;
  }
  return vm::normalize(result);
}

QJsonArray vecToJson(const vm::vec3d& value)
{
  return QJsonArray{value.x(), value.y(), value.z()};
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

QJsonObject shapeJson(
  const QString& name,
  const QString& purpose,
  const QString& parameters,
  const bool polygonBatch,
  const QJsonArray& points2dExample)
{
  return QJsonObject{
    {"name", name},
    {"purpose", purpose},
    {"parameters", parameters},
    {"polygonBatch", polygonBatch},
    {"points2dExample", points2dExample},
  };
}

QJsonArray points2dJson(std::initializer_list<std::initializer_list<double>> points)
{
  auto result = QJsonArray{};
  for (const auto& point : points)
  {
    auto pointArray = QJsonArray{};
    for (const auto value : point)
    {
      pointArray.push_back(value);
    }
    result.push_back(pointArray);
  }
  return result;
}

bool nearlyEqual(const double lhs, const double rhs, const double epsilon = 0.001)
{
  return std::abs(lhs - rhs) <= epsilon;
}

double snapToGrid(const double value, const double grid)
{
  return grid > 0.0 && std::isfinite(grid) ? std::round(value / grid) * grid : value;
}

QJsonArray vec2PointsToJson(const std::vector<vm::vec2d>& points)
{
  auto result = QJsonArray{};
  for (const auto& point : points)
  {
    result.push_back(QJsonArray{point.x(), point.y()});
  }
  return result;
}

std::optional<QJsonArray> cleanedPoints2dArray(
  const QJsonValue& value, const double grid, QString& error)
{
  if (!value.isArray())
  {
    error = "points2d must be an array";
    return std::nullopt;
  }

  auto points = std::vector<vm::vec2d>{};
  for (const auto& pointValue : value.toArray())
  {
    if (!pointValue.isArray())
    {
      error = "points2d must contain [x,y] arrays";
      return std::nullopt;
    }
    const auto pointArray = pointValue.toArray();
    if (pointArray.size() < 2 || !pointArray[0].isDouble() || !pointArray[1].isDouble())
    {
      error = "points2d points must contain x and y numbers";
      return std::nullopt;
    }
    const auto point = vm::vec2d{
      snapToGrid(pointArray[0].toDouble(), grid),
      snapToGrid(pointArray[1].toDouble(), grid)};
    if (
      points.empty() || !nearlyEqual(points.back().x(), point.x())
      || !nearlyEqual(points.back().y(), point.y()))
    {
      points.push_back(point);
    }
  }
  if (
    points.size() > 1 && nearlyEqual(points.front().x(), points.back().x())
    && nearlyEqual(points.front().y(), points.back().y()))
  {
    points.pop_back();
  }

  auto changed = true;
  while (changed && points.size() >= 3)
  {
    changed = false;
    for (auto i = size_t{0}; i < points.size(); ++i)
    {
      const auto& previous = points[(i + points.size() - 1) % points.size()];
      const auto& current = points[i];
      const auto& next = points[(i + 1) % points.size()];
      const auto ab = current - previous;
      const auto bc = next - current;
      const auto cross = ab.x() * bc.y() - ab.y() * bc.x();
      if (std::abs(cross) <= 0.001)
      {
        points.erase(points.begin() + static_cast<std::ptrdiff_t>(i));
        changed = true;
        break;
      }
    }
  }

  auto result = QJsonArray{};
  for (const auto& point : points)
  {
    result.push_back(QJsonArray{point.x(), point.y()});
  }
  return result;
}

std::optional<std::vector<vm::vec2d>> vec2PointsFromJsonArray(
  const QJsonArray& array, QString& error)
{
  auto result = std::vector<vm::vec2d>{};
  result.reserve(static_cast<size_t>(array.size()));
  for (auto i = 0; i < array.size(); ++i)
  {
    const auto value = array[i];
    if (!value.isArray())
    {
      error = QString{"points2d[%1] must be [x,y]"}.arg(i);
      return std::nullopt;
    }
    const auto point = value.toArray();
    if (point.size() < 2 || !point[0].isDouble() || !point[1].isDouble())
    {
      error = QString{"points2d[%1] must contain x and y numbers"}.arg(i);
      return std::nullopt;
    }
    result.emplace_back(point[0].toDouble(), point[1].toDouble());
  }
  return result;
}

double polygonSignedArea(const std::vector<vm::vec2d>& points)
{
  auto area = 0.0;
  for (auto i = size_t{0}; i < points.size(); ++i)
  {
    const auto& current = points[i];
    const auto& next = points[(i + 1) % points.size()];
    area += current.x() * next.y() - next.x() * current.y();
  }
  return area * 0.5;
}

QJsonObject polygonConvexityDiagnostic(
  const std::vector<vm::vec2d>& points, const int brushIndex)
{
  auto result = QJsonObject{
    {"brushIndex", brushIndex},
    {"pointCount", static_cast<int>(points.size())},
  };

  if (points.size() < 3)
  {
    result.insert("valid", false);
    result.insert(
      "reason", "points2d must contain at least 3 unique non-collinear points");
    return result;
  }

  auto sign = 0;
  auto failingVertices = QJsonArray{};
  auto failingEdges = QJsonArray{};
  for (auto i = size_t{0}; i < points.size(); ++i)
  {
    const auto previousIndex = (i + points.size() - 1) % points.size();
    const auto nextIndex = (i + 1) % points.size();
    const auto& previous = points[previousIndex];
    const auto& current = points[i];
    const auto& next = points[nextIndex];
    const auto incoming = current - previous;
    const auto outgoing = next - current;
    const auto cross = incoming.x() * outgoing.y() - incoming.y() * outgoing.x();
    if (std::abs(cross) <= 0.001)
    {
      failingVertices.push_back(static_cast<int>(i));
      failingEdges.push_back(QJsonObject{
        {"from", static_cast<int>(previousIndex)},
        {"through", static_cast<int>(i)},
        {"to", static_cast<int>(nextIndex)},
        {"reason", "collinear_or_duplicate_after_grid_snap"},
        {"cross", cross},
      });
      continue;
    }

    const auto currentSign = cross > 0.0 ? 1 : -1;
    if (sign == 0)
    {
      sign = currentSign;
    }
    else if (sign != currentSign)
    {
      failingVertices.push_back(static_cast<int>(i));
      failingEdges.push_back(QJsonObject{
        {"from", static_cast<int>(previousIndex)},
        {"through", static_cast<int>(i)},
        {"to", static_cast<int>(nextIndex)},
        {"reason", "concave_turn"},
        {"cross", cross},
      });
    }
  }

  result.insert("valid", failingVertices.isEmpty());
  if (!failingVertices.isEmpty())
  {
    result.insert("reason", "points2d must form a strictly convex polygon");
    result.insert("failingPointIndices", failingVertices);
    result.insert("failingEdges", failingEdges);
  }
  result.insert("signedArea", polygonSignedArea(points));
  result.insert("points2d", vec2PointsToJson(points));
  return result;
}

QJsonObject metadataForObject(
  const QString& objectId,
  const QString& documentFingerprint,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  const auto scopedKey = documentFingerprint.isEmpty()
                           ? objectId
                           : QString{"%1|%2"}.arg(documentFingerprint, objectId);
  auto it = metadataStore.find(scopedKey);
  if (it == metadataStore.end())
  {
    it = metadataStore.find(objectId);
  }
  if (
    it == metadataStore.end() || it->second.stale
    || (!it->second.documentFingerprint.isEmpty() && it->second.documentFingerprint != documentFingerprint))
  {
    return {};
  }
  return it->second.metadata;
}

bool recordMatchesDocument(
  const McpBrushMetadataRecord& record, const QString& documentFingerprint)
{
  return record.documentFingerprint.isEmpty()
         || record.documentFingerprint == documentFingerprint;
}

QString metadataStoreKey(const QString& documentFingerprint, const QString& objectId)
{
  return documentFingerprint.isEmpty()
           ? objectId
           : QString{"%1|%2"}.arg(documentFingerprint, objectId);
}

bool metadataObjectMatches(const QJsonObject& metadata, const QJsonObject& expected)
{
  for (const auto& key : expected.keys())
  {
    if (metadata.value(key) != expected.value(key))
    {
      return false;
    }
  }
  return true;
}

bool isMetadataControlParam(const QString& key)
{
  static const auto Keys = QStringList{
    "detail",
    "limit",
    "metadata",
    "metadataOrder",
    "objectIds",
    "playerHull",
    "select",
  };
  return Keys.contains(key);
}

QJsonObject expectedMetadataFromParams(const QJsonObject& params)
{
  auto result = params.value("metadata").isObject() ? params.value("metadata").toObject()
                                                    : QJsonObject{};
  for (auto it = params.begin(); it != params.end(); ++it)
  {
    if (!isMetadataControlParam(it.key()))
    {
      result.insert(it.key(), it.value());
    }
  }
  return result;
}

bool allMetadataFiltersMatch(const QJsonObject& metadata, const QJsonObject& params)
{
  return metadataObjectMatches(metadata, expectedMetadataFromParams(params));
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

QString detailFromParams(const QJsonObject& params)
{
  const auto detail = params.value("detail").toString("summary").trimmed().toLower();
  return detail == "full" || detail == "ids" ? detail : QString{"summary"};
}

QJsonArray stringVectorToJson(const std::vector<QString>& values)
{
  auto result = QJsonArray{};
  for (const auto& value : values)
  {
    result.push_back(value);
  }
  return result;
}

std::vector<QString> matchingMetadataObjectIds(
  const QJsonObject& params,
  const QString& documentFingerprint,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const size_t limit)
{
  auto result = std::vector<QString>{};
  for (const auto& [objectId, record] : metadataStore)
  {
    Q_UNUSED(objectId);
    if (
      record.stale || record.metadata.isEmpty()
      || !recordMatchesDocument(record, documentFingerprint))
    {
      continue;
    }
    if (!allMetadataFiltersMatch(record.metadata, params))
    {
      continue;
    }
    result.push_back(record.objectId);
    if (result.size() >= limit)
    {
      break;
    }
  }
  return result;
}

void sortMetadataObjectIdsByOrder(
  std::vector<QString>& objectIds,
  const QString& documentFingerprint,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  QJsonArray& warnings)
{
  const auto allHaveOrder = std::ranges::all_of(objectIds, [&](const auto& objectId) {
    const auto it = std::ranges::find_if(metadataStore, [&](const auto& entry) {
      return entry.second.objectId == objectId && !entry.second.stale
             && recordMatchesDocument(entry.second, documentFingerprint);
    });
    return it != metadataStore.end() && it->second.metadata.value("order").isDouble();
  });
  if (!allHaveOrder)
  {
    warnings.push_back(
      "Route order was inferred from MCP object ids because one or more metadata records "
      "do not define numeric order. Pass ordered objectIds or set metadata.order for "
      "exact "
      "chain order.");
    return;
  }

  std::ranges::stable_sort(objectIds, [&](const auto& lhs, const auto& rhs) {
    const auto lhsIt = std::ranges::find_if(metadataStore, [&](const auto& entry) {
      return entry.second.objectId == lhs && !entry.second.stale
             && recordMatchesDocument(entry.second, documentFingerprint);
    });
    const auto rhsIt = std::ranges::find_if(metadataStore, [&](const auto& entry) {
      return entry.second.objectId == rhs && !entry.second.stale
             && recordMatchesDocument(entry.second, documentFingerprint);
    });
    return lhsIt->second.metadata.value("order").toDouble()
           < rhsIt->second.metadata.value("order").toDouble();
  });
}

std::vector<mdl::BrushNode*> liveBrushNodesFromObjectIds(
  mdl::Map& map, const std::vector<QString>& objectIds, QString& error)
{
  auto result = std::vector<mdl::BrushNode*>{};
  for (const auto& objectId : objectIds)
  {
    auto* node = resolveNodeId(map.worldNode(), objectId);
    auto* brush = dynamic_cast<mdl::BrushNode*>(node);
    if (!brush)
    {
      error = QString{"Object id is not a live brush: %1"}.arg(objectId);
      return {};
    }
    result.push_back(brush);
  }
  return result;
}

std::optional<QJsonObject> metadataFromJsonValue(
  const QJsonValue& value, const QString& key, QString& error)
{
  if (value.isUndefined() || value.isNull())
  {
    return QJsonObject{};
  }
  if (!value.isObject())
  {
    error = QString{"%1 must be an object"}.arg(key);
    return std::nullopt;
  }

  return value.toObject();
}

std::optional<std::vector<QJsonObject>> metadataArrayFromBrushes(
  const QJsonArray& brushes, QString& error)
{
  auto result = std::vector<QJsonObject>{};
  result.reserve(static_cast<size_t>(brushes.size()));

  for (auto i = 0; i < brushes.size(); ++i)
  {
    if (!brushes[i].isObject())
    {
      error = QString{"brushes[%1] must be an object"}.arg(i);
      return std::nullopt;
    }
    const auto brush = brushes[i].toObject();
    auto metadata = metadataFromJsonValue(
      brush.value("metadata"), QString{"brushes[%1].metadata"}.arg(i), error);
    if (!metadata)
    {
      return std::nullopt;
    }
    result.push_back(*metadata);
  }
  return result;
}

vm::vec2d platformCenter2D(const mdl::BrushNode& node)
{
  const auto& bounds = node.logicalBounds();
  const auto center = bounds.center();
  return vm::vec2d{center.x(), center.y()};
}

double platformArea2D(const mdl::BrushNode& node)
{
  const auto& bounds = node.logicalBounds();
  return std::max(0.0, bounds.size().x()) * std::max(0.0, bounds.size().y());
}

vm::vec2d directionBetween(const mdl::BrushNode& from, const mdl::BrushNode& to)
{
  auto delta = platformCenter2D(to) - platformCenter2D(from);
  if (vm::is_zero(delta, vm::Cd::almost_zero()))
  {
    return vm::vec2d{1, 0};
  }
  return vm::normalize(delta);
}

QJsonObject segmentMetricsJson(
  const mdl::BrushNode& from,
  const mdl::BrushNode& to,
  const mdl::WorldNode& worldNode,
  const QJsonObject& fromMetadata,
  const QJsonObject& toMetadata,
  const double hullWidth,
  const int index)
{
  auto metadataDirection = vec2FromJsonValue(fromMetadata.value("outgoingDirection"));
  if (!metadataDirection)
  {
    metadataDirection = vec2FromJsonValue(toMetadata.value("incomingDirection"));
  }
  const auto direction =
    metadataDirection ? *metadataDirection : directionBetween(from, to);
  const auto delta = platformCenter2D(to) - platformCenter2D(from);
  const auto centerDistance = vm::length(delta);
  const auto fromBounds = from.logicalBounds();
  const auto toBounds = to.logicalBounds();
  const auto fromHalfAlong = std::abs(direction.x()) * fromBounds.size().x() / 2.0
                             + std::abs(direction.y()) * fromBounds.size().y() / 2.0;
  const auto toHalfAlong = std::abs(direction.x()) * toBounds.size().x() / 2.0
                           + std::abs(direction.y()) * toBounds.size().y() / 2.0;
  const auto edgeGap = std::max(0.0, centerDistance - fromHalfAlong - toHalfAlong);
  const auto heightDelta = toBounds.max.z() - fromBounds.max.z();
  const auto verticalGap =
    toBounds.min.z() > fromBounds.max.z()   ? toBounds.min.z() - fromBounds.max.z()
    : fromBounds.min.z() > toBounds.max.z() ? toBounds.max.z() - fromBounds.min.z()
                                            : 0.0;
  const auto lateralOffset =
    std::abs((-direction.y()) * delta.x() + direction.x() * delta.y());
  const auto landingWindowArea = platformArea2D(to);
  const auto badLandingPenalty =
    std::min(std::sqrt(std::max(0.0, landingWindowArea)), std::max(0.0, hullWidth));

  return QJsonObject{
    {"index", index},
    {"fromObjectId", nodePathId(from, worldNode)},
    {"toObjectId", nodePathId(to, worldNode)},
    {"edgeGap", edgeGap},
    {"effectiveDistanceIdeal", edgeGap},
    {"effectiveDistanceBadLanding", edgeGap + badLandingPenalty},
    {"heightDelta", heightDelta},
    {"verticalGap", verticalGap},
    {"lateralOffset", lateralOffset},
    {"landingWindowArea", landingWindowArea},
    {"direction", QJsonArray{direction.x(), direction.y()}},
    {"usedMetadataDirection", metadataDirection.has_value()},
  };
}

} // namespace

int storeBatchOperationMetadata(
  const QJsonArray& operations,
  const QStringList& changedObjectIds,
  std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  return storeBatchOperationMetadata(
    operations, changedObjectIds, {}, QJsonObject{}, metadataStore, nullptr, {});
}

int storeBatchOperationMetadata(
  const QJsonArray& operations,
  const QStringList& changedObjectIds,
  const QJsonObject& defaultMetadata,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>* moduleStore,
  const QString& operationId)
{
  return storeBatchOperationMetadata(
    operations,
    changedObjectIds,
    {},
    defaultMetadata,
    metadataStore,
    moduleStore,
    operationId);
}

int storeBatchOperationMetadata(
  const QJsonArray& operations,
  const QStringList& changedObjectIds,
  const QString& documentFingerprint,
  const QJsonObject& defaultMetadata,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>* moduleStore,
  const QString& operationId)
{
  if (changedObjectIds.isEmpty())
  {
    return 0;
  }

  const auto mergeMetadata = [](QJsonObject base, const QJsonObject& overlay) {
    for (auto it = overlay.begin(); it != overlay.end(); ++it)
    {
      base.insert(it.key(), it.value());
    }
    return base;
  };

  const auto partMetadata =
    [&](const QJsonObject& operation, const QString& partName, const QJsonObject& base) {
      auto result = base;
      if (!partName.isEmpty())
      {
        result.insert("part", partName);
      }
      if (operation.value("partMetadata").isObject())
      {
        const auto partMetadataObject = operation.value("partMetadata").toObject();
        if (partMetadataObject.value(partName).isObject())
        {
          result = mergeMetadata(result, partMetadataObject.value(partName).toObject());
        }
      }
      return result;
    };

  const auto partRequested = [](const QJsonObject& operation, const QString& partName) {
    if (!operation.value("parts").isArray())
    {
      return true;
    }
    const auto parts = operation.value("parts").toArray();
    if (parts.isEmpty())
    {
      return false;
    }
    for (const auto& part : parts)
    {
      if (part.toString().compare(partName, Qt::CaseInsensitive) == 0)
      {
        return true;
      }
    }
    return false;
  };
  const auto explicitPartRequested =
    [](const QJsonObject& operation, const QString& partName) {
      if (!operation.value("parts").isArray())
      {
        return false;
      }
      for (const auto& part : operation.value("parts").toArray())
      {
        if (part.toString().compare(partName, Qt::CaseInsensitive) == 0)
        {
          return true;
        }
      }
      return false;
    };

  std::function<void(const QJsonObject&, const QJsonObject&)> appendOperationMetadata;
  auto metadataByObject = std::vector<QJsonObject>{};
  appendOperationMetadata = [&](const QJsonObject& operation, const QJsonObject& parent) {
    auto metadata = mergeMetadata(parent, operation.value("metadata").toObject());
    if (metadata.isEmpty())
    {
      metadata = parent;
    }
    const auto operationType = operation.value("type").toString().trimmed();

    const auto appendRepeated = [&](const int count, const QString& part = {}) {
      for (auto i = 0; i < count; ++i)
      {
        metadataByObject.push_back(partMetadata(operation, part, metadata));
      }
    };

    if (operationType == "repeat_translate")
    {
      const auto count = std::clamp(operation.value("count").toInt(1), 1, 256);
      const auto child = operation.value("operation").toObject();
      for (auto i = 0; i < count; ++i)
      {
        appendOperationMetadata(child, metadata);
      }
      return;
    }
    if (operationType == "repeat_grid")
    {
      auto instanceCount = 1;
      const auto counts = operation.value("counts");
      if (counts.isDouble())
      {
        instanceCount = std::max(1, counts.toInt(1));
      }
      else if (counts.isArray())
      {
        for (const auto& value : counts.toArray())
        {
          instanceCount *= std::max(1, value.toInt(1));
        }
      }
      const auto child = operation.value("operation").toObject();
      for (auto i = 0; i < instanceCount; ++i)
      {
        appendOperationMetadata(child, metadata);
      }
      return;
    }
    if (operationType == "curved_corridor")
    {
      const auto segments = std::max(1, operation.value("segments").toInt(12));
      for (auto i = 0; i < segments; ++i)
      {
        if (partRequested(operation, "floor"))
        {
          metadataByObject.push_back(partMetadata(operation, "floor", metadata));
        }
        if (partRequested(operation, "ceiling"))
        {
          metadataByObject.push_back(partMetadata(operation, "ceiling", metadata));
        }
        if (partRequested(operation, "inner_wall"))
        {
          metadataByObject.push_back(partMetadata(operation, "inner_wall", metadata));
        }
        if (partRequested(operation, "outer_wall"))
        {
          metadataByObject.push_back(partMetadata(operation, "outer_wall", metadata));
        }
      }
      const auto caps = operation.value("caps").toString("none").trimmed().toLower();
      if ((caps == "start" || caps == "both") && partRequested(operation, "start_cap"))
      {
        metadataByObject.push_back(partMetadata(operation, "start_cap", metadata));
      }
      if ((caps == "end" || caps == "both") && partRequested(operation, "end_cap"))
      {
        metadataByObject.push_back(partMetadata(operation, "end_cap", metadata));
      }
      return;
    }
    if (operationType == "path_ribbon")
    {
      if (
        partRequested(operation, "surface") || partRequested(operation, "floor")
        || partRequested(operation, "ribbon"))
      {
        const auto points = operation.value("points3d").isArray()
                              ? operation.value("points3d").toArray()
                              : operation.value("points2d").toArray();
        const auto partName = explicitPartRequested(operation, "floor") ? QString{"floor"}
                              : explicitPartRequested(operation, "ribbon")
                                ? QString{"ribbon"}
                                : QString{"surface"};
        appendRepeated(std::max(1, static_cast<int>(points.size()) - 1), partName);
      }
      return;
    }
    if (operationType == "arc_ramp" || operationType == "helical_ramp")
    {
      appendRepeated(std::max(1, operation.value("segments").toInt(12)), "ramp");
      return;
    }
    if (operationType == "arc_ramp_segment")
    {
      appendRepeated(1, "ramp");
      return;
    }

    metadataByObject.push_back(metadata);
  };

  for (const auto& operationValue : operations)
  {
    if (!operationValue.isObject())
    {
      continue;
    }
    const auto operation = operationValue.toObject();
    appendOperationMetadata(operation, defaultMetadata);
  }

  auto metadataCount = 0;
  for (auto i = 0;
       i < changedObjectIds.size() && i < static_cast<int>(metadataByObject.size());
       ++i)
  {
    const auto objectId = changedObjectIds[i];
    const auto metadata = metadataByObject[static_cast<size_t>(i)];
    if (objectId.isEmpty() || metadata.isEmpty())
    {
      continue;
    }
    metadataStore[metadataStoreKey(documentFingerprint, objectId)] =
      McpBrushMetadataRecord{objectId, documentFingerprint, metadata, false};
    const auto moduleId = metadata.value("moduleId").toString().trimmed();
    if (moduleStore != nullptr && !moduleId.isEmpty())
    {
      const auto moduleKey = documentFingerprint.isEmpty()
                               ? moduleId
                               : QString{"%1|%2"}.arg(documentFingerprint, moduleId);
      auto& module = (*moduleStore)[moduleKey];
      module.moduleId = moduleId;
      module.documentFingerprint = documentFingerprint;
      module.metadata =
        mergeMetadata(module.metadata, moduleLevelMetadata(defaultMetadata));
      module.metadata = mergeMetadata(module.metadata, moduleLevelMetadata(metadata));
      module.objectIds.push_back(objectId);
      module.objectIds.removeDuplicates();
      if (!operationId.isEmpty())
      {
        module.operationIds.push_back(operationId);
        module.operationIds.removeDuplicates();
      }
    }
    ++metadataCount;
  }
  return metadataCount;
}

McpBridgeToolResult shapeLibraryListResult()
{
  return McpBridgeToolResult::success(QJsonObject{
    {"shapes",
     QJsonArray{
       shapeJson(
         "box",
         "Fast blockout/test block.",
         "center, size",
         true,
         points2dJson({{-64, -64}, {64, -64}, {64, 64}, {-64, 64}})),
       shapeJson(
         "diamond",
         "Directional landing/takeoff cue.",
         "center, width, depth",
         true,
         points2dJson({{0, -72}, {96, 0}, {0, 72}, {-96, 0}})),
       shapeJson(
         "trapezoid",
         "Bias landing window toward an outgoing edge.",
         "center, topWidth, bottomWidth, depth",
         true,
         points2dJson({{-96, -64}, {96, -64}, {56, 64}, {-56, 64}})),
       shapeJson(
         "chamfered_rect",
         "Trim meaningless corners from box-like platforms.",
         "center, width, depth, chamfer",
         true,
         points2dJson(
           {{-80, -64},
            {80, -64},
            {96, -48},
            {96, 48},
            {80, 64},
            {-80, 64},
            {-96, 48},
            {-96, -48}})),
       shapeJson(
         "half_hex",
         "Broad route turn with fewer false corners.",
         "center, radius",
         true,
         points2dJson({{-96, -56}, {0, -96}, {96, -56}, {96, 56}, {0, 96}, {-96, 56}})),
       shapeJson(
         "arrowhead",
         "Strong direction cue for next jump.",
         "center, width, depth",
         true,
         points2dJson({{-96, -64}, {32, -64}, {112, 0}, {32, 64}, {-96, 64}})),
       shapeJson(
         "slanted_plank",
         "Long narrow route-guiding ledge.",
         "center, length, width, angle",
         true,
         points2dJson({{-128, -32}, {128, -32}, {128, 32}, {-128, 32}})),
     }},
  });
}

McpBridgeToolResult brushCreatePolygonBatchResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return brushCreatePolygonBatchForMapResult(
    mapWindow->document().map(),
    toolName,
    params,
    history,
    nextOperationIndex,
    metadataStore);
}

McpBridgeToolResult brushCreatePolygonBatchForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  const auto brushesValue = params.value("brushes");
  if (!brushesValue.isArray())
  {
    return preMutationInvalidParamsFailure(
      "brush_create_polygon_batch requires brushes array",
      "provide_polygon_brushes_then_retry");
  }

  const auto brushes = brushesValue.toArray();
  if (brushes.isEmpty())
  {
    return preMutationInvalidParamsFailure(
      "brushes must not be empty", "provide_polygon_brushes_then_retry");
  }

  auto error = QString{};
  const auto metadata = metadataArrayFromBrushes(brushes, error);
  if (!metadata)
  {
    return preMutationInvalidParamsFailure(error, "fix_polygon_metadata_then_retry");
  }

  auto operations = QJsonArray{};
  auto normalizedPointCount = 0;
  auto polygonDiagnostics = QJsonArray{};
  auto errors = QJsonArray{};
  auto firstInvalidPolygonIndex = -1;
  const auto grid =
    params.value("grid").isDouble() ? params.value("grid").toDouble() : 0.0;
  for (auto i = 0; i < brushes.size(); ++i)
  {
    auto operation = brushes[i].toObject();
    const auto originalPointsCount = operation.value("points2d").toArray().size();
    const auto cleanedPoints =
      cleanedPoints2dArray(operation.value("points2d"), grid, error);
    if (!cleanedPoints)
    {
      if (firstInvalidPolygonIndex < 0)
      {
        firstInvalidPolygonIndex = i;
      }
      errors.push_back(QString{"brushes[%1].%2"}.arg(i).arg(error));
      polygonDiagnostics.push_back(QJsonObject{
        {"brushIndex", i},
        {"valid", false},
        {"reason", error},
      });
      continue;
    }

    auto pointsError = QString{};
    auto points = vec2PointsFromJsonArray(*cleanedPoints, pointsError);
    if (!points)
    {
      if (firstInvalidPolygonIndex < 0)
      {
        firstInvalidPolygonIndex = i;
      }
      errors.push_back(QString{"brushes[%1].%2"}.arg(i).arg(pointsError));
      polygonDiagnostics.push_back(QJsonObject{
        {"brushIndex", i},
        {"valid", false},
        {"reason", pointsError},
      });
      continue;
    }
    auto diagnostic = polygonConvexityDiagnostic(*points, i);
    if (!diagnostic.value("valid").toBool(false))
    {
      if (firstInvalidPolygonIndex < 0)
      {
        firstInvalidPolygonIndex = i;
      }
      errors.push_back(
        QString{"brushes[%1].%2"}.arg(i).arg(diagnostic.value("reason").toString()));
      polygonDiagnostics.push_back(diagnostic);
      continue;
    }

    const auto minZ = operation.value("minZ").toDouble(0.0);
    const auto maxZ = operation.value("maxZ").toDouble(64.0);
    if (!std::isfinite(minZ) || !std::isfinite(maxZ) || minZ >= maxZ)
    {
      if (firstInvalidPolygonIndex < 0)
      {
        firstInvalidPolygonIndex = i;
      }
      const auto reason = QString{"minZ must be smaller than maxZ"};
      errors.push_back(QString{"brushes[%1].%2"}.arg(i).arg(reason));
      diagnostic.insert("valid", false);
      diagnostic.insert("reason", reason);
      polygonDiagnostics.push_back(diagnostic);
      continue;
    }
    operation.insert("points2d", *cleanedPoints);
    normalizedPointCount +=
      std::max(0, static_cast<int>(originalPointsCount - cleanedPoints->size()));
    operation.remove("metadata");
    operation.insert("type", "prism");
    operations.push_back(operation);
  }

  if (!errors.isEmpty())
  {
    const auto invalidPolygonCount = errors.size();
    return McpBridgeToolResult::success(QJsonObject{
      {"valid", false},
      {"validation",
       QJsonObject{
         {"valid", false},
         {"errors", errors},
         {"failedOperationIndex", firstInvalidPolygonIndex},
         {"failedOperationType", "prism"},
         {"compiledOperationCount", 0},
         {"compiledBrushCount", 0},
         {"invalidPolygonCount", invalidPolygonCount},
       }},
      {"polygonDiagnostics", polygonDiagnostics},
      {"invalidPolygonCount", invalidPolygonCount},
      {"firstInvalidPolygonIndex", firstInvalidPolygonIndex},
      {"mutatedDocument", false},
      {"retrySafe", true},
      {"recoveryAction", "fix_polygon_points_then_retry"},
    });
  }

  const auto requestedDetail = detailFromParams(params);
  auto batchParams = QJsonObject{
    {"name",
     params.value("transactionName")
       .toString(params.value("name").toString("MCP: Create polygon platform batch"))},
    {"operations", operations},
    {"select", params.value("select").toBool(true)},
    {"detail", requestedDetail == "full" ? "full" : "ids"},
  };
  if (params.contains("grid"))
  {
    batchParams.insert("grid", params.value("grid"));
  }
  if (params.contains("material"))
  {
    batchParams.insert("material", params.value("material"));
  }

  auto result = blockoutCreateBatchForMapResult(
    map, toolName, batchParams, history, nextOperationIndex);
  if (!result.ok)
  {
    return result;
  }

  const auto validation = result.result.value("validation").toObject();
  if (!validation.value("valid").toBool(false))
  {
    result.result.insert("detail", requestedDetail);
    result.result.remove("changedObjectIds");
    return result;
  }

  const auto changedObjectIds = result.result.value("changedObjectIds").toArray();
  auto metadataCount = 0;
  const auto documentFingerprint = documentFingerprintForMap(map);
  for (auto i = 0; i < changedObjectIds.size() && i < static_cast<int>(metadata->size());
       ++i)
  {
    const auto objectId = changedObjectIds[i].toString();
    const auto& brushMetadata = (*metadata)[static_cast<size_t>(i)];
    if (objectId.isEmpty() || brushMetadata.isEmpty())
    {
      continue;
    }
    metadataStore[metadataStoreKey(documentFingerprint, objectId)] =
      McpBrushMetadataRecord{
        objectId,
        documentFingerprint,
        brushMetadata,
        false,
      };
    ++metadataCount;
  }

  result.result.insert("metadataCount", metadataCount);
  result.result.insert("normalizedPointCount", normalizedPointCount);
  if (requestedDetail == "summary")
  {
    result.result.remove("changedObjectIds");
  }
  result.result.insert("detail", requestedDetail);
  if (
    !history.empty()
    && history.back().operationId == result.result.value("operationId").toString())
  {
    history.back().setSummary(result.result);
  }
  return result;
}

McpBridgeToolResult brushMetadataSetResult(
  AppController& appController,
  const QJsonObject& params,
  std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return brushMetadataSetForMapResult(mapWindow->document().map(), params, metadataStore);
}

McpBridgeToolResult brushMetadataSetForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  auto error = QString{};
  auto objectIds = stringListFromJson(params, "objectIds", error);
  if (!objectIds)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error,
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"recoveryAction", "fix_object_ids_then_retry"},
      });
  }
  if (objectIds->empty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "objectIds must not be empty",
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"recoveryAction", "provide_brush_object_ids_then_retry"},
      });
  }

  auto metadata = metadataFromJsonValue(params.value("metadata"), "metadata", error);
  if (!metadata)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      error,
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"recoveryAction", "fix_metadata_then_retry"},
      });
  }
  if (metadata->isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      "metadata must not be empty",
      QJsonObject{
        {"mutatedDocument", false},
        {"retrySafe", true},
        {"recoveryAction", "provide_metadata_then_retry"},
      });
  }

  for (const auto& objectId : *objectIds)
  {
    auto* node = resolveNodeId(map.worldNode(), objectId);
    if (dynamic_cast<mdl::BrushNode*>(node) == nullptr)
    {
      return McpBridgeToolResult::failure(
        mcp::McpErrorCode::InvalidParams,
        QString{"Object id is not a live brush: %1"}.arg(objectId),
        QJsonObject{
          {"mutatedDocument", false},
          {"retrySafe", true},
          {"objectId", objectId},
          {"recoveryAction", "refresh_status_or_select_live_brushes"},
        });
    }
  }

  const auto documentFingerprint = documentFingerprintForMap(map);
  for (const auto& objectId : *objectIds)
  {
    metadataStore[metadataStoreKey(documentFingerprint, objectId)] =
      McpBrushMetadataRecord{
        objectId,
        documentFingerprint,
        *metadata,
        false,
      };
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"count", static_cast<int>(objectIds->size())},
    {"objectIds", stringVectorToJson(*objectIds)},
    {"metadata", *metadata},
    {"mutatedDocument", false},
  });
}

McpBridgeToolResult brushMetadataGetResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return brushMetadataGetForMapResult(mapWindow->document().map(), params, metadataStore);
}

McpBridgeToolResult brushMetadataGetForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  auto error = QString{};
  auto objectIds = stringListFromJson(params, "objectIds", error);
  if (!objectIds)
  {
    return invalidParamsFailure(error);
  }

  auto results = QJsonArray{};
  auto staleCount = 0;
  const auto documentFingerprint = documentFingerprintForMap(map);
  for (const auto& objectId : *objectIds)
  {
    auto it = metadataStore.find(metadataStoreKey(documentFingerprint, objectId));
    if (it == metadataStore.end())
    {
      it = metadataStore.find(objectId);
    }
    auto* node = resolveNodeId(map.worldNode(), objectId);
    const auto live = dynamic_cast<mdl::BrushNode*>(node) != nullptr;
    const auto explicitlyStale = it != metadataStore.end() && it->second.stale;
    const auto wrongDocument = it != metadataStore.end()
                               && !recordMatchesDocument(it->second, documentFingerprint);
    const auto stale = !live || explicitlyStale || wrongDocument;
    if (stale)
    {
      ++staleCount;
    }

    auto object = QJsonObject{
      {"objectId", objectId},
      {"live", live},
      {"stale", stale},
      {"hasMetadata",
       it != metadataStore.end() && !wrongDocument && !it->second.metadata.isEmpty()},
      {"metadata",
       it != metadataStore.end() && !wrongDocument ? it->second.metadata : QJsonObject{}},
    };
    if (stale)
    {
      object.insert(
        "staleReason",
        !live           ? "objectId does not resolve to a live brush"
        : wrongDocument ? "metadata belongs to a different document"
                        : "metadata record was marked stale");
    }
    results.push_back(object);
  }

  auto result = QJsonObject{
    {"count", results.size()},
    {"staleCount", staleCount},
    {"objects", results},
  };
  if (staleCount > 0)
  {
    result.insert(
      "diagnostic",
      "Some metadata records no longer point at live brush objects. This can happen "
      "after undo, delete, map reload, or object id reassignment.");
    result.insert(
      "suggestedAction",
      "Re-identify objects with selection_filter/selection_by_bounds or recreate "
      "metadata for the current document session.");
  }
  return McpBridgeToolResult::success(result);
}

McpBridgeToolResult selectionByMetadataResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return selectionByMetadataForMapResult(
    mapWindow->document().map(), params, metadataStore);
}

McpBridgeToolResult selectionByMetadataForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  const auto limit =
    std::clamp(optionalSize(params, "limit", 100), size_t{1}, size_t{1000});
  const auto matchingIds = matchingMetadataObjectIds(
    params, documentFingerprintForMap(map), metadataStore, limit);

  auto nodes = std::vector<mdl::Node*>{};
  auto objectIds = QJsonArray{};
  auto staleCount = 0;
  for (const auto& objectId : matchingIds)
  {
    auto* node = resolveNodeId(map.worldNode(), objectId);
    auto* brush = dynamic_cast<mdl::BrushNode*>(node);
    if (!brush)
    {
      ++staleCount;
      continue;
    }
    nodes.push_back(brush);
    objectIds.push_back(objectId);
  }

  if (params.value("select").toBool(false))
  {
    mdl::deselectAll(map);
    mdl::selectNodes(map, nodes);
  }

  auto result = QJsonObject{
    {"count", objectIds.size()},
    {"objectIds", objectIds},
    {"selected", params.value("select").toBool(false)},
    {"staleCount", staleCount},
    {"truncated", matchingIds.size() >= limit},
    {"mutatedDocument", false},
  };
  if (staleCount > 0)
  {
    result.insert(
      "diagnostic",
      "Some matching metadata records were skipped because their object ids no longer "
      "resolve to live brushes.");
    result.insert(
      "suggestedAction",
      "Use selection_filter/selection_by_bounds to re-identify the current brush set, "
      "then refresh session metadata if needed.");
  }
  return McpBridgeToolResult::success(result);
}

McpBridgeToolResult routeGeometryAnalyzeChainResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return routeGeometryAnalyzeChainForMapResult(
    mapWindow->document().map(), params, metadataStore);
}

McpBridgeToolResult routeGeometryAnalyzeChainForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  auto error = QString{};
  auto objectIds = std::vector<QString>{};
  auto warnings = QJsonArray{};
  if (params.value("objectIds").isArray())
  {
    auto parsedIds = stringListFromJson(params, "objectIds", error);
    if (!parsedIds)
    {
      return invalidParamsFailure(error);
    }
    objectIds = *parsedIds;
  }
  else
  {
    const auto routeId = params.value("routeId").toString().trimmed();
    if (routeId.isEmpty())
    {
      return invalidParamsFailure(
        "route geometry analysis requires objectIds or routeId");
    }
    objectIds = matchingMetadataObjectIds(
      QJsonObject{{"routeId", routeId}},
      documentFingerprintForMap(map),
      metadataStore,
      std::numeric_limits<size_t>::max());
    const auto orderBy =
      params.value("orderBy").toString("metadataOrder").trimmed().toLower();
    if (orderBy == "metadataorder" || orderBy == "metadata" || orderBy == "order")
    {
      sortMetadataObjectIdsByOrder(
        objectIds, documentFingerprintForMap(map), metadataStore, warnings);
    }
    else
    {
      warnings.push_back(
        "Route order was inferred from MCP object ids; pass ordered objectIds or use "
        "orderBy=metadataOrder with metadata.order for exact chain order.");
    }
  }

  if (objectIds.size() < 2)
  {
    return invalidParamsFailure("At least two brush object ids are required");
  }

  const auto playerHull =
    optionalVec3FromJson(params, "playerHull", vm::vec3d{32.0, 32.0, 72.0}, error);
  if (!playerHull)
  {
    return invalidParamsFailure(error);
  }
  const auto hullWidth = std::max(playerHull->x(), playerHull->y());

  auto brushes = liveBrushNodesFromObjectIds(map, objectIds, error);
  if (!error.isEmpty())
  {
    return invalidParamsFailure(error);
  }

  if (params.value("movementType").toString().trimmed().isEmpty())
  {
    warnings.push_back(
      "movementType was omitted; distances are geometry metrics, not a gameplay "
      "difficulty verdict.");
  }

  auto segments = QJsonArray{};
  auto maxEdgeGap = 0.0;
  auto maxEffectiveDistanceBadLanding = 0.0;
  auto maxHeightDelta = 0.0;
  for (size_t i = 0; i + 1u < brushes.size(); ++i)
  {
    const auto fromObjectId = nodePathId(*brushes[i], map.worldNode());
    const auto toObjectId = nodePathId(*brushes[i + 1u], map.worldNode());
    const auto documentFingerprint = documentFingerprintForMap(map);
    const auto fromMetadata =
      metadataForObject(fromObjectId, documentFingerprint, metadataStore);
    const auto toMetadata =
      metadataForObject(toObjectId, documentFingerprint, metadataStore);
    if (
      !fromMetadata.contains("outgoingDirection")
      && !toMetadata.contains("incomingDirection"))
    {
      warnings.push_back(QString{
        "Segment %1 has no route direction metadata; direction uses platform centers."}
                           .arg(static_cast<int>(i)));
    }

    const auto segment = segmentMetricsJson(
      *brushes[i],
      *brushes[i + 1u],
      map.worldNode(),
      fromMetadata,
      toMetadata,
      hullWidth,
      static_cast<int>(i));
    maxEdgeGap = std::max(maxEdgeGap, segment.value("edgeGap").toDouble());
    maxEffectiveDistanceBadLanding = std::max(
      maxEffectiveDistanceBadLanding,
      segment.value("effectiveDistanceBadLanding").toDouble());
    maxHeightDelta =
      std::max(maxHeightDelta, std::abs(segment.value("heightDelta").toDouble()));
    segments.push_back(segment);
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"objectIds", stringVectorToJson(objectIds)},
    {"movementType", params.value("movementType").toString()},
    {"playerHull", vecToJson(*playerHull)},
    {"segmentCount", segments.size()},
    {"segments", segments},
    {"maxEdgeGap", maxEdgeGap},
    {"maxEffectiveDistanceBadLanding", maxEffectiveDistanceBadLanding},
    {"maxAbsHeightDelta", maxHeightDelta},
    {"warnings", warnings},
    {"note",
     "Route geometry analysis reports static brush metrics only. Gameplay difficulty "
     "and pass/fail viability should be judged by the Agent with domain context and "
     "in-game testing."},
  });
}

McpBridgeToolResult kzDistanceAnalyzeChainResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  return routeGeometryAnalyzeChainResult(appController, params, metadataStore);
}

McpBridgeToolResult kzDistanceAnalyzeChainForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore)
{
  return routeGeometryAnalyzeChainForMapResult(map, params, metadataStore);
}

} // namespace tb::ui
