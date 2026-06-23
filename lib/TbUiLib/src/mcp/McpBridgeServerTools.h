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

McpBridgeToolResult compileProfilesListResult(AppController& appController);
McpBridgeToolResult compileRunResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult compileLogTailResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult leaksLoadPointfileResult(
  AppController& appController, const QJsonObject& params);

} // namespace tb::ui
