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

#include <vector>

namespace tb::mdl
{
class Map;
}

namespace tb::ui
{

class AppController;

McpBridgeToolResult noActiveDocumentFailure();
McpBridgeToolResult invalidParamsFailure(const QString& message);

template <typename Result>
QString resultErrorMessage(const Result& result)
{
  const auto error = result.error();
  return QString::fromStdString(std::get<Error>(error).msg);
}

QJsonObject makeStatus(AppController& appController, const mcp::McpBridgeConfig& config);
QJsonObject doctorJson(AppController& appController, const mcp::McpBridgeConfig& config);
QJsonObject documentsListJson(AppController& appController);
QJsonObject activeDocumentJson(AppController& appController);
QJsonObject mapSnapshotJson(AppController& appController);
McpBridgeToolResult documentOpenResult(
  AppController& appController, const QJsonObject& params);
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
McpBridgeToolResult textureApplyResult(
  AppController& appController,
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
McpBridgeToolResult selectionSetResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult selectionFilterResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult selectionByBoundsResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult selectionGrowResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult viewportFocusResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult viewportClearMarksResult(
  AppController& appController, const QJsonObject& params, QJsonObject& overlayState);
McpBridgeToolResult viewportCaptureCurrentResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult viewportCapture3DResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult viewportCapture2DResult(
  AppController& appController, const QJsonObject& params);

McpBridgeToolResult historyListResult(const std::vector<McpOperationRecord>& history);
McpBridgeToolResult operationInspectResult(
  const std::vector<McpOperationRecord>& history, const QJsonObject& params);
McpBridgeToolResult operationSelectResult(
  AppController& appController,
  const std::vector<McpOperationRecord>& history,
  const QJsonObject& params);
McpBridgeToolResult operationValidateResult(
  AppController& appController,
  const std::vector<McpOperationRecord>& history,
  const QJsonObject& params);
McpBridgeToolResult historyUndoResult(
  AppController& appController, std::vector<McpOperationRecord>& history);
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
McpBridgeToolResult transformObjectsResult(
  AppController& appController,
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
