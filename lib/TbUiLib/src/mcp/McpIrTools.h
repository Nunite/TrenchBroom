/*
 Copyright (C) 2026 XiangXtreme

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

McpBridgeToolResult irValidateResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult irCompilePreviewResult(
  AppController& appController, const QJsonObject& params);
McpBridgeToolResult irCompilePreviewResult(
  AppController& appController,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult irCompilePreviewForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  const std::map<QString, McpBrushMetadataRecord>& metadataStore,
  const std::map<QString, McpModuleRecord>& moduleStore,
  const McpObjectRegistry& objectRegistry);
McpBridgeToolResult irCompilePreviewFromFileResult(
  AppController& appController,
  const QJsonObject& params,
  std::map<QString, McpIrPreviewCacheRecord>* previewCache = nullptr,
  int* nextPreviewIndex = nullptr,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore = nullptr,
  const std::map<QString, McpModuleRecord>* moduleStore = nullptr,
  const McpObjectRegistry* objectRegistry = nullptr);
McpBridgeToolResult irCompilePreviewFromFileForMapResult(
  mdl::Map& map,
  const QJsonObject& params,
  std::map<QString, McpIrPreviewCacheRecord>* previewCache = nullptr,
  int* nextPreviewIndex = nullptr,
  const std::map<QString, McpBrushMetadataRecord>* metadataStore = nullptr,
  const std::map<QString, McpModuleRecord>* moduleStore = nullptr,
  const McpObjectRegistry* objectRegistry = nullptr);
McpBridgeToolResult irApplyResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  McpObjectRegistry* objectRegistry = nullptr);
McpBridgeToolResult irApplyForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  McpObjectRegistry* objectRegistry = nullptr);
McpBridgeToolResult irApplyFromFileResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  McpObjectRegistry* objectRegistry = nullptr,
  std::map<QString, McpIrPreviewCacheRecord>* previewCache = nullptr);
McpBridgeToolResult irApplyFromFileForMapResult(
  mdl::Map& map,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  std::map<QString, McpBrushMetadataRecord>& metadataStore,
  std::map<QString, McpModuleRecord>& moduleStore,
  McpObjectRegistry* objectRegistry = nullptr,
  std::map<QString, McpIrPreviewCacheRecord>* previewCache = nullptr);

} // namespace tb::ui
