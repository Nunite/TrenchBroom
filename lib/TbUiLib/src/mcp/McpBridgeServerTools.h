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

#pragma once

#include <QJsonObject>
#include <QString>

#include "Result.h"
#include "ui/mcp/McpBridgeServer.h"

#include <map>
#include <vector>

namespace tb::mdl
{
class Map;
}

namespace tb::ui
{

class AppController;
class McpObjectRegistry;

struct McpSelectorDiagnostics
{
  int matchedBeforeLimit = 0;
  bool limitApplied = false;
  int staleExcluded = 0;
  int moduleObjectIdCount = 0;
  int operationObjectIdCount = 0;
  int metadataRecordCount = 0;
};

McpBridgeToolResult noActiveDocumentFailure();
McpBridgeToolResult invalidParamsFailure(const QString& message);

template <typename Result>
QString resultErrorMessage(const Result& result)
{
  const auto error = result.error();
  return QString::fromStdString(std::get<Error>(error).msg);
}

QJsonObject makeStatus(
  AppController& appController,
  const mcp::McpBridgeConfig& config,
  const QString& bridgeInstanceId = {},
  const QString& bridgeStartedAt = {},
  const McpObjectRegistry* objectRegistry = nullptr);
QJsonObject doctorJson(
  AppController& appController, const mcp::McpBridgeConfig& config, bool fullDetail);
QJsonObject bridgeIdentityJson(
  const QString& bridgeInstanceId,
  const QString& bridgeStartedAt,
  quint16 httpPort = 37666);
QJsonObject documentsListJson(
  AppController& appController, const McpObjectRegistry* objectRegistry = nullptr);
QJsonObject activeDocumentJson(
  AppController& appController,
  const QString& bridgeInstanceId = {},
  const QString& bridgeStartedAt = {},
  quint16 httpPort = 37666,
  const McpObjectRegistry* objectRegistry = nullptr);
QJsonObject mapSnapshotJson(
  AppController& appController,
  const QString& bridgeInstanceId = {},
  const QString& bridgeStartedAt = {},
  quint16 httpPort = 37666,
  const McpObjectRegistry* objectRegistry = nullptr);
QJsonObject mapSnapshotJsonForMap(const mdl::Map& map, const QJsonObject& document);
QString documentFingerprintForMap(
  const mdl::Map& map, const McpObjectRegistry* objectRegistry = nullptr);
int documentEpochForMap(
  const mdl::Map& map, const McpObjectRegistry* objectRegistry = nullptr);
QString activeDocumentPath(AppController& appController);
McpBridgeToolResult expectedDocumentPathFailure(
  AppController& appController,
  const QString& expectedPath,
  const QString& bridgeInstanceId,
  const QString& bridgeStartedAt,
  quint16 httpPort = 37666);
McpBridgeToolResult documentOpenResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult documentOpenVerifiedResult(
  AppController& appController,
  const QJsonObject& params,
  const QString& bridgeInstanceId = {},
  const QString& bridgeStartedAt = {},
  quint16 httpPort = 37666,
  const McpObjectRegistry* objectRegistry = nullptr);
McpBridgeToolResult documentActivateResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult documentSaveResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult documentCloseResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult documentExportResult(
  AppController& appController, const QJsonObject& params);

McpBridgeToolResult assetSearchResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult placeAssetResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult placeAssetForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);

McpBridgeToolResult textureSearchResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult textureSearchForMapResult(mdl::Map& map, const QJsonObject& params);
McpBridgeToolResult textureLockGetResult(AppController& appController);
McpBridgeToolResult textureLockSetResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult textureApplyResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult textureApplyByFilterResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult textureApplyByFilterForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry* objectRegistry = nullptr);
McpBridgeToolResult textureReplaceResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult textureAlignFaceResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult textureCopyFromFaceResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult textureCopyFromFaceForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult faceListResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult faceSelectResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult faceSelectForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult faceTextureSetResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult faceTextureSetForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry);

QJsonObject actionsListJson(AppController& appController);
McpBridgeToolResult actionExecuteResult(
  AppController& appController, const QJsonObject& params);

QJsonObject mapSearchJson(AppController& appController, const QJsonObject& params);
QJsonObject selectionJson(AppController& appController);
QJsonObject selectionJsonForMap(const mdl::Map& map);
McpBridgeToolResult selectionSetResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult selectionSetForMapResult(mdl::Map& map, const QJsonObject& params);
McpBridgeToolResult selectionFilterResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult selectionFilterForMapResult(mdl::Map& map, const QJsonObject& params);
McpBridgeToolResult selectionByBoundsResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult selectionByBoundsForMapResult(
  mdl::Map& map, const QJsonObject& params);
