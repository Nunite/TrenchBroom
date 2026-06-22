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

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalSocket>
#include <QTextStream>
#include <QUuid>

#include "mcp/McpBridgeConfig.h"
#include "mcp/McpBridgeMessages.h"
#include "mcp/McpError.h"
#include "mcp/McpToolCatalog.h"

namespace tb::mcp
{
namespace
{

constexpr auto ProtocolVersion = "2025-06-18";
constexpr auto ServerName = "trenchbroom-mcp";
constexpr auto ServerVersion = "0.1.0";
constexpr auto BridgeTimeoutMs = 5000;

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

QJsonObject initializeResult(const QJsonObject& params)
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
     "The TrenchBroom app must be running and MCP must be enabled in its local "
     "config."},
  };
}

std::optional<McpBridgeConfig> loadConfig(QString* error)
{
  return readOrCreateBridgeConfig(defaultConfigPath(), error);
}

QJsonObject toolsListResult()
{
  auto error = QString{};
  const auto config = loadConfig(&error);
  if (!config)
  {
    return QJsonObject{
      {"tools", QJsonArray{}},
      {"configError", error},
    };
  }

  return QJsonObject{
    {"tools", toolsListJson(config->mode)},
  };
}

McpBridgeResponse callBridge(const QString& toolName, const QJsonObject& arguments)
{
  auto error = QString{};
  const auto config = loadConfig(&error);
  if (!config)
  {
    return McpBridgeResponse::failure(
      {},
      McpError{
        McpErrorCode::InternalError,
        QString{"Could not load MCP config: %1"}.arg(error)});
  }

  if (config->mode == McpMode::Off)
  {
    return McpBridgeResponse::failure(
      {}, McpError{McpErrorCode::Forbidden, "TrenchBroom MCP bridge is disabled"});
  }

  auto socket = QLocalSocket{};
  socket.connectToServer(config->pipeName);
  if (!socket.waitForConnected(BridgeTimeoutMs))
  {
    return McpBridgeResponse::failure(
      {},
      McpError{
        McpErrorCode::InternalError,
        QString{"Could not connect to TrenchBroom MCP bridge '%1': %2"}.arg(
          config->pipeName, socket.errorString())});
  }

  const auto request = McpBridgeRequest{
    QUuid::createUuid().toString(QUuid::WithoutBraces),
    config->token,
    toolName,
    arguments,
    config->mode,
  };

  socket.write(QJsonDocument{toJson(request)}.toJson(QJsonDocument::Compact));
  socket.write("\n");
  if (!socket.waitForBytesWritten(BridgeTimeoutMs))
  {
    return McpBridgeResponse::failure(
      {},
      McpError{
        McpErrorCode::InternalError,
        QString{"Could not write MCP bridge request: %1"}.arg(socket.errorString())});
  }

  if (!socket.waitForReadyRead(BridgeTimeoutMs))
  {
    return McpBridgeResponse::failure(
      {},
      McpError{
        McpErrorCode::InternalError,
        QString{"Timed out waiting for MCP bridge response: %1"}.arg(
          socket.errorString())});
  }

  auto parseError = QJsonParseError{};
  const auto document = QJsonDocument::fromJson(socket.readLine().trimmed(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
  {
    return McpBridgeResponse::failure(
      {},
      McpError{McpErrorCode::InvalidRequest, "Invalid JSON response from MCP bridge"});
  }

  auto parseMessage = QString{};
  const auto response = bridgeResponseFromJson(document.object(), &parseMessage);
  if (!response)
  {
    return McpBridgeResponse::failure(
      {},
      McpError{
        McpErrorCode::InvalidRequest,
        QString{"Invalid MCP bridge response: %1"}.arg(parseMessage)});
  }

  return *response;
}

QJsonObject toolCallResult(const QJsonObject& params)
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

  const auto bridgeResponse = callBridge(nameValue.toString(), arguments);
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

std::optional<QJsonObject> handleRequest(const QJsonObject& request)
{
  const auto id = request.value("id");
  const auto method = request.value("method").toString();
  const auto paramsValue = request.value("params");
  const auto params = paramsValue.isObject() ? paramsValue.toObject() : QJsonObject{};

  if (method.isEmpty())
  {
    return jsonRpcError(id, -32600, "Invalid JSON-RPC request");
  }

  if (method == "notifications/initialized")
  {
    return std::nullopt;
  }

  if (method == "initialize")
  {
    return jsonRpcResult(id, initializeResult(params));
  }

  if (method == "ping")
  {
    return jsonRpcResult(id, {});
  }

  if (method == "tools/list")
  {
    return jsonRpcResult(id, toolsListResult());
  }

  if (method == "tools/call")
  {
    return jsonRpcResult(id, toolCallResult(params));
  }

  return jsonRpcError(id, -32601, QString{"Method not found: %1"}.arg(method));
}

void writeJsonLine(QTextStream& out, const QJsonObject& json)
{
  out << QString::fromUtf8(QJsonDocument{json}.toJson(QJsonDocument::Compact))
      << Qt::endl;
  out.flush();
}

int runServer()
{
  auto in = QTextStream{stdin};
  auto out = QTextStream{stdout};

  while (!in.atEnd())
  {
    const auto line = in.readLine();
    if (line.trimmed().isEmpty())
    {
      continue;
    }

    auto parseError = QJsonParseError{};
    const auto request = QJsonDocument::fromJson(line.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !request.isObject())
    {
      writeJsonLine(out, jsonRpcError({}, -32700, "Parse error"));
      continue;
    }

    const auto response = handleRequest(request.object());
    if (response)
    {
      writeJsonLine(out, *response);
    }
  }

  return 0;
}

} // namespace
} // namespace tb::mcp

int main(int argc, char* argv[])
{
  auto app = QCoreApplication{argc, argv};
  QCoreApplication::setApplicationName("trenchbroom-mcp");

  return tb::mcp::runServer();
}
