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

#include "ui/mcp/McpBridgeServer.h"

#include <vector>

namespace tb::mdl
{
class Map;
}

namespace tb::ui
{

class AppController;

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
McpBridgeToolResult operationSelectForMapResult(
  mdl::Map& map,
  const std::vector<McpOperationRecord>& history,
  const QJsonObject& params,
  const McpObjectRegistry* objectRegistry = nullptr);
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

} // namespace tb::ui