McpBridgeToolResult selectionGrowResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult selectionGrowForMapResult(mdl::Map& map, const QJsonObject& params);
McpBridgeToolResult viewportFocusResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult viewportClearMarksResult(
  AppController& appController, const QJsonObject& params, QJsonObject& overlayState);
McpBridgeToolResult viewportLayoutGetResult(AppController& appController);
McpBridgeToolResult viewportLayoutSetResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult viewportCameraFrameBoundsResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult viewportCameraSetResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult viewportCaptureCurrentResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult viewportCapture3DResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult viewportCapture2DResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult viewportCaptureSceneReviewResult(
  AppController& appController,
  const QJsonObject& params,
  QJsonObject& overlayState,
  const std::vector<McpOperationRecord>& history = {},
  const McpObjectRegistry* objectRegistry = nullptr);
McpBridgeToolResult renderReviewTargetsResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history = {},
  const McpObjectRegistry* objectRegistry = nullptr,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore = nullptr);
McpBridgeToolResult renderReviewTargetsForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history = {},
  const McpObjectRegistry* objectRegistry = nullptr,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore = nullptr);
McpBridgeToolResult renderReviewCurrentSceneForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history = {},
  const McpObjectRegistry* objectRegistry = nullptr,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore = nullptr);
McpBridgeToolResult renderReviewCurrentSceneResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history = {},
  const McpObjectRegistry* objectRegistry = nullptr,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore = nullptr);
McpBridgeToolResult renderReviewOperationResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history = {},
  const McpObjectRegistry* objectRegistry = nullptr,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore = nullptr);
McpBridgeToolResult selectorPreviewResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult selectorPreviewForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry);
QJsonObject selectorFromParams(const QJsonObject& params);
std::vector<mdl::Node*> resolveSelectorNodes(
  mdl::Map& map,
  const QJsonObject& selector,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry,
  QJsonArray& warnings,
  QString& error,
  McpSelectorDiagnostics* diagnostics = nullptr);
McpBridgeToolResult objectsSelectBySelectorResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult objectsDeleteBySelectorResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult objectsDeleteBySelectorForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult renderReviewSelectorResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult moduleListResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult moduleListForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult moduleInspectResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult moduleSelectResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult moduleRenderReviewResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult moduleValidateResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult moduleCompactResult(
  AppController& appController,
  const QJsonObject& params,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult irValidateResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult irCompilePreviewResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult irCompilePreviewFromFileResult(
  AppController& appController,
  const QJsonObject& params,
  std::map<QString, McpIrPreviewCacheRecord>* previewCache = nullptr,
  int* nextPreviewIndex = nullptr);
McpBridgeToolResult irCompilePreviewFromFileForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  std::map<QString, McpIrPreviewCacheRecord>* previewCache = nullptr,
  int* nextPreviewIndex = nullptr);
McpBridgeToolResult irApplyResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry* objectRegistry = nullptr);
McpBridgeToolResult irApplyForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry* objectRegistry = nullptr);
McpBridgeToolResult irApplyFromFileResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry* objectRegistry = nullptr,
  std::map<QString, McpIrPreviewCacheRecord>* previewCache = nullptr);
McpBridgeToolResult irApplyFromFileForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry* objectRegistry = nullptr,
  std::map<QString, McpIrPreviewCacheRecord>* previewCache = nullptr);

