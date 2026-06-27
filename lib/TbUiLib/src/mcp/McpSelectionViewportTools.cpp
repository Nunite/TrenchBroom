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
#include <QCoreApplication>
#include <QFileInfo>
#include <QIODevice>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QPixmap>
#include <QStringList>
#include <QUuid>

#include "McpBridgeServerTools.h"
#include "McpSelectionQuery.h"
#include "gl/Camera.h"
#include "mcp/McpError.h"
#include "mdl/Brush.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushFaceHandle.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/EntityProperties.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Selection.h"
#include "mdl/Node.h"
#include "mdl/PatchNode.h"
#include "mdl/Selection.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapView2D.h"
#include "ui/MapView3D.h"
#include "ui/MapViewBase.h"
#include "ui/MapViewLayout.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"
#include "ui/SystemPaths.h"
#include "ui/mcp/McpObjectRegistry.h"

#include "vm/bbox.h"

#include <algorithm>
#include <array>
#include <chrono>
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

QJsonObject cameraToJson(const gl::Camera& camera)
{
  return QJsonObject{
    {"position", vecToJson(vm::vec3d{camera.position()})},
    {"direction", vecToJson(vm::vec3d{camera.direction()})},
    {"up", vecToJson(vm::vec3d{camera.up()})},
    {"right", vecToJson(vm::vec3d{camera.right()})},
    {"zoom", camera.zoom()},
  };
}

QString pathToQString(const std::filesystem::path& path)
{
  return path.empty() ? QString{} : pathAsQString(path);
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

std::optional<vm::bbox3d> boundsFromParamObject(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto value = params.value(key);
  if (!value.isObject())
  {
    error = QString{"%1 must be an object with min/max arrays"}.arg(key);
    return std::nullopt;
  }
  return boundsFromJson(value.toObject(), error);
}

QJsonArray defaultSceneReviewChecklist()
{
  return QJsonArray{
    "silhouette reads as the requested scene type",
    "major spaces and route connections are coherent",
    "scale is plausible for the target game/editor context",
    "brushes do not visibly float, overlap incorrectly, or leave unintended gaps",
    "whitebox material differences are enough to distinguish functional parts",
    "MCP tool friction or missing primitives are recorded for follow-up",
  };
}

QJsonArray stringArrayFromValueOrDefault(
  const QJsonValue& value, const QJsonArray& defaultValue)
{
  if (!value.isArray())
  {
    return defaultValue;
  }

  auto result = QJsonArray{};
  for (const auto& entry : value.toArray())
  {
    if (entry.isString() && !entry.toString().trimmed().isEmpty())
    {
      result.push_back(entry.toString().trimmed());
    }
  }
  return result.isEmpty() ? defaultValue : result;
}

void applyFramingPreset(QJsonObject& cameraParams, const QString& framing)
{
  const auto normalized = framing.trimmed().toLower();
  if (normalized == "overview_orbit")
  {
    cameraParams.insert("azimuth", -45.0);
    cameraParams.insert("elevation", 38.0);
    cameraParams.insert("distanceScale", 1.55);
  }
  else if (normalized == "top_fit")
  {
    cameraParams.insert("azimuth", -90.0);
    cameraParams.insert("elevation", 82.0);
    cameraParams.insert("distanceScale", 1.35);
  }
  else if (normalized == "side_profile")
  {
    cameraParams.insert("azimuth", -90.0);
    cameraParams.insert("elevation", 8.0);
    cameraParams.insert("distanceScale", 1.65);
  }
  else if (normalized == "route_follow")
  {
    cameraParams.insert("azimuth", -35.0);
    cameraParams.insert("elevation", 18.0);
    cameraParams.insert("distanceScale", 1.45);
  }
}

QString normalizedReviewViewName(const QString& view)
{
  const auto normalized = view.trimmed().toLower();
  if (normalized == "window")
  {
    return "current";
  }
  if (normalized == "current" || normalized == "3d" || normalized == "2d")
  {
    return normalized;
  }
  if (normalized == "overview_3d" || normalized == "detail_3d")
  {
    return "3d";
  }
  if (normalized == "top_2d_fit" || normalized == "side_2d_fit")
  {
    return normalized;
  }
  return {};
}

QString normalizedIsolateMode(const QJsonObject& params)
{
  const auto mode =
    params.value("isolateMode").toString("highlight_only").trimmed().toLower();
  if (mode == "hide_others" || mode == "fade_others" || mode == "highlight_only")
  {
    return mode;
  }
  return "highlight_only";
}

QString mapViewLayoutName(const MapViewLayout layout)
{
  switch (layout)
  {
  case MapViewLayout::OnePane:
    return "onePane";
  case MapViewLayout::TwoPanes:
    return "twoPanes";
  case MapViewLayout::ThreePanes:
    return "threePanes";
  case MapViewLayout::FourPanes:
    return "fourPanes";
  }
  return "unknown";
}

std::optional<MapViewLayout> parseMapViewLayout(const QString& value)
{
  const auto normalized = value.trimmed().toLower();
  if (normalized == "1" || normalized == "one" || normalized == "onepane")
  {
    return MapViewLayout::OnePane;
  }
  if (normalized == "2" || normalized == "two" || normalized == "twopanes")
  {
    return MapViewLayout::TwoPanes;
  }
  if (normalized == "3" || normalized == "three" || normalized == "threepanes")
  {
    return MapViewLayout::ThreePanes;
  }
  if (normalized == "4" || normalized == "four" || normalized == "fourpanes")
  {
    return MapViewLayout::FourPanes;
  }
  return std::nullopt;
}

QJsonObject viewportLayoutJson(MapWindow& mapWindow)
{
  const auto hasVisible2D = std::ranges::any_of(
    mapWindow.findChildren<MapView2D*>(),
    [](const auto* view) { return view != nullptr && view->isVisible(); });
  const auto hasVisible3D = std::ranges::any_of(
    mapWindow.findChildren<MapView3D*>(),
    [](const auto* view) { return view != nullptr && view->isVisible(); });

  return QJsonObject{
    {"layout", mapViewLayoutName(mapWindow.currentMapViewLayout())},
    {"hasVisible2D", hasVisible2D},
    {"hasVisible3D", hasVisible3D},
  };
}

double optionalFiniteNumber(
  const QJsonObject& params, const QString& key, const double defaultValue)
{
  const auto value = params.value(key);
  if (!value.isDouble() || !std::isfinite(value.toDouble()))
  {
    return defaultValue;
  }
  return value.toDouble();
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

McpBridgeToolResult capturePixmapResult(
  const QPixmap& pixmap, const QJsonObject& params, const QString& scope)
{
  if (pixmap.isNull())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError,
      QString{"Could not capture %1 viewport"}.arg(scope));
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
      {"scope", scope},
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
    {"scope", scope},
  });
}

