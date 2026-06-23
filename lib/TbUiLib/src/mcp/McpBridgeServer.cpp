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
