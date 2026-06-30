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

#include <QDateTime>
#include <QJsonObject>
#include <QLocalServer>
#include <QUuid>

#include "McpBridgeServerTools.h"
#include "mcp/McpError.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"

#include <utility>

namespace tb::ui
{
namespace mcp = tb::mcp;

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

McpBridgeToolResult McpBridgeToolResult::failure(
  const mcp::McpErrorCode code, QString message, QJsonObject details)
{
  return McpBridgeToolResult{
    false, {}, mcp::McpError{code, std::move(message), std::move(details)}};
}

McpBridgeServer::McpBridgeServer(AppController& appController, QObject* parent)
  : McpBridgeServer{
      [&appController, this](const auto& toolName, const auto& params) {
        if (toolName == "tb_status")
        {
          return McpBridgeToolResult::success(makeStatus(
            appController,
            m_config,
            m_bridgeInstanceId,
            m_bridgeStartedAtUtc.toString(Qt::ISODateWithMs),
            &m_objectRegistry));
        }
        if (toolName == "tb_doctor")
        {
          const auto fullDetail =
            params.value("detail").toString("summary").trimmed().toLower() == "full";
          auto doctor = doctorJson(appController, m_config, fullDetail);
          doctor.insert("overlay", m_overlayState);
          return McpBridgeToolResult::success(std::move(doctor));
        }
        if (toolName == "tb_tools_search")
        {
          return McpBridgeToolResult::success(QJsonObject{
            {"tools",
             mcp::toolsSearchJson(
               params.value("query").toString(),
               params.value("category").toString(),
               params.value("detail").toString("summary"),
               m_config.mode,
               m_config.toolProfile)},
            {"toolProfile", mcp::toolProfileName(m_config.toolProfile)},
          });
        }
        if (toolName == "documents_list")
        {
          return McpBridgeToolResult::success(documentsListJson(appController));
        }
        if (toolName == "documents_open")
        {
          return documentOpenResult(appController, params);
        }
        if (toolName == "documents_open_verified")
        {
          return documentOpenVerifiedResult(
            appController,
            params,
            m_bridgeInstanceId,
            m_bridgeStartedAtUtc.toString(Qt::ISODateWithMs),
            m_config.httpPort,
            &m_objectRegistry);
        }
        if (toolName == "documents_activate")
        {
          return documentActivateResult(appController, params);
        }
        if (
          toolName == "documents_save" || toolName == "documents_save_current"
          || toolName == "documents_save_as")
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
          return McpBridgeToolResult::success(activeDocumentJson(
            appController,
            m_bridgeInstanceId,
            m_bridgeStartedAtUtc.toString(Qt::ISODateWithMs),
            m_config.httpPort,
            &m_objectRegistry));
        }
        if (toolName == "map_snapshot")
        {
          return McpBridgeToolResult::success(mapSnapshotJson(
            appController,
            m_bridgeInstanceId,
            m_bridgeStartedAtUtc.toString(Qt::ISODateWithMs),
            m_config.httpPort,
            &m_objectRegistry));
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
        if (toolName == "viewport_layout_get")
        {
          return viewportLayoutGetResult(appController);
        }
        if (toolName == "viewport_layout_set")
        {
          return viewportLayoutSetResult(appController, params);
        }
        if (toolName == "viewport_camera_frame_bounds")
        {
          return viewportCameraFrameBoundsResult(appController, params);
        }
        if (toolName == "viewport_camera_set")
        {
          return viewportCameraSetResult(appController, params);
        }
        if (toolName == "viewport_capture_current")
        {
          return viewportCaptureCurrentResult(appController, params);
        }
        if (toolName == "viewport_capture_3d")
        {
          return viewportCapture3DResult(appController, params);
        }
        if (toolName == "viewport_capture_2d")
        {
          return viewportCapture2DResult(appController, params);
        }
        if (toolName == "viewport_capture_scene_review")
        {
          return viewportCaptureSceneReviewResult(
            appController, params, m_overlayState, m_operationHistory, &m_objectRegistry);
        }
        if (toolName == "render_review_targets")
        {
          return renderReviewTargetsResult(
            appController,
            params,
            m_operationHistory,
            &m_objectRegistry,
            &m_brushMetadata);
        }
        if (toolName == "render_review_current_scene")
        {
          return renderReviewCurrentSceneResult(
            appController,
            params,
            m_operationHistory,
            &m_objectRegistry,
            &m_brushMetadata);
        }
        if (toolName == "render_review_operation")
        {
          return renderReviewOperationResult(
            appController,
            params,
            m_operationHistory,
            &m_objectRegistry,
            &m_brushMetadata);
        }
        if (toolName == "selector_preview")
        {
          return selectorPreviewResult(
            appController,
            params,
            m_operationHistory,
            m_brushMetadata,
            m_modules,
            m_objectRegistry);
        }
        if (toolName == "objects_select_by_selector")
        {
          return objectsSelectBySelectorResult(
            appController,
            params,
            m_operationHistory,
            m_brushMetadata,
            m_modules,
            m_objectRegistry);
        }
        if (toolName == "objects_delete_by_selector")
        {
          return objectsDeleteBySelectorResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_brushMetadata,
            m_modules,
            m_objectRegistry);
        }
        if (toolName == "render_review_selector")
        {
          return renderReviewSelectorResult(
            appController,
            params,
            m_operationHistory,
            m_brushMetadata,
            m_modules,
            m_objectRegistry);
        }
        if (toolName == "module_list")
        {
          return moduleListResult(
            appController, params, m_brushMetadata, m_modules, m_objectRegistry);
        }
        if (toolName == "module_inspect")
        {
          return moduleInspectResult(
            appController, params, m_brushMetadata, m_modules, m_objectRegistry);
        }
        if (toolName == "module_select")
        {
          return moduleSelectResult(
            appController, params, m_brushMetadata, m_modules, m_objectRegistry);
        }
        if (toolName == "module_render_review")
        {
          return moduleRenderReviewResult(
            appController,
            params,
            m_operationHistory,
            m_brushMetadata,
            m_modules,
            m_objectRegistry);
        }
        if (toolName == "module_validate")
        {
          return moduleValidateResult(
            appController,
            params,
            m_operationHistory,
            m_brushMetadata,
            m_modules,
            m_objectRegistry);
        }
        if (toolName == "module_compact")
        {
          return moduleCompactResult(
            appController, params, m_brushMetadata, m_modules, m_objectRegistry);
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
          appController.refreshMcpOverlayViews();
          return McpBridgeToolResult::success(QJsonObject{
            {"overlay", m_overlayState},
            {"active", true},
          });
        }
        if (toolName == "overlay_clear")
        {
          m_overlayState = QJsonObject{};
          appController.refreshMcpOverlayViews();
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
        if (toolName == "entity_properties_update")
        {
          return entityPropertiesUpdateResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry);
        }
        if (toolName == "entity_properties_delete")
        {
          return entityPropertiesDeleteResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry);
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
        if (toolName == "entity_create_checked")
        {
          return createEntityCheckedResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "entity_create_checked_batch")
        {
          return createEntityCheckedBatchResult(
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
        if (toolName == "shape_library_list")
        {
          return shapeLibraryListResult();
        }
        if (
          toolName == "brush_create" || toolName == "brush_create_box"
          || toolName == "brush_create_wedge" || toolName == "brush_create_cylinder"
          || toolName == "brush_create_cone" || toolName == "brush_create_pipe"
          || toolName == "brush_create_sphere" || toolName == "brush_create_pyramid"
          || toolName == "brush_create_tetrahedron"
          || toolName == "brush_create_from_planes" || toolName == "brush_create_prism"
          || toolName == "brush_create_cylinder_sector")
        {
          return createBrushResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "brush_create_boxes_batch")
        {
          return createBoxesBatchResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "brush_create_polygon_batch")
        {
          return brushCreatePolygonBatchResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_brushMetadata);
        }
        if (toolName == "brush_metadata_set")
        {
          return brushMetadataSetResult(appController, params, m_brushMetadata);
        }
        if (toolName == "brush_metadata_get")
        {
          return brushMetadataGetResult(appController, params, m_brushMetadata);
        }
        if (toolName == "selection_by_metadata")
        {
          return selectionByMetadataResult(appController, params, m_brushMetadata);
        }
        if (
          toolName == "route_geometry_analyze_chain"
          || toolName == "kz_distance_analyze_chain")
        {
          return routeGeometryAnalyzeChainResult(appController, params, m_brushMetadata);
        }
        if (toolName == "history_list")
        {
          return historyListResult(appController, m_operationHistory, m_objectRegistry);
        }
        if (toolName == "history_status")
        {
          return historyStatusResult(
            appController,
            m_operationHistory,
            m_objectRegistry,
            m_bridgeInstanceId,
            m_bridgeStartedAtUtc.toString(Qt::ISODateWithMs));
        }
        if (toolName == "operation_inspect")
        {
          return operationInspectResult(
            appController, m_operationHistory, params, m_objectRegistry);
        }
        if (toolName == "operation_select")
        {
          return operationSelectResult(
            appController, m_operationHistory, params, m_objectRegistry);
        }
        if (toolName == "operation_validate")
        {
          return operationValidateResult(
            appController, m_operationHistory, params, m_objectRegistry);
        }
        if (toolName == "history_undo_mcp")
        {
          return historyUndoResult(appController, m_operationHistory);
        }
        if (toolName == "history_undo_to_operation")
        {
          return historyUndoToOperationResult(appController, m_operationHistory, params);
        }
        if (toolName == "history_redo_mcp")
        {
          return historyRedoResult(appController, m_operationHistory);
        }
        if (toolName == "objects_delete_by_operation")
        {
          return deleteObjectsByOperationResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry);
        }
        if (toolName == "group_create_from_selection")
        {
          return groupCreateFromSelectionResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry,
            &m_brushMetadata,
            &m_modules);
        }
        if (toolName == "group_inspect")
        {
          return groupInspectResult(appController, params, m_objectRegistry);
        }
        if (toolName == "group_rename_selected")
        {
          return groupRenameSelectedResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry);
        }
        if (toolName == "group_ungroup_selected")
        {
          return groupUngroupSelectedResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry,
            &m_brushMetadata,
            &m_modules);
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
        if (toolName == "texture_lock_get")
        {
          return textureLockGetResult(appController);
        }
        if (toolName == "texture_lock_set")
        {
          return textureLockSetResult(appController, params);
        }
        if (toolName == "texture_apply")
        {
          return textureApplyResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry);
        }
        if (toolName == "texture_apply_by_filter")
        {
          return textureApplyByFilterResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry);
        }
        if (toolName == "texture_replace")
        {
          return textureReplaceResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "texture_align_face")
        {
          return textureAlignFaceResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry);
        }
        if (toolName == "texture_copy_from_face")
        {
          return textureCopyFromFaceResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry);
        }
        if (toolName == "face_list")
        {
          return faceListResult(
            appController, params, m_operationHistory, m_objectRegistry);
        }
        if (toolName == "face_select")
        {
          return faceSelectResult(
            appController, params, m_operationHistory, m_objectRegistry);
        }
        if (toolName == "face_texture_set")
        {
          return faceTextureSetResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry);
        }
        if (toolName == "objects_delete")
        {
          return deleteObjectsResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "objects_delete_by_filter")
        {
          return deleteObjectsByFilterResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "objects_transform")
        {
          return transformObjectsResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_objectRegistry,
            &m_brushMetadata,
            &m_modules);
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
        if (toolName == "python_generate_blockout")
        {
          return pythonGenerateBlockoutResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_brushMetadata);
        }
        if (toolName == "heightmap_import_grayscale")
        {
          return heightmapImportGrayscaleResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "heightmap_preview_grayscale")
        {
          return heightmapPreviewGrayscaleResult(appController, params);
        }
        if (toolName == "ir_validate")
        {
          return irValidateResult(appController, params);
        }
        if (toolName == "ir_compile_preview")
        {
          return irCompilePreviewResult(appController, params);
        }
        if (toolName == "ir_compile_preview_from_file")
        {
          return irCompilePreviewFromFileResult(
            appController, params, &m_irPreviewCache, &m_nextIrPreviewIndex);
        }
        if (toolName == "ir_apply")
        {
          return irApplyResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_brushMetadata,
            m_modules,
            &m_objectRegistry);
        }
        if (toolName == "ir_apply_from_file")
        {
          return irApplyFromFileResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            m_brushMetadata,
            m_modules,
            &m_objectRegistry,
            &m_irPreviewCache);
        }
        if (
          toolName == "blockout_create_batch"
          || toolName == "blockout_create_curved_corridor")
        {
          return blockoutCreateBatchResult(
            appController,
            toolName,
            params,
            m_operationHistory,
            m_nextOperationIndex,
            &m_brushMetadata,
            &m_modules,
            &m_objectRegistry);
        }
        if (
          toolName == "blockout_create_room" || toolName == "blockout_create_corridor"
          || toolName == "blockout_create_stairs" || toolName == "blockout_create_ramp"
          || toolName == "blockout_create_doorway" || toolName == "blockout_create_cover"
          || toolName == "blockout_create_sky_shell"
          || toolName == "blockout_create_spiral_stairs")
        {
          return blockoutCreateResult(
            appController, toolName, params, m_operationHistory, m_nextOperationIndex);
        }
        if (toolName == "blockout_validate")
        {
          return blockoutValidateResult(params);
        }
        if (toolName == "geometry_analyze_selection")
        {
          return geometryAnalyzeSelectionResult(appController, params);
        }
        if (toolName == "geometry_analyze_slopes")
        {
          return geometryAnalyzeSlopesResult(
            appController,
            params,
            m_operationHistory,
            &m_objectRegistry,
            &m_brushMetadata,
            &m_modules);
        }
        if (toolName == "geometry_analyze_route_continuity")
        {
          return geometryAnalyzeRouteContinuityResult(
            appController,
            params,
            m_operationHistory,
            &m_objectRegistry,
            &m_brushMetadata,
            &m_modules);
        }
        if (toolName == "blockout_validate_spiral_stairs")
        {
          return blockoutValidateSpiralStairsResult(
            appController, params, m_operationHistory);
        }
        return McpBridgeToolResult::failure(
          mcp::McpErrorCode::ToolNotFound,
          QString{"MCP tool is registered but not wired yet: %1"}.arg(toolName));
      },
      parent}
{
  m_activeMapProvider = [&appController]() -> mdl::Map* {
    auto* mapWindow = appController.mapWindowManager().topMapWindow();
    return mapWindow != nullptr ? &mapWindow->document().map() : nullptr;
  };
}

McpBridgeServer::McpBridgeServer(ToolHandler toolHandler, QObject* parent)
  : QObject{parent}
  , m_toolHandler{std::move(toolHandler)}
{
}

McpBridgeServer::McpBridgeServer(
  ToolHandler toolHandler, ActiveMapProvider activeMapProvider, QObject* parent)
  : QObject{parent}
  , m_toolHandler{std::move(toolHandler)}
  , m_activeMapProvider{std::move(activeMapProvider)}
{
}

McpBridgeServer::~McpBridgeServer()
{
  stop();
}

} // namespace tb::ui