QJsonObject captureQualityJson(
  const QJsonObject& capture,
  const QString& view,
  const int min2dHeight,
  const bool requireCameraFrame)
{
  auto warnings = QJsonArray{};
  const auto path = capture.value("path").toString();
  const auto width = capture.value("width").toInt(0);
  const auto height = capture.value("height").toInt(0);
  auto fileSize = int64_t{0};
  auto exists = false;

  if (!path.isEmpty())
  {
    const auto info = QFileInfo{path};
    exists = info.exists() && info.isFile();
    fileSize = exists ? info.size() : int64_t{0};
  }
  else if (capture.contains("base64"))
  {
    exists = true;
    fileSize = capture.value("base64").toString().size();
  }

  if (!exists)
  {
    warnings.push_back("missingCaptureFile");
  }
  if (width <= 0 || height <= 0)
  {
    warnings.push_back("invalidCaptureDimensions");
  }
  if (fileSize <= 0)
  {
    warnings.push_back("emptyCaptureFile");
  }
  if (view.contains("2d") && height < min2dHeight)
  {
    warnings.push_back("tooSmall2dCapture");
  }
  if (requireCameraFrame)
  {
    warnings.push_back("missingCameraFramingMetadata");
  }

  if (!path.isEmpty() && exists)
  {
    const auto image = QImage{path};
    if (image.isNull())
    {
      warnings.push_back("unreadableCaptureImage");
    }
    else
    {
      auto minLuma = 255;
      auto maxLuma = 0;
      auto sampled = 0;
      const auto stepX = std::max(1, image.width() / 24);
      const auto stepY = std::max(1, image.height() / 24);
      for (auto y = 0; y < image.height(); y += stepY)
      {
        for (auto x = 0; x < image.width(); x += stepX)
        {
          const auto color = image.pixelColor(x, y);
          const auto luma = qGray(color.rgb());
          minLuma = std::min(minLuma, luma);
          maxLuma = std::max(maxLuma, luma);
          ++sampled;
        }
      }
      if (sampled > 0 && maxLuma - minLuma < 4)
      {
        warnings.push_back("nearBlankCapture");
      }
      if (sampled > 0 && maxLuma < 8)
      {
        warnings.push_back("nearBlackCapture");
      }
      if (sampled > 0 && minLuma > 247)
      {
        warnings.push_back("nearWhiteCapture");
      }
    }
  }

  return QJsonObject{
    {"view", view},
    {"valid", warnings.isEmpty()},
    {"warnings", warnings},
    {"path", path},
    {"exists", exists},
    {"fileSize", static_cast<double>(fileSize)},
    {"width", width},
    {"height", height},
  };
}

bool captureQualityValid(const QJsonObject& quality)
{
  return quality.value("valid").toBool(false);
}

void appendWarnings(QJsonArray& target, const QJsonArray& warnings)
{
  for (const auto& warning : warnings)
  {
    target.push_back(warning);
  }
}

template <typename View>
View* findCaptureView(MapWindow& mapWindow)
{
  if (auto* currentView = dynamic_cast<View*>(mapWindow.currentMapViewBase()))
  {
    if (currentView->isVisible())
    {
      return currentView;
    }
  }

  for (auto* view : mapWindow.findChildren<View*>())
  {
    if (view && view->isVisible())
    {
      return view;
    }
  }

  if (auto* currentView = dynamic_cast<View*>(mapWindow.currentMapViewBase()))
  {
    return currentView;
  }

  return mapWindow.findChild<View*>();
}

std::vector<mdl::Node*> nodesFromObjectIds(
  mdl::Map& map, const QJsonArray& objectIds, QString& error)
{
  auto& worldNode = map.worldNode();
  auto nodes = std::vector<mdl::Node*>{};

  for (const auto& objectIdValue : objectIds)
  {
    if (!objectIdValue.isString())
    {
      error = "objectIds must contain only strings";
      return {};
    }

    const auto objectId = objectIdValue.toString();
    auto* node = resolveNodeId(worldNode, objectId);
    if (!node)
    {
      error = QString{"Unknown MCP object id: %1"}.arg(objectId);
      return {};
    }
    nodes.push_back(node);
  }

  return nodes;
}

vm::bbox3d boundsForNodes(const std::vector<mdl::Node*>& nodes)
{
  auto builder = vm::bbox3d::builder{};
  for (const auto* node : nodes)
  {
    if (node)
    {
      builder.add(node->logicalBounds());
    }
  }
  return builder.bounds();
}

