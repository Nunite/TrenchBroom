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

#include "mcp/McpJsonRpc.h"

#include <QJsonArray>
#include <QJsonDocument>

#include "mcp/McpError.h"
#include "mcp/McpToolCatalog.h"

namespace tb::mcp
{
namespace
{

constexpr auto ProtocolVersion = "2025-06-18";
constexpr auto ServerName = "trenchbroom-mcp";
constexpr auto ServerVersion = "0.1.0";

QJsonObject textToolResult(
  const QString& text, const bool isError, QJsonObject structuredContent = {})
{
  auto result = QJsonObject{
    {"content",
     QJsonArray{
       QJsonObject{
         {"type", "text"},
         {"text", text},
       },
     }},
    {"isError", isError},
  };

  if (!structuredContent.isEmpty())
  {
    result.insert("structuredContent", std::move(structuredContent));
  }

  return result;
}

QString compactJsonText(const QJsonObject& json)
{
  return QString::fromUtf8(QJsonDocument{json}.toJson(QJsonDocument::Compact));
}

} // namespace

QJsonObject jsonRpcResult(const QJsonValue& id, QJsonObject result)
{
  return QJsonObject{
    {"jsonrpc", "2.0"},
    {"id", id},
    {"result", std::move(result)},
  };
}

QJsonObject jsonRpcError(const QJsonValue& id, const int code, const QString& message)
{
  return QJsonObject{
    {"jsonrpc", "2.0"},
    {"id", id},
    {"error",
     QJsonObject{
       {"code", code},
       {"message", message},
     }},
  };
}

QJsonObject mcpInitializeResult(const QJsonObject& params)
{
  const auto requestedVersion = params.value("protocolVersion").toString();
  const auto protocolVersion =
    requestedVersion.isEmpty() ? QString{ProtocolVersion} : requestedVersion;

  return QJsonObject{
    {"protocolVersion", protocolVersion},
    {"capabilities",
     QJsonObject{
       {"tools", QJsonObject{{"listChanged", false}}},
     }},
    {"serverInfo",
     QJsonObject{
       {"name", ServerName},
       {"title", "TrenchBroom MCP"},
       {"version", ServerVersion},
     }},
    {"instructions",
     "Use structured tools to inspect and edit the running TrenchBroom instance. "
     "The TrenchBroom app must be running and MCP must be enabled in Preferences > "
     "MCP."},
  };
}

QJsonObject mcpToolsListResult(const McpMode currentMode)
{
  return QJsonObject{
    {"tools", toolsListJson(McpMode::Edit)},
    {"trenchBroomMode", modeName(currentMode)},
  };
}

QJsonObject mcpToolCallResult(const QJsonObject& params, const McpToolCaller& toolCaller)
{
  const auto nameValue = params.value("name");
  if (!nameValue.isString() || nameValue.toString().trimmed().isEmpty())
  {
    return textToolResult("tools/call requires a non-empty tool name", true);
  }

  auto arguments = QJsonObject{};
  const auto argumentsValue = params.value("arguments");
  if (!argumentsValue.isUndefined())
  {
    if (!argumentsValue.isObject())
    {
      return textToolResult("tools/call arguments must be an object", true);
    }
    arguments = argumentsValue.toObject();
  }

  const auto bridgeResponse = toolCaller(nameValue.toString(), arguments);
  if (bridgeResponse.ok)
  {
    return textToolResult(
      compactJsonText(bridgeResponse.result), false, bridgeResponse.result);
  }

  const auto error = bridgeResponse.error.value_or(
    McpError{McpErrorCode::InternalError, "Unknown MCP bridge error"});
  const auto structuredError = QJsonObject{
    {"code", errorCodeName(error.code)},
    {"message", error.message},
  };
  return textToolResult(compactJsonText(structuredError), true, structuredError);
}

std::optional<QJsonObject> handleMcpJsonRpcRequest(
  const QJsonObject& request, const McpMode currentMode, const McpToolCaller& toolCaller)
{
  const auto id = request.value("id");
  const auto method = request.value("method").toString();
  const auto paramsValue = request.value("params");
  const auto params = paramsValue.isObject() ? paramsValue.toObject() : QJsonObject{};
  const auto isNotification = request.value("id").isUndefined();

  if (method.isEmpty())
  {
    return jsonRpcError(id, -32600, "Invalid JSON-RPC request");
  }

  if (isNotification)
  {
    return std::nullopt;
  }

  if (method == "initialize")
  {
    return jsonRpcResult(id, mcpInitializeResult(params));
  }

  if (method == "ping")
  {
    return jsonRpcResult(id, {});
  }

  if (method == "tools/list")
  {
    return jsonRpcResult(id, mcpToolsListResult(currentMode));
  }

  if (method == "tools/call")
  {
    return jsonRpcResult(id, mcpToolCallResult(params, toolCaller));
  }

  return jsonRpcError(id, -32601, QString{"Method not found: %1"}.arg(method));
}

} // namespace tb::mcp
