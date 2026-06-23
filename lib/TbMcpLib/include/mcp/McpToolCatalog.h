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

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "mcp/McpMode.h"

#include <optional>
#include <vector>

namespace tb::mcp
{

struct McpToolDefinition
{
  QString name;
  QString description;
  McpMode requiredMode = McpMode::ReadOnly;
  bool mutatesDocument = false;
  bool implemented = true;
  QJsonObject inputSchema;
};

const std::vector<McpToolDefinition>& defaultToolCatalog();

std::optional<McpToolDefinition> findToolDefinition(const QString& name);
bool canCallTool(const McpToolDefinition& tool, McpMode mode);

QJsonObject toMcpToolJson(const McpToolDefinition& tool);
QJsonObject toMcpToolDiagnosticJson(const McpToolDefinition& tool, McpMode currentMode);
QJsonArray toolsListJson(McpMode mode, bool implementedOnly = true);
QJsonArray toolDiagnosticsJson(McpMode currentMode);

} // namespace tb::mcp
