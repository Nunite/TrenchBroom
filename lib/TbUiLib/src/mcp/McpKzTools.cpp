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
#include "mdl/BrushNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Selection.h"
#include "mdl/Node.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"

#include "vm/bbox.h"
#include "vm/vec.h"

#include <algorithm>
#include <array>
#include <cmath>
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

QJsonObject boundsToJson(const vm::bbox3d& bounds)
{
  return QJsonObject{
    {"min", vecToJson(bounds.min)},
    {"max", vecToJson(bounds.max)},
  };
}

QJsonObject shapeJson(
  const QString& name,
  const QString& purpose,
  const QString& parameters,
  const bool polygonBatch)
{
  return QJsonObject{
    {"name", name},
    {"purpose", purpose},
    {"parameters", parameters},
    {"polygonBatch", polygonBatch},
  };
}

QJsonObject metadataForObject(
  const QString& objectId,
  const std::map<QString, McpKzBrushMetadataRecord>& metadataStore)
{
  const auto it = metadataStore.find(objectId);
  if (it == metadataStore.end())
  {
    return {};
  }
  return it->second.metadata;
}

bool metadataMatches(
  const QJsonObject& metadata, const QString& key, const QJsonObject& params)
{
  const auto expected = params.value(key).toString().trimmed();
  if (expected.isEmpty())
  {
    return true;
  }
  return metadata.value(key).toString().compare(expected, Qt::CaseInsensitive) == 0;
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

bool allMetadataFiltersMatch(const QJsonObject& metadata, const QJsonObject& params)
{
  return metadataMatches(metadata, "routeId", params)
         && metadataMatches(metadata, "intent", params)
         && metadataMatches(metadata, "difficulty", params)
         && metadataMatches(metadata, "movementType", params)
         && metadataObjectMatches(
           metadata,
           params.value("metadata").isObject() ? params.value("metadata").toObject()
                                               : QJsonObject{});
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
  const std::map<QString, McpKzBrushMetadataRecord>& metadataStore,
  const size_t limit)
{
  auto result = std::vector<QString>{};
  for (const auto& [objectId, record] : metadataStore)
  {
    if (record.stale || record.metadata.isEmpty())
    {
      continue;
    }
    if (!allMetadataFiltersMatch(record.metadata, params))
    {
      continue;
    }
    result.push_back(objectId);
    if (result.size() >= limit)
    {
      break;
    }
  }
  return result;
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

QJsonArray metadataKeysJson()
{
  return QJsonArray{
    "routeId",
    "intent",
    "difficulty",
    "movementType",
    "takeoffEdge",
    "landingWindow",
    "incomingDirection",
    "outgoingDirection",
  };
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

McpBridgeToolResult shapeLibraryListResult()
{
  return McpBridgeToolResult::success(QJsonObject{
    {"shapes",
     QJsonArray{
       shapeJson("box", "Fast blockout/test block.", "center, size", true),
       shapeJson(
         "diamond", "Directional landing/takeoff cue.", "center, width, depth", true),
       shapeJson(
         "trapezoid",
         "Bias landing window toward an outgoing edge.",
         "center, topWidth, bottomWidth, depth",
         true),
       shapeJson(
         "chamfered_rect",
         "Trim meaningless corners from box-like platforms.",
         "center, width, depth, chamfer",
         true),
       shapeJson(
         "half_hex",
         "Broad route turn with fewer false corners.",
         "center, radius",
         true),
       shapeJson(
         "arrowhead",
         "Strong direction cue for next jump.",
         "center, width, depth",
         true),
       shapeJson(
         "slanted_plank",
         "Long narrow route-guiding ledge.",
         "center, length, width, angle",
         true),
     }},
  });
}

McpBridgeToolResult brushCreatePolygonBatchResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpKzBrushMetadataRecord>& metadataStore)
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
  std::map<QString, McpKzBrushMetadataRecord>& metadataStore)
{
  const auto brushesValue = params.value("brushes");
  if (!brushesValue.isArray())
  {
    return invalidParamsFailure("brush_create_polygon_batch requires brushes array");
  }

  const auto brushes = brushesValue.toArray();
  if (brushes.isEmpty())
  {
    return invalidParamsFailure("brushes must not be empty");
  }

  auto error = QString{};
  const auto metadata = metadataArrayFromBrushes(brushes, error);
  if (!metadata)
  {
    return invalidParamsFailure(error);
  }

  auto operations = QJsonArray{};
  for (auto i = 0; i < brushes.size(); ++i)
  {
    auto operation = brushes[i].toObject();
    operation.remove("metadata");
    operation.insert("type", "prism");
    operations.push_back(operation);
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
  for (auto i = 0; i < changedObjectIds.size() && i < static_cast<int>(metadata->size());
       ++i)
  {
    const auto objectId = changedObjectIds[i].toString();
    const auto& brushMetadata = (*metadata)[static_cast<size_t>(i)];
    if (objectId.isEmpty() || brushMetadata.isEmpty())
    {
      continue;
    }
    metadataStore[objectId] = McpKzBrushMetadataRecord{
      objectId,
      brushMetadata,
      false,
    };
    ++metadataCount;
  }

  result.result.insert("metadataCount", metadataCount);
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
  std::map<QString, McpKzBrushMetadataRecord>& metadataStore)
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
  std::map<QString, McpKzBrushMetadataRecord>& metadataStore)
{
  auto error = QString{};
  auto objectIds = stringListFromJson(params, "objectIds", error);
  if (!objectIds)
  {
    return invalidParamsFailure(error);
  }
  if (objectIds->empty())
  {
    return invalidParamsFailure("objectIds must not be empty");
  }

  auto metadata = metadataFromJsonValue(params.value("metadata"), "metadata", error);
  if (!metadata)
  {
    return invalidParamsFailure(error);
  }
  if (metadata->isEmpty())
  {
    return invalidParamsFailure("metadata must not be empty");
  }

  for (const auto& objectId : *objectIds)
  {
    auto* node = resolveNodeId(map.worldNode(), objectId);
    if (dynamic_cast<mdl::BrushNode*>(node) == nullptr)
    {
      return invalidParamsFailure(
        QString{"Object id is not a live brush: %1"}.arg(objectId));
    }
  }

  for (const auto& objectId : *objectIds)
  {
    metadataStore[objectId] = McpKzBrushMetadataRecord{
      objectId,
      *metadata,
      false,
    };
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"count", static_cast<int>(objectIds->size())},
    {"objectIds", stringVectorToJson(*objectIds)},
    {"metadata", *metadata},
  });
}

