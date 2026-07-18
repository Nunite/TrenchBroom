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

#include "mcp/McpStdioAdapter.h"

#include "mcp/McpError.h"
#include "mcp/McpJsonRpc.h"

namespace tb::mcp
{
namespace
{

McpBridgeResponse disabledResponse()
{
  return McpBridgeResponse::failure(
    {}, McpError{McpErrorCode::Forbidden, "TrenchBroom MCP bridge is disabled"});
}

} // namespace

std::optional<QJsonObject> handleMcpStdioJsonRpcRequest(
  const QJsonObject& request,
  const McpBridgeConfig& config,
  const McpBridgeClient& client)
{
  const auto enabled = config.mode != McpMode::Off;
  return handleMcpJsonRpcRequest(
    request,
    config.mode,
    [&](const QString& toolName, const QJsonObject& arguments) {
      return enabled ? client.call(config, toolName, arguments) : disabledResponse();
    },
    config.toolProfile,
    [&](const QString& cursor) {
      return enabled ? client.listResources(config, cursor) : disabledResponse();
    },
    [&](const QString& uri) {
      return enabled ? client.readResource(config, uri) : disabledResponse();
    });
}

} // namespace tb::mcp
