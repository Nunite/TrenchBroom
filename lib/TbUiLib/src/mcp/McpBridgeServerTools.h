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
QJsonObject doctorJson(AppController& appController, const mcp::McpBridgeConfig& config);
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
  int& nextOperationIndex);
McpBridgeToolResult textureApplyByFilterResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult textureApplyByFilterForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
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
  int& nextOperationIndex);
McpBridgeToolResult textureCopyFromFaceResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult faceListResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult faceSelectResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult faceTextureSetResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);

QJsonObject actionsListJson(AppController& appController);
McpBridgeToolResult actionExecuteResult(
  AppController& appController, const QJsonObject& params);

QJsonObject mapSearchJson(AppController& appController, const QJsonObject& params);
QJsonObject selectionJson(AppController& appController);
QJsonObject selectionJsonForMap(const mdl::Map& map);
McpBridgeToolResult selectionSetResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult selectionFilterResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult selectionFilterForMapResult(mdl::Map& map, const QJsonObject& params);
McpBridgeToolResult selectionByBoundsResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult selectionByBoundsForMapResult(
  mdl::Map& map, const QJsonObject& params);
McpBridgeToolResult selectionGrowResult(
  AppController& appController, const QJsonObject& params);
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
  const McpObjectRegistry* objectRegistry = nullptr);
McpBridgeToolResult renderReviewTargetsForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history = {},
  const McpObjectRegistry* objectRegistry = nullptr);
McpBridgeToolResult renderReviewCurrentSceneForMapResult(
  mdl::Map& map, const QJsonObject& params);
McpBridgeToolResult renderReviewCurrentSceneResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult renderReviewOperationResult(
  AppController& appController,
  const QJsonObject& params,
  const std::vector<McpOperationRecord>& history = {},
  const McpObjectRegistry* objectRegistry = nullptr);

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
  AppController& appController, std::vector<McpOperationRecord>& history);
McpBridgeToolResult historyUndoForMapResult(
  mdl::Map& map, std::vector<McpOperationRecord>& history);
McpBridgeToolResult historyRedoResult(
  AppController& appController, std::vector<McpOperationRecord>& history);

McpBridgeToolResult createEntityResult(
  AppController& appController,
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
McpBridgeToolResult deleteEntityResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
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
McpBridgeToolResult createEntityCheckedResult(
  AppController& appController,
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
McpBridgeToolResult untieBrushesResult(
  AppController& appController,
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
McpBridgeToolResult transformObjectsResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
McpBridgeToolResult transformObjectsForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);

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
  int& nextOperationIndex);
McpBridgeToolResult blockoutCreateBatchForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);
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
McpBridgeToolResult mapFixAllSafeResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex);

} // namespace tb::ui