McpBridgeToolResult historyListResult(const std::vector<McpOperationRecord>& history);
McpBridgeToolResult historyListResult(
  AppController& appController,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult historyStatusResult(
  AppController& appController,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry& objectRegistry,
  const QString& bridgeInstanceId = {},
  const QString& bridgeStartedAt = {});
McpBridgeToolResult historyStatusForMapResult(
  mdl::Map& map,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry& objectRegistry,
  const QString& bridgeInstanceId = {},
  const QString& bridgeStartedAt = {},
  const QString& activeDocumentPath = {});
McpBridgeToolResult operationInspectResult(
  const std::vector<McpOperationRecord>& history, const QJsonObject& params);
McpBridgeToolResult operationInspectResult(
  AppController& appController,
  const std::vector<McpOperationRecord>& history,
  const QJsonObject& params,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult operationInspectResult(
  AppController& appController,
  const std::vector<McpOperationRecord>& history,
  const QJsonObject& params);
McpBridgeToolResult operationSelectResult(
  AppController& appController,
  const std::vector<McpOperationRecord>& history,
  const QJsonObject& params,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult operationSelectResult(
  AppController& appController,
  const std::vector<McpOperationRecord>& history,
  const QJsonObject& params);
McpBridgeToolResult operationValidateResult(
  AppController& appController,
  const std::vector<McpOperationRecord>& history,
  const QJsonObject& params,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult operationValidateForMapResult(
  mdl::Map& map,
  const std::vector<McpOperationRecord>& history,
  const QJsonObject& params,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult operationValidateResult(
  AppController& appController,
  const std::vector<McpOperationRecord>& history,
  const QJsonObject& params);
McpBridgeToolResult historyUndoResult(
  AppController& appController,
  std::vector<McpOperationRecord>& history,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult historyUndoForMapResult(
  mdl::Map& map,
  std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry = nullptr);
McpBridgeToolResult historyUndoToOperationResult(
  AppController& appController,
  std::vector<McpOperationRecord>& history,
  const QJsonObject& params,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult historyUndoToOperationForMapResult(
  mdl::Map& map,
  std::vector<McpOperationRecord>& history,
  const QJsonObject& params,
  const McpObjectRegistry* objectRegistry = nullptr);
McpBridgeToolResult historyRedoResult(
  AppController& appController,
  std::vector<McpOperationRecord>& history,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult historyRedoForMapResult(
  mdl::Map& map,
  std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry = nullptr);

McpBridgeToolResult createEntityResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult createEntityForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult updateEntityResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult updateEntityForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult deleteEntityResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult deleteEntityForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult entityPropertiesUpdateResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& operationHistory,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult entityPropertiesUpdateForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& operationHistory,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult entityPropertiesDeleteResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& operationHistory,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult entityPropertiesDeleteForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& operationHistory,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult fgdEntitiesListResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult entitySchemaResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult createEntityFromSchemaResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult createEntityFromSchemaForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult createEntityCheckedResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult createEntityCheckedForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult createEntityCheckedBatchResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult createEntityCheckedBatchForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult tieBrushesResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult tieBrushesForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult untieBrushesResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult untieBrushesForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);

McpBridgeToolResult deleteObjectsResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult deleteObjectsForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult deleteObjectsByFilterResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult deleteObjectsByOperationResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult deleteObjectsByOperationForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult groupCreateFromSelectionResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry,
  std::map<QString, McpBrushMetadataRecord>* metadataStore = nullptr,
  std::map<QString, McpModuleRecord>* moduleStore = nullptr);
McpBridgeToolResult groupCreateFromSelectionForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry,
  std::map<QString, McpBrushMetadataRecord>* metadataStore = nullptr,
  std::map<QString, McpModuleRecord>* moduleStore = nullptr);
McpBridgeToolResult groupInspectResult(
  AppController& appController,
  const QJsonObject& params,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult groupInspectForMapResult(
  mdl::Map& map, const QJsonObject& params, const McpObjectRegistry& objectRegistry);
McpBridgeToolResult groupRenameSelectedResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult groupRenameSelectedForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult groupUngroupSelectedResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry,
  std::map<QString, McpBrushMetadataRecord>* metadataStore = nullptr,
  std::map<QString, McpModuleRecord>* moduleStore = nullptr);
McpBridgeToolResult groupUngroupSelectedForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry,
  std::map<QString, McpBrushMetadataRecord>* metadataStore = nullptr,
  std::map<QString, McpModuleRecord>* moduleStore = nullptr);
McpBridgeToolResult transformObjectsResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore,
  const std::map<QString, McpModuleRecord>* moduleStore);
McpBridgeToolResult transformObjectsForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult transformObjectsForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const McpObjectRegistry& objectRegistry,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore,
  const std::map<QString, McpModuleRecord>* moduleStore);

McpBridgeToolResult brushTypesListResult();
McpBridgeToolResult createBrushResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult createBoxesBatchResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult createBoxesBatchForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult blockoutValidateResult(const QJsonObject& params);
McpBridgeToolResult blockoutCreateResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult blockoutCreateBatchResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>* metadataStore = nullptr,
  std::map<QString, McpModuleRecord>* moduleStore = nullptr,
  const McpObjectRegistry* objectRegistry = nullptr);
McpBridgeToolResult blockoutCreateBatchForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult blockoutCreateBatchForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>* metadataStore,
  std::map<QString, McpModuleRecord>* moduleStore = nullptr,
  const McpObjectRegistry* objectRegistry = nullptr);