std::optional<vm::bbox3d> cameraFrameBoundsFromParams(
  mdl::Map& map, const QJsonObject& params, QString& error)
{
  if (const auto boundsValue = params.value("bounds"); boundsValue.isObject())
  {
    return boundsFromParamObject(params, "bounds", error);
  }
  if (params.contains("min") || params.contains("max"))
  {
    return boundsFromJson(params, error);
  }
  if (const auto objectIds = params.value("objectIds"); objectIds.isArray())
  {
    const auto nodes = nodesFromObjectIds(map, objectIds.toArray(), error);
    if (!error.isEmpty())
    {
      return std::nullopt;
    }
    if (nodes.empty())
    {
      error = "objectIds must contain at least one object";
      return std::nullopt;
    }
    return boundsForNodes(nodes);
  }

  error = "viewport_camera_frame_bounds requires objectIds, bounds, or min/max";
  return std::nullopt;
}

QJsonObject frame3dCameraOnBounds(
  MapView3D& view, const vm::bbox3d& bounds, const QJsonObject& params)
{
  const auto azimuthDegrees = optionalFiniteNumber(params, "azimuth", -45.0);
  const auto elevationDegrees =
    std::clamp(optionalFiniteNumber(params, "elevation", 32.0), -85.0, 85.0);
  const auto distanceScale =
    std::max(0.25, optionalFiniteNumber(params, "distanceScale", 1.35));
  const auto minDistance =
    std::max(1.0, optionalFiniteNumber(params, "minDistance", 256.0));
  const auto targetOffset = [&]() {
    auto error = QString{};
    if (const auto offset = mcpVec3FromJson(params, "targetOffset", error))
    {
      return *offset;
    }
    return vm::vec3d{};
  }();

  const auto target = bounds.center() + targetOffset;
  const auto size = bounds.size();
  const auto diagonal = std::max(1.0, vm::length(size));
  const auto distance = std::max(minDistance, diagonal * distanceScale);
  const auto azimuth = azimuthDegrees * vm::constants<double>::pi() / 180.0;
  const auto elevation = elevationDegrees * vm::constants<double>::pi() / 180.0;
  const auto horizontal = std::cos(elevation);
  const auto offsetDirection = vm::normalize(vm::vec3d{
    std::cos(azimuth) * horizontal,
    std::sin(azimuth) * horizontal,
    std::sin(elevation),
  });
  const auto position = target + offsetDirection * distance;

  auto& camera = static_cast<MapViewBase&>(view).camera();
  camera.moveTo(vm::vec3f{position});
  camera.lookAt(vm::vec3f{target}, vm::vec3f{0, 0, 1});
  camera.setZoom(1.0f);
  view.update();
  QCoreApplication::processEvents();

  return QJsonObject{
    {"cameraControlled", true},
    {"mode", "orbitBounds"},
    {"bounds", boundsToJson(bounds)},
    {"target", vecToJson(target)},
    {"position", vecToJson(position)},
    {"azimuth", azimuthDegrees},
    {"elevation", elevationDegrees},
    {"distance", distance},
    {"distanceScale", distanceScale},
    {"minDistance", minDistance},
    {"camera", cameraToJson(camera)},
  };
}

McpBridgeToolResult set3dCameraLookAtResult(MapView3D& view, const QJsonObject& params)
{
  auto error = QString{};
  const auto position = mcpVec3FromJson(params, "position", error);
  if (!position)
  {
    return invalidParamsFailure(error);
  }
  const auto target = mcpVec3FromJson(params, "target", error);
  if (!target)
  {
    return invalidParamsFailure(error);
  }

  auto up = vm::vec3d{0, 0, 1};
  if (params.contains("up"))
  {
    const auto parsedUp = mcpVec3FromJson(params, "up", error);
    if (!parsedUp)
    {
      return invalidParamsFailure(error);
    }
    up = *parsedUp;
  }

  const auto direction = *target - *position;
  if (
    vm::is_zero(vm::squared_length(direction), vm::Cd::almost_zero())
    || vm::is_zero(vm::squared_length(up), vm::Cd::almost_zero()))
  {
    return invalidParamsFailure(
      "position and target must differ, and up must be non-zero");
  }

  const auto zoom = std::max(0.01, optionalFiniteNumber(params, "zoom", 1.0));
  auto& camera = static_cast<MapViewBase&>(view).camera();
  camera.moveTo(vm::vec3f{*position});
  camera.lookAt(vm::vec3f{*target}, vm::vec3f{up});
  camera.setZoom(static_cast<float>(zoom));
  view.update();
  QCoreApplication::processEvents();

  return McpBridgeToolResult::success(QJsonObject{
    {"cameraControlled", true},
    {"mode", "lookAt"},
    {"position", vecToJson(*position)},
    {"target", vecToJson(*target)},
    {"up", vecToJson(up)},
    {"zoom", zoom},
    {"camera", cameraToJson(camera)},
  });
}

template <typename View>
McpBridgeToolResult viewportCaptureTypedResult(
  AppController& appController, const QJsonObject& params, const QString& scope)
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

  auto* view = findCaptureView<View>(*mapWindow);
  if (!view || !view->isVisible())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, QString{"No visible %1 viewport"}.arg(scope));
  }

  return capturePixmapResult(view->grab(), params, scope);
}

MapView2D* findReview2DView(MapWindow& mapWindow, const QString& reviewView)
{
  const auto desiredObjectName =
    reviewView == "side_2d_fit" ? QString{"XZ View"} : QString{"XY View"};
  for (auto* view : mapWindow.findChildren<MapView2D*>())
  {
    if (view != nullptr && view->isVisible() && view->objectName() == desiredObjectName)
    {
      return view;
    }
  }
  return findCaptureView<MapView2D>(mapWindow);
}

