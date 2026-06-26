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
#include "mcp/McpToolCatalog.h"
#include "mdl/Brush.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/GameInfo.h"
#include "mdl/Grid.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Node.h"
#include "mdl/PatchNode.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/GetVersion.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"

#include "vm/bbox.h"

#include <filesystem>

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

vm::bbox3d contentBounds(const mdl::WorldNode& worldNode)
{
  auto result = vm::bbox3d{};
  auto hasBounds = false;

  worldNode.visitChildren([&](auto&& thisLambda, const mdl::Node& node) {
    if (
      dynamic_cast<const mdl::EntityNode*>(&node) != nullptr
      || dynamic_cast<const mdl::BrushNode*>(&node) != nullptr
      || dynamic_cast<const mdl::PatchNode*>(&node) != nullptr)
    {
      result = hasBounds ? vm::merge(result, node.logicalBounds()) : node.logicalBounds();
      hasBounds = true;
    }

    node.visitChildren(thisLambda);
  });

  return hasBounds ? result : vm::bbox3d{};
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

bool mcpOptionalBool(
  const QJsonObject& params, const QString& key, const bool defaultValue)
{
  const auto value = params.value(key);
  return value.isBool() ? value.toBool() : defaultValue;
}

} // namespace

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

  return mapSnapshotJsonForMap(mapWindow->document().map(), documentJson(*mapWindow, 0));
}

QJsonObject mapSnapshotJsonForMap(const mdl::Map& map, const QJsonObject& document)
{
  const auto& worldNode = map.worldNode();
  const auto& grid = map.grid();

  auto entities = 0;
  auto brushes = 0;
  auto patches = 0;
  collectMapCounts(worldNode, entities, brushes, patches);
  const auto mapContentBounds = contentBounds(worldNode);

  auto worldspawn = QJsonObject{};
  for (const auto& property : worldNode.entity().properties())
  {
    worldspawn.insert(
      QString::fromStdString(property.key()), QString::fromStdString(property.value()));
  }

  auto world = mcpNodeSummaryJson(worldNode, worldNode);
  world.insert("nodeLogicalBounds", world.value("logicalBounds"));
  world.insert("logicalBounds", boundsToJson(mapContentBounds));
  world.insert("contentBounds", boundsToJson(mapContentBounds));

  return QJsonObject{
    {"document", document},
    {"world", world},
    {"worldspawn", worldspawn},
    {"entityCount", entities},
    {"brushCount", brushes},
    {"patchCount", patches},
    {"nodeCount", static_cast<int>(worldNode.descendantCount() + 1)},
    {"bounds", boundsToJson(mapContentBounds)},
    {"contentBounds", boundsToJson(mapContentBounds)},
    {"grid",
     QJsonObject{
       {"size", grid.size()},
       {"actualSize", grid.actualSize()},
       {"snap", grid.snap()},
       {"visible", grid.visible()},
     }},
  };
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
  const auto implementedTools = mcp::toolsListJson(config.mode, true, config.toolProfile);
  return QJsonObject{
    {"configPath", mcp::defaultConfigPath()},
    {"pipeName", config.pipeName},
    {"mode", mcp::modeName(config.mode)},
    {"toolProfile", mcp::toolProfileName(config.toolProfile)},
    {"tokenPresent", !config.token.isEmpty()},
    {"listening", config.mode != mcp::McpMode::Off},
    {"documentCount",
     static_cast<int>(appController.mapWindowManager().mapWindows().size())},
    {"activeDocument", appController.mapWindowManager().topMapWindow() != nullptr},
    {"implementedToolCount", implementedTools.size()},
    {"implementedTools", implementedTools},
    {"toolDiagnostics", mcp::toolDiagnosticsJson(config.mode)},
  };
}

} // namespace tb::ui