McpBridgeToolResult blockoutCompilePreviewForMapResult(
  mdl::Map& map, const QJsonObject& params);
McpBridgeToolResult pythonGenerateBlockoutResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore);
McpBridgeToolResult pythonGenerateBlockoutForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore);
McpBridgeToolResult heightmapImportGrayscaleResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult heightmapPreviewGrayscaleResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult heightmapPreviewGrayscaleForMapResult(
  mdl::Map& map, const QJsonObject& params);
McpBridgeToolResult heightmapImportGrayscaleForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);

McpBridgeToolResult geometryCsgSelectionResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult geometryCsgSelectionForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);

McpBridgeToolResult shapeLibraryListResult();
McpBridgeToolResult brushCreatePolygonBatchResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore);
McpBridgeToolResult brushCreatePolygonBatchForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore);
int storeBatchOperationMetadata(
  const QJsonArray& operations,
  const QStringList& changedObjectIds,
  std::map<QString, McpBrushMetadataRecord>& metadataStore);
int storeBatchOperationMetadata(
  const QJsonArray& operations,
  const QStringList& changedObjectIds,
  const QJsonObject& defaultMetadata,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>* moduleStore,
  const QString& operationId);
int storeBatchOperationMetadata(
  const QJsonArray& operations,
  const QStringList& changedObjectIds,
  const QString& documentFingerprint,
  const QJsonObject& defaultMetadata,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>* moduleStore,
  const QString& operationId);
McpBridgeToolResult brushMetadataSetForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  std::map<QString, McpBrushMetadataRecord>& metadataStore);
McpBridgeToolResult brushMetadataGetForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore);
McpBridgeToolResult selectionByMetadataForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore);
McpBridgeToolResult routeGeometryAnalyzeChainForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore);
McpBridgeToolResult kzDistanceAnalyzeChainForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore);
McpBridgeToolResult brushMetadataSetResult(
  AppController& appController,
  const QJsonObject& params,
  std::map<QString, McpBrushMetadataRecord>& metadataStore);
McpBridgeToolResult brushMetadataGetResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore);
McpBridgeToolResult selectionByMetadataResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore);
McpBridgeToolResult routeGeometryAnalyzeChainResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore);
McpBridgeToolResult kzDistanceAnalyzeChainResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore);
McpBridgeToolResult blockoutCreateSpiralStairsForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult geometryAnalyzeSelectionResult(
  mdl::Map& map, const QJsonObject& params);
McpBridgeToolResult geometryAnalyzeSelectionResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult geometryAnalyzeSlopesForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry = nullptr,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore = nullptr,
  const std::map<QString, McpModuleRecord>* moduleStore = nullptr);
McpBridgeToolResult geometryAnalyzeSlopesResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry = nullptr,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore = nullptr,
  const std::map<QString, McpModuleRecord>* moduleStore = nullptr);
McpBridgeToolResult geometryAnalyzeRouteContinuityForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry = nullptr,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore = nullptr,
  const std::map<QString, McpModuleRecord>* moduleStore = nullptr);
McpBridgeToolResult geometryAnalyzeRouteContinuityResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history,
  const McpObjectRegistry* objectRegistry = nullptr,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore = nullptr,
  const std::map<QString, McpModuleRecord>* moduleStore = nullptr);
McpBridgeToolResult blockoutValidateSpiralStairsResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history);
McpBridgeToolResult blockoutValidateSpiralStairsResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history);

McpBridgeToolResult compileProfilesListResult(AppController& appController);
McpBridgeToolResult compileRunResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult compileLogTailResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult leaksLoadPointfileResult(
  AppController& appController, const QJsonObject& params);

McpBridgeToolResult problemsCheckResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult mapValidateResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult problemsFixResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult problemsFixForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult mapFixAllSafeResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult mapFixAllSafeForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);

} // namespace tb::ui