MapView2D* ensureReview2DPlane(MapWindow& mapWindow, const QString& reviewView)
{
  const auto desiredObjectName =
    reviewView == "side_2d_fit" ? QString{"XZ View"} : QString{"XY View"};
  for (auto attempt = 0; attempt < 3; ++attempt)
  {
    auto* view = findReview2DView(mapWindow, reviewView);
    if (view == nullptr || view->objectName() == desiredObjectName)
    {
      return view;
    }
    view->cycleMapView();
    QCoreApplication::processEvents();
  }
  return findReview2DView(mapWindow, reviewView);
}

QString viewPlaneName(const MapView2D* view)
{
  if (view == nullptr)
  {
    return {};
  }
  if (view->objectName() == "XY View")
  {
    return "xy";
  }
  if (view->objectName() == "XZ View")
  {
    return "xz";
  }
  if (view->objectName() == "YZ View")
  {
    return "yz";
  }
  return view->objectName();
}

McpBridgeToolResult viewportCaptureReview2DResult(
  AppController& appController, const QJsonObject& params, const QString& reviewView)
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

  auto* view = ensureReview2DPlane(*mapWindow, reviewView);
  if (!view || !view->isVisible())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError,
      QString{"No visible %1 viewport"}.arg(reviewView));
  }

  auto result = capturePixmapResult(view->grab(), params, "2d");
  if (result.ok)
  {
    result.result.insert("viewPlane", viewPlaneName(view));
    result.result.insert("viewObjectName", view->objectName());
  }
  return result;
}

} // namespace

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

  return selectionJsonForMap(mapWindow->document().map());
}

QJsonObject selectionJsonForMap(const mdl::Map& map)
{
  const auto& worldNode = map.worldNode();
  const auto& selection = map.selection();

  auto nodes = QJsonArray{};
  auto selectedBrushTotalFaceCount = 0;
  for (const auto* node : selection.nodes)
  {
    nodes.push_back(mcpNodeSummaryJson(*node, worldNode));
    if (const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(node))
    {
      selectedBrushTotalFaceCount += static_cast<int>(brushNode->brush().faceCount());
    }
  }

  auto faceOwnerBrushIds = QJsonArray{};
  auto faceOwnerBrushes = QJsonArray{};
  auto faceCountsByBrush = std::map<const mdl::BrushNode*, int>{};
  for (const auto& handle : selection.brushFaces)
  {
    ++faceCountsByBrush[handle.node()];
  }
  for (const auto& [brushNode, faceCount] : faceCountsByBrush)
  {
    if (brushNode == nullptr)
    {
      continue;
    }
    const auto objectId = nodePathId(*brushNode, worldNode);
    faceOwnerBrushIds.push_back(objectId);
    faceOwnerBrushes.push_back(QJsonObject{
      {"id", objectId},
      {"type", "brush"},
      {"selectedFaceCount", faceCount},
      {"faceCount", static_cast<int>(brushNode->brush().faceCount())},
      {"logicalBounds", boundsToJson(brushNode->logicalBounds())},
    });
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
    {"selectedBrushFaceCount", selectedBrushTotalFaceCount},
    {"faceOwnerBrushCount", faceOwnerBrushIds.size()},
    {"faceOwnerBrushIds", faceOwnerBrushIds},
    {"faceOwnerBrushes", faceOwnerBrushes},
    {"selectedBrushTotalFaceCount", selectedBrushTotalFaceCount},
  };
}

QJsonObject selectionSummaryJson(AppController& appController)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return {};
  }

  const auto& selection = mapWindow->document().map().selection();
  auto selectedBrushTotalFaceCount = 0;
  for (const auto* brushNode : selection.brushes)
  {
    selectedBrushTotalFaceCount += static_cast<int>(brushNode->brush().faceCount());
  }

  auto faceOwnerBrushIds = QJsonArray{};
  auto faceCountsByBrush = std::map<const mdl::BrushNode*, int>{};
  const auto& worldNode = mapWindow->document().map().worldNode();
  for (const auto& handle : selection.brushFaces)
  {
    ++faceCountsByBrush[handle.node()];
  }
  for (const auto& entry : faceCountsByBrush)
  {
    const auto* brushNode = entry.first;
    if (brushNode == nullptr)
    {
      continue;
    }
    faceOwnerBrushIds.push_back(nodePathId(*brushNode, worldNode));
  }

  return QJsonObject{
    {"hasSelection", selection.hasAny()},
    {"nodeCount", static_cast<int>(selection.nodes.size())},
    {"groupCount", static_cast<int>(selection.groups.size())},
    {"entityCount", static_cast<int>(selection.entities.size())},
    {"brushCount", static_cast<int>(selection.brushes.size())},
    {"patchCount", static_cast<int>(selection.patches.size())},
    {"brushFaceCount", static_cast<int>(selection.brushFaces.size())},
    {"selectedBrushFaceCount", selectedBrushTotalFaceCount},
    {"faceOwnerBrushCount", faceOwnerBrushIds.size()},
    {"faceOwnerBrushIds", faceOwnerBrushIds},
    {"selectedBrushTotalFaceCount", selectedBrushTotalFaceCount},
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

McpBridgeToolResult selectionFilterResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return selectionFilterForMapResult(mapWindow->document().map(), params);
}

