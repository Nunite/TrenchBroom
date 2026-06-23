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
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTextStream>
#include <QUuid>

#include "mcp/McpBridgeConfig.h"
#include "mcp/McpBridgeMessages.h"
#include "mcp/McpError.h"
#include "mcp/McpJsonRpc.h"

namespace tb::mcp
{
namespace
{

constexpr auto BridgeTimeoutMs = 5000;

std::optional<McpBridgeConfig> loadConfig(QString* error)
{
  return readOrCreateBridgeConfig(defaultConfigPath(), error);
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

std::optional<QJsonObject> handleRequest(const QJsonObject& request)
{
  auto error = QString{};
  const auto config = loadConfig(&error);
  if (!config)
  {
    return jsonRpcError(
      request.value("id"), -32603, QString{"Could not load MCP config: %1"}.arg(error));
  }

  return handleMcpJsonRpcRequest(request, config->mode, callBridge);
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
