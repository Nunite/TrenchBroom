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
#include <QIODevice>
#include <QJsonArray>
#include <QJsonObject>
#include <QPixmap>
#include <QStringList>

#include "McpBridgeServerTools.h"
#include "McpSelectionQuery.h"
#include "mcp/McpError.h"
#include "mdl/Brush.h"
#include "mdl/BrushFace.h"
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
#include "ui/Action.h"
#include "ui/ActionExecutionContext.h"
#include "ui/ActionManager.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapView2D.h"
#include "ui/MapView3D.h"
#include "ui/MapViewBase.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"
#include "ui/SystemPaths.h"

#include "vm/bbox.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
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

template <typename View>
View* findCaptureView(MapWindow& mapWindow)
{
  if (auto* currentView = dynamic_cast<View*>(mapWindow.currentMapViewBase()))
  {
    return currentView;
  }

  return mapWindow.findChild<View*>();
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

  return QJsonObject{
    {"hasSelection", selection.hasAny()},
    {"nodes", nodes},
    {"nodeCount", static_cast<int>(selection.nodes.size())},
    {"groupCount", static_cast<int>(selection.groups.size())},
    {"entityCount", static_cast<int>(selection.entities.size())},
    {"brushCount", static_cast<int>(selection.brushes.size())},
    {"patchCount", static_cast<int>(selection.patches.size())},
    {"brushFaceCount", static_cast<int>(selection.brushFaces.size())},
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

  return QJsonObject{
    {"hasSelection", selection.hasAny()},
    {"nodeCount", static_cast<int>(selection.nodes.size())},
    {"groupCount", static_cast<int>(selection.groups.size())},
    {"entityCount", static_cast<int>(selection.entities.size())},
    {"brushCount", static_cast<int>(selection.brushes.size())},
    {"patchCount", static_cast<int>(selection.patches.size())},
    {"brushFaceCount", static_cast<int>(selection.brushFaces.size())},
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

  const auto& actionsMap = appController.actionManager().actionsMap();
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  auto* mapView = mapWindow ? mapWindow->currentMapViewBase() : nullptr;
  if (mapWindow && !mapView)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden, "No active map view");
  }
  auto context = ActionExecutionContext{appController, mapWindow, mapView};
  const auto actionPath = std::filesystem::path{"Menu/View/Focus on Selection"};
  const auto actionIt = actionsMap.find(actionPath);
  if (actionIt != std::end(actionsMap) && actionIt->second.enabled(context))
  {
    actionIt->second.execute(context);
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"focused", true},
    {"selection", selectionSummaryJson(appController)},
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

} // namespace tb::ui