McpBridgeToolResult selectionFilterForMapResult(mdl::Map& map, const QJsonObject& params)
{
  auto& worldNode = map.worldNode();
  auto error = QString{};
  auto options = McpSelectionQueryOptions{};
  options.excludeWorld = mcpOptionalBool(params, "excludeWorld", true);
  options.selectableOnly = mcpOptionalBool(params, "selectableOnly", false);
  options.leafOnly = mcpOptionalBool(params, "leafOnly", false);
  options.exactTypeOnly = mcpOptionalBool(params, "exactTypeOnly", true);
  options.removeDescendantMatches =
    mcpOptionalBool(params, "removeDescendantMatches", false);
  auto matches = mcpFilteredNodes(map, params, options, error);
  if (!error.isEmpty())
  {
    return invalidParamsFailure(error);
  }

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
  auto objectIds = QJsonArray{};
  const auto detail = params.value("detail").toString("summary").toLower();
  for (const auto* node : matches)
  {
    const auto objectId = nodePathId(*node, worldNode);
    objectIds.push_back(objectId);
    if (detail == "full")
    {
      results.push_back(mcpNodeSummaryJson(*node, worldNode));
    }
  }

  auto result = QJsonObject{
    {"objectIds", objectIds},
    {"count", objectIds.size()},
    {"detail", detail == "full" ? "full" : "summary"},
    {"filters",
     QJsonObject{
       {"excludeWorld", options.excludeWorld},
       {"selectableOnly", options.selectableOnly},
       {"leafOnly", options.leafOnly},
       {"exactTypeOnly", options.exactTypeOnly},
       {"removeDescendantMatches", options.removeDescendantMatches},
     }},
  };
  if (detail == "full")
  {
    result.insert("results", results);
  }
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult selectionByBoundsResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return selectionByBoundsForMapResult(mapWindow->document().map(), params);
}

McpBridgeToolResult selectionByBoundsForMapResult(
  mdl::Map& map, const QJsonObject& params)
{
  auto paramsWithSelect = params;
  paramsWithSelect.insert("select", true);
  paramsWithSelect.insert("boundsMode", params.value("mode").toString("intersects"));
  if (!paramsWithSelect.contains("excludeWorld"))
  {
    paramsWithSelect.insert("excludeWorld", true);
  }
  if (!paramsWithSelect.contains("selectableOnly"))
  {
    paramsWithSelect.insert("selectableOnly", true);
  }
  if (!paramsWithSelect.contains("leafOnly"))
  {
    paramsWithSelect.insert("leafOnly", true);
  }
  return selectionFilterForMapResult(map, paramsWithSelect);
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

  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }
  auto* mapView = mapWindow->currentMapViewBase();
  if (!mapView)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden, "No active map view");
  }

  const auto selection = selectionSummaryJson(appController);
  const auto selectedNodeCount = selection.value("nodeCount").toInt();
  const auto canFocus = selectedNodeCount > 0;
  if (canFocus)
  {
    mapWindow->focusCameraOnSelection(false);
    QCoreApplication::processEvents();
    mapWindow->refreshMapViews();
    QCoreApplication::processEvents();
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"focused", canFocus},
    {"cameraControlled", canFocus},
    {"focusedObjectCount", selectedNodeCount},
    {"selection", selection},
  });
}

McpBridgeToolResult viewportClearMarksResult(
  AppController& appController, const QJsonObject& params, QJsonObject& overlayState)
{
  overlayState = QJsonObject{};

  if (mcpOptionalBool(params, "clearSelection", false))
  {
    auto* activeMapWindow = appController.mapWindowManager().topMapWindow();
    if (!activeMapWindow)
    {
      return noActiveDocumentFailure();
    }
    mdl::deselectAll(activeMapWindow->document().map());
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"overlay", overlayState},
    {"active", false},
    {"selectionCleared", mcpOptionalBool(params, "clearSelection", false)},
  });
}

McpBridgeToolResult viewportLayoutGetResult(AppController& appController)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return McpBridgeToolResult::success(viewportLayoutJson(*mapWindow));
}

McpBridgeToolResult viewportLayoutSetResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  const auto layoutText = params.value("layout").toString();
  const auto layout = parseMapViewLayout(layoutText);
  if (!layout)
  {
    return invalidParamsFailure(
      "layout must be onePane, twoPanes, threePanes, fourPanes, or 1/2/3/4");
  }

  const auto before = viewportLayoutJson(*mapWindow);
  if (mapWindow->currentMapViewLayout() != *layout)
  {
    mapWindow->switchMapViewLayout(*layout);
    QCoreApplication::processEvents();
  }

  auto after = viewportLayoutJson(*mapWindow);
  after.insert("previousLayout", before.value("layout"));
  after.insert("changed", before.value("layout") != after.value("layout"));
  return McpBridgeToolResult::success(std::move(after));
}

McpBridgeToolResult viewportCameraFrameBoundsResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto* view = findCaptureView<MapView3D>(*mapWindow);
  if (!view)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "No 3D viewport is available");
  }

  auto error = QString{};
  const auto bounds =
    cameraFrameBoundsFromParams(mapWindow->document().map(), params, error);
  if (!bounds)
  {
    return invalidParamsFailure(error);
  }

  return McpBridgeToolResult::success(frame3dCameraOnBounds(*view, *bounds, params));
}

McpBridgeToolResult viewportCameraSetResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto* view = findCaptureView<MapView3D>(*mapWindow);
  if (!view)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "No 3D viewport is available");
  }

  return set3dCameraLookAtResult(*view, params);
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

  return capturePixmapResult(mapWindow->grab(), params, "window");
}

McpBridgeToolResult viewportCapture3DResult(
  AppController& appController, const QJsonObject& params)
{
  return viewportCaptureTypedResult<MapView3D>(appController, params, "3d");
}