McpBridgeToolResult brushMetadataGetResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpKzBrushMetadataRecord>& metadataStore)
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
  const std::map<QString, McpKzBrushMetadataRecord>& metadataStore)
{
  auto error = QString{};
  auto objectIds = stringListFromJson(params, "objectIds", error);
  if (!objectIds)
  {
    return invalidParamsFailure(error);
  }

  auto results = QJsonArray{};
  auto staleCount = 0;
  for (const auto& objectId : *objectIds)
  {
    const auto it = metadataStore.find(objectId);
    auto* node = resolveNodeId(map.worldNode(), objectId);
    const auto live = dynamic_cast<mdl::BrushNode*>(node) != nullptr;
    const auto stale = !live || (it != metadataStore.end() && it->second.stale);
    if (stale)
    {
      ++staleCount;
    }

    results.push_back(QJsonObject{
      {"objectId", objectId},
      {"live", live},
      {"stale", stale},
      {"hasMetadata", it != metadataStore.end() && !it->second.metadata.isEmpty()},
      {"metadata", it != metadataStore.end() ? it->second.metadata : QJsonObject{}},
    });
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"count", results.size()},
    {"staleCount", staleCount},
    {"objects", results},
  });
}

McpBridgeToolResult selectionByMetadataResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpKzBrushMetadataRecord>& metadataStore)
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
  const std::map<QString, McpKzBrushMetadataRecord>& metadataStore)
{
  const auto limit =
    std::clamp(optionalSize(params, "limit", 100), size_t{1}, size_t{1000});
  const auto matchingIds = matchingMetadataObjectIds(params, metadataStore, limit);

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

  return McpBridgeToolResult::success(QJsonObject{
    {"count", objectIds.size()},
    {"objectIds", objectIds},
    {"selected", params.value("select").toBool(false)},
    {"staleCount", staleCount},
    {"truncated", matchingIds.size() >= limit},
  });
}

McpBridgeToolResult kzDistanceAnalyzeChainResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpKzBrushMetadataRecord>& metadataStore)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return kzDistanceAnalyzeChainForMapResult(
    mapWindow->document().map(), params, metadataStore);
}

McpBridgeToolResult kzDistanceAnalyzeChainForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::map<QString, McpKzBrushMetadataRecord>& metadataStore)
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
        "kz_distance_analyze_chain requires objectIds or routeId");
    }
    objectIds = matchingMetadataObjectIds(
      QJsonObject{{"routeId", routeId}},
      metadataStore,
      std::numeric_limits<size_t>::max());
    warnings.push_back(
      "Route order was inferred from MCP object ids; pass ordered objectIds for exact "
      "chain order.");
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
      "movementType was omitted; distances are geometry metrics, not a KZ difficulty "
      "verdict.");
  }

  auto segments = QJsonArray{};
  auto maxEdgeGap = 0.0;
  auto maxEffectiveDistanceBadLanding = 0.0;
  auto maxHeightDelta = 0.0;
  for (size_t i = 0; i + 1u < brushes.size(); ++i)
  {
    const auto fromObjectId = nodePathId(*brushes[i], map.worldNode());
    const auto toObjectId = nodePathId(*brushes[i + 1u], map.worldNode());
    const auto fromMetadata = metadataForObject(fromObjectId, metadataStore);
    const auto toMetadata = metadataForObject(toObjectId, metadataStore);
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
     "KZ distance analysis is a mapper heuristic and not an in-game pass/fail "
     "guarantee."},
  });
}

} // namespace tb::ui