McpBridgeToolResult viewportCapture2DResult(
  AppController& appController, const QJsonObject& params)
{
  return viewportCaptureTypedResult<MapView2D>(appController, params, "2d");
}

McpBridgeToolResult viewportCaptureSceneReviewResult(
  AppController& appController,
  const QJsonObject& params,
  QJsonObject& overlayState,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry)
{
  const auto requestedLayoutValue = params.value("layout");
  auto* initialMapWindow = appController.mapWindowManager().topMapWindow();
  const auto originalLayout = initialMapWindow != nullptr
                                ? initialMapWindow->currentMapViewLayout()
                                : MapViewLayout::OnePane;
  if (params.contains("layout"))
  {
    const auto layoutResult = viewportLayoutSetResult(
      appController, QJsonObject{{"layout", params.value("layout")}});
    if (!layoutResult.ok)
    {
      return layoutResult;
    }
  }

  auto objectIds = QJsonArray{};
  if (const auto objectIdsValue = params.value("objectIds"); objectIdsValue.isArray())
  {
    for (const auto& objectIdValue : objectIdsValue.toArray())
    {
      if (objectIdValue.isString())
      {
        objectIds.push_back(objectIdValue.toString());
      }
    }
  }

  auto* reviewMapWindow = appController.mapWindowManager().topMapWindow();
  auto* map = reviewMapWindow != nullptr ? &reviewMapWindow->document().map() : nullptr;
  auto targetWarnings = QJsonArray{};
  const auto appendResolvableObjectId = [&](const QString& objectId) {
    if (objectId.isEmpty())
    {
      return;
    }
    if (objectRegistry != nullptr && map != nullptr)
    {
      const auto resolved = objectRegistry->resolveExternalId(*map, objectId);
      if (resolved.ok && !resolved.legacyPathId.isEmpty())
      {
        objectIds.push_back(resolved.legacyPathId);
      }
      else
      {
        targetWarnings.push_back(
          QString{"Could not resolve MCP object id '%1' for review."}.arg(objectId));
      }
      return;
    }
    objectIds.push_back(objectId);
  };

  if (objectRegistry != nullptr && map != nullptr)
  {
    auto resolvedObjectIds = QJsonArray{};
    for (const auto& objectIdValue : objectIds)
    {
      if (!objectIdValue.isString())
      {
        continue;
      }
      const auto objectId = objectIdValue.toString();
      const auto resolved = objectRegistry->resolveExternalId(*map, objectId);
      if (resolved.ok && !resolved.legacyPathId.isEmpty())
      {
        resolvedObjectIds.push_back(resolved.legacyPathId);
      }
      else
      {
        targetWarnings.push_back(
          QString{"Could not resolve MCP object id '%1' for review."}.arg(objectId));
      }
    }
    objectIds = resolvedObjectIds;
  }

  if (const auto operationIdsValue = params.value("operationIds");
      operationIdsValue.isArray())
  {
    for (const auto& operationIdValue : operationIdsValue.toArray())
    {
      const auto operationId = operationIdValue.toString();
      const auto it = std::ranges::find_if(history, [&](const auto& operation) {
        return operation.operationId == operationId;
      });
      if (it != history.end())
      {
        for (const auto& objectId : it->changedObjectIds)
        {
          appendResolvableObjectId(objectId);
        }
      }
      else if (!operationId.isEmpty())
      {
        targetWarnings.push_back(
          QString{"Unknown MCP operation id '%1' for scene review."}.arg(operationId));
      }
    }
  }

  auto dedupedObjectIds = QJsonArray{};
  auto seenObjectIds = std::set<QString>{};
  for (const auto& objectIdValue : objectIds)
  {
    if (!objectIdValue.isString())
    {
      continue;
    }
    const auto objectId = objectIdValue.toString();
    if (seenObjectIds.insert(objectId).second)
    {
      dedupedObjectIds.push_back(objectId);
    }
  }
  objectIds = dedupedObjectIds;

  auto targetBounds = QJsonObject{};
  if (map != nullptr && !objectIds.isEmpty())
  {
    auto error = QString{};
    const auto nodes = nodesFromObjectIds(*map, objectIds, error);
    if (error.isEmpty() && !nodes.empty())
    {
      targetBounds = boundsToJson(boundsForNodes(nodes));
    }
    else if (!error.isEmpty())
    {
      targetWarnings.push_back(error);
    }
  }
  else if (const auto boundsValue = params.value("bounds"); boundsValue.isObject())
  {
    targetBounds = boundsValue.toObject();
  }

  const auto requestedIsolate = mcpOptionalBool(params, "isolate", false);
  const auto isolateMode = normalizedIsolateMode(params);
  auto appliedIsolateMode = QString{};

  auto cameraControlled = false;
  auto focusedObjectCount = 0;
  auto cameraFrame = QJsonObject{};
  if (!objectIds.isEmpty())
  {
    focusedObjectCount = objectIds.size();
    if (!requestedIsolate)
    {
      const auto focusResult =
        viewportFocusResult(appController, QJsonObject{{"objectIds", objectIds}});
      if (!focusResult.ok)
      {
        return focusResult;
      }
      cameraControlled = focusResult.result.value("cameraControlled").toBool(false);
      focusedObjectCount = focusResult.result.value("focusedObjectCount").toInt(0);
    }

    if (mcpOptionalBool(params, "highlight", true) || requestedIsolate)
    {
      appliedIsolateMode = "highlight_only";
      overlayState.insert("highlightObjectIds", objectIds);
      if (const auto sceneName = params.value("sceneName").toString().trimmed();
          !sceneName.isEmpty())
      {
        auto labels = QJsonArray{};
        labels.push_back(QJsonObject{
          {"text", sceneName},
          {"objectId", objectIds.isEmpty() ? QJsonValue{} : objectIds.first()},
        });
        overlayState.insert("labels", labels);
      }
    }
    else
    {
      overlayState = QJsonObject{};
    }
    appController.refreshMcpOverlayViews();
  }
  if (requestedIsolate && isolateMode != "highlight_only")
  {
    targetWarnings.push_back(
      QString{"isolationModeFallback: %1 requested, but this build only supports "
              "non-persistent highlight_only review isolation without changing map "
              "visibility or undo state."}
        .arg(isolateMode));
  }

  if (const auto cameraValue = params.value("camera"); cameraValue.isObject())
  {
    auto cameraParams = cameraValue.toObject();
    auto cameraResult =
      cameraParams.contains("position") && cameraParams.contains("target")
        ? viewportCameraSetResult(appController, cameraParams)
        : McpBridgeToolResult::failure(
            mcp::McpErrorCode::InvalidParams,
            "camera requires position/target or bounds");
    if (
      !cameraParams.contains("position") && !cameraParams.contains("target")
      && !cameraParams.contains("objectIds") && !objectIds.isEmpty())
    {
      cameraParams.insert("objectIds", objectIds);
    }
    if (!(cameraParams.contains("position") && cameraParams.contains("target")))
    {
      cameraResult = viewportCameraFrameBoundsResult(appController, cameraParams);
    }
    if (!cameraResult.ok)
    {
      return cameraResult;
    }
    cameraFrame = cameraResult.result;
    cameraControlled = true;
  }
  else if (const auto framing = params.value("framing").toString("current");
           framing != "current" && !framing.trimmed().isEmpty())
  {
    auto cameraParams = QJsonObject{};
    if (!objectIds.isEmpty())
    {
      cameraParams.insert("objectIds", objectIds);
    }
    if (const auto bounds = params.value("bounds"); bounds.isObject())
    {
      cameraParams.insert("bounds", bounds);
    }
    applyFramingPreset(cameraParams, framing);
    const auto cameraResult =
      viewportCameraFrameBoundsResult(appController, cameraParams);
    if (!cameraResult.ok)
    {
      return cameraResult;
    }
    cameraFrame = cameraResult.result;
    cameraFrame.insert("framing", framing);
    cameraControlled = true;
  }

  if (mcpOptionalBool(params, "clearSelectionBeforeCapture", false))
  {
    auto* mapWindow = appController.mapWindowManager().topMapWindow();
    if (!mapWindow)
    {
      return noActiveDocumentFailure();
    }
    mdl::deselectAll(mapWindow->document().map());
    QCoreApplication::processEvents();
  }

  const auto views = stringArrayFromValueOrDefault(
    params.value("views"), QJsonArray{"current", "3d", "2d"});
  const auto captureParams =
    QJsonObject{{"returnBase64", mcpOptionalBool(params, "returnBase64", false)}};
  const auto min2dHeight = std::max(1, params.value("min2dHeight").toInt(360));
  auto captures = QJsonArray{};
  auto quality = QJsonArray{};
  auto warnings = QJsonArray{};
  appendWarnings(warnings, targetWarnings);

  for (const auto& viewValue : views)
  {
    const auto view = normalizedReviewViewName(viewValue.toString());
    if (view.isEmpty())
    {
      warnings.push_back(
        QString{"Unknown review view '%1'; expected current, 3d, 2d, "
                "overview_3d, detail_3d, top_2d_fit, or side_2d_fit."}
          .arg(viewValue.toString()));
      continue;
    }

    auto* captureMapWindow = appController.mapWindowManager().topMapWindow();
    const auto captureLayoutBefore = captureMapWindow != nullptr
                                       ? captureMapWindow->currentMapViewLayout()
                                       : originalLayout;
    if ((view == "top_2d_fit" || view == "side_2d_fit") && captureMapWindow != nullptr)
    {
      if (captureMapWindow->currentMapViewLayout() != MapViewLayout::TwoPanes)
      {
        captureMapWindow->switchMapViewLayout(MapViewLayout::TwoPanes);
        QCoreApplication::processEvents();
      }
      if (!objectIds.isEmpty())
      {
        if (!requestedIsolate)
        {
          const auto focusResult =
            viewportFocusResult(appController, QJsonObject{{"objectIds", objectIds}});
          if (!focusResult.ok)
          {
            warnings.push_back(QString{"Could not focus 2D review target: %1"}.arg(
              focusResult.error.message));
          }
        }
      }
    }

    auto captureResult =
      view == "current" ? viewportCaptureCurrentResult(appController, captureParams)
      : view == "3d"    ? viewportCapture3DResult(appController, captureParams)
      : (view == "top_2d_fit" || view == "side_2d_fit")
        ? viewportCaptureReview2DResult(appController, captureParams, view)
        : viewportCapture2DResult(appController, captureParams);
    if (!captureResult.ok)
    {
      warnings.push_back(
        QString{"Could not capture %1 view: %2"}.arg(view, captureResult.error.message));
      continue;
    }
    auto capture = captureResult.result;
    capture.insert("view", view);
    capture.insert("requestedView", viewValue.toString());
    capture.insert("isolate", requestedIsolate);
    capture.insert("isolateMode", appliedIsolateMode);
    capture.insert("framing", params.value("framing").toString("current"));
    capture.insert("cameraFrame", cameraFrame);
    capture.insert("layoutBeforeCapture", mapViewLayoutName(captureLayoutBefore));

    auto captureQuality = captureQualityJson(
      capture, view, min2dHeight, cameraControlled && cameraFrame.isEmpty());
    if (
      (view == "top_2d_fit" || view == "side_2d_fit")
      && !captureQualityValid(captureQuality))
    {
      appendWarnings(warnings, captureQuality.value("warnings").toArray());
      if (captureMapWindow != nullptr)
      {
        captureMapWindow->switchMapViewLayout(MapViewLayout::TwoPanes);
        QCoreApplication::processEvents();
        auto retryResult =
          viewportCaptureReview2DResult(appController, captureParams, view);
        if (retryResult.ok)
        {
          auto retryCapture = retryResult.result;
          retryCapture.insert("view", view);
          retryCapture.insert("requestedView", viewValue.toString());
          retryCapture.insert("retry", true);
          retryCapture.insert("isolate", requestedIsolate);
          retryCapture.insert("isolateMode", appliedIsolateMode);
          retryCapture.insert("framing", params.value("framing").toString("current"));
          retryCapture.insert("cameraFrame", cameraFrame);
          retryCapture.insert(
            "layoutBeforeCapture", mapViewLayoutName(MapViewLayout::TwoPanes));
          const auto retryQuality = captureQualityJson(
            retryCapture, view, min2dHeight, cameraControlled && cameraFrame.isEmpty());
          if (captureQualityValid(retryQuality) || !captureQualityValid(captureQuality))
          {
            capture = retryCapture;
            captureQuality = retryQuality;
          }
        }
      }
    }
    if (!captureQualityValid(captureQuality))
    {
      appendWarnings(warnings, captureQuality.value("warnings").toArray());
    }
    captures.push_back(capture);
    quality.push_back(captureQuality);
  }

  if (mcpOptionalBool(params, "clearSelectionAfter", false))
  {
    const auto clearResult = viewportClearMarksResult(
      appController, QJsonObject{{"clearSelection", true}}, overlayState);
    if (!clearResult.ok)
    {
      warnings.push_back(
        QString{"Could not clear review selection: %1"}.arg(clearResult.error.message));
    }
  }

  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (
    mapWindow != nullptr && !params.contains("layout")
    && mapWindow->currentMapViewLayout() != originalLayout)
  {
    mapWindow->switchMapViewLayout(originalLayout);
    QCoreApplication::processEvents();
  }
  const auto layout =
    mapWindow != nullptr ? viewportLayoutJson(*mapWindow) : QJsonObject{};
  const auto checklist = stringArrayFromValueOrDefault(
    params.value("checklist"), defaultSceneReviewChecklist());
  const auto qualityValid = std::ranges::all_of(
    quality, [](const auto& entry) { return entry.toObject().value("valid").toBool(); });
  return McpBridgeToolResult::success(QJsonObject{
    {"sceneName", params.value("sceneName").toString()},
    {"captureCount", captures.size()},
    {"captures", captures},
    {"quality", quality},
    {"qualityValid", qualityValid && captures.size() > 0 && focusedObjectCount > 0},
    {"checklist", checklist},
    {"warnings", warnings},
    {"layout", layout.value("layout")},
    {"viewportLayout", layout},
    {"cameraControlled", cameraControlled},
    {"cameraFrame", cameraFrame},
    {"focusedObjectCount", focusedObjectCount},
    {"targetObjectIds", objectIds},
    {"targetObjectCount", objectIds.size()},
    {"targetBounds", targetBounds},
    {"isolate", requestedIsolate},
    {"requestedIsolateMode", isolateMode},
    {"appliedIsolateMode", appliedIsolateMode},
    {"originalLayout", mapViewLayoutName(originalLayout)},
    {"requestedLayout",
     requestedLayoutValue.isString() ? requestedLayoutValue.toString() : QString{}},
    {"note",
     cameraControlled
       ? "This review package controlled the visible viewport target before capture."
       : "This review package uses the current visible TrenchBroom viewport state. "
         "Pass objectIds to focus the selection before capture."},
  });
}

McpBridgeToolResult renderReviewOperationResult(
  AppController& appController,
  const QJsonObject& params,
  QJsonObject& overlayState,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry)
{
  auto reviewParams = params;
  if (!reviewParams.contains("sceneName"))
  {
    reviewParams.insert("sceneName", "MCP isolated scene review");
  }
  if (!reviewParams.contains("isolate"))
  {
    reviewParams.insert("isolate", true);
  }
  if (!reviewParams.contains("isolateMode"))
  {
    reviewParams.insert("isolateMode", "hide_others");
  }
  if (!reviewParams.contains("views"))
  {
    reviewParams.insert(
      "views", QJsonArray{"overview_3d", "top_2d_fit", "side_2d_fit", "detail_3d"});
  }
  if (!reviewParams.contains("framing"))
  {
    reviewParams.insert(
      "framing", params.value("framingPreset").toString("overview_orbit"));
  }
  if (!reviewParams.contains("min2dHeight"))
  {
    reviewParams.insert("min2dHeight", 360);
  }
  if (!reviewParams.contains("highlight"))
  {
    reviewParams.insert("highlight", true);
  }
  if (!reviewParams.contains("returnBase64"))
  {
    reviewParams.insert("returnBase64", false);
  }

  auto review = viewportCaptureSceneReviewResult(
    appController, reviewParams, overlayState, history, objectRegistry);
  if (!review.ok)
  {
    return review;
  }

  const auto reviewId =
    QString{"review-%1"}.arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  review.result.insert("reviewId", reviewId);
  review.result.insert("resourceUri", QString{"tbmcp://review/%1"}.arg(reviewId));
  review.result.insert("tool", "render_review_operation");
  return review;
}

} // namespace tb::ui
