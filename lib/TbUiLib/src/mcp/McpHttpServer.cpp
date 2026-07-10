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

#include "ui/mcp/McpHttpServer.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include "mcp/McpJsonRpc.h"

#include <algorithm>
#include <optional>

namespace tb::ui
{
namespace
{

constexpr auto EndpointPath = "/mcp";
constexpr auto MaxHeaderBytes = 64 * 1024;
constexpr auto MaxBodyBytes = 4 * 1024 * 1024;

QByteArray reasonPhrase(const int statusCode)
{
  switch (statusCode)
  {
  case 200:
    return "OK";
  case 204:
    return "No Content";
  case 202:
    return "Accepted";
  case 400:
    return "Bad Request";
  case 401:
    return "Unauthorized";
  case 403:
    return "Forbidden";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  default:
    return "Internal Server Error";
  }
}

QByteArray jsonBody(const QString& message)
{
  return QJsonDocument{QJsonObject{{"error", message}}}.toJson(QJsonDocument::Compact);
}

QString headerValue(const QList<QByteArray>& lines, const QByteArray& key)
{
  const auto lowerKey = key.toLower() + ":";
  for (int i = 1; i < lines.size(); ++i)
  {
    const auto line = lines[i];
    if (line.toLower().startsWith(lowerKey))
    {
      return QString::fromUtf8(line.mid(lowerKey.size()).trimmed());
    }
  }
  return {};
}

std::optional<QByteArray> allowedOrigin(const QList<QByteArray>& lines)
{
  const auto origin = headerValue(lines, "Origin");
  if (origin.isEmpty())
  {
    return QByteArray{};
  }

  const auto url = QUrl{origin};
  if (
    url.host() == "127.0.0.1"
    || url.host().compare("localhost", Qt::CaseInsensitive) == 0)
  {
    return origin.toUtf8();
  }
  return std::nullopt;
}

bool constantTimeEquals(const QByteArray& lhs, const QByteArray& rhs)
{
  const auto count = std::max(lhs.size(), rhs.size());
  auto difference = static_cast<unsigned int>(lhs.size() ^ rhs.size());
  for (auto i = qsizetype{0}; i < count; ++i)
  {
    const auto left = i < lhs.size() ? static_cast<unsigned char>(lhs[i]) : 0u;
    const auto right = i < rhs.size() ? static_cast<unsigned char>(rhs[i]) : 0u;
    difference |= left ^ right;
  }
  return difference == 0u;
}

bool hasValidAuthorization(const QList<QByteArray>& lines, const QString& configuredToken)
{
  const auto authorization = headerValue(lines, "Authorization").toUtf8();
  const auto expected = QByteArray{"Bearer "} + configuredToken.toUtf8();
  return constantTimeEquals(authorization, expected);
}

QString methodFromRequestLine(const QByteArray& requestLine)
{
  return QString::fromUtf8(requestLine.split(' ').value(0));
}

QString pathFromRequestLine(const QByteArray& requestLine)
{
  return QString::fromUtf8(requestLine.split(' ').value(1));
}

} // namespace

McpHttpServer::McpHttpServer(McpBridgeServer& bridgeServer, QObject* parent)
  : QObject{parent}
  , m_bridgeServer{bridgeServer}
{
}

McpHttpServer::~McpHttpServer()
{
  stop();
}

bool McpHttpServer::start(const mcp::McpBridgeConfig& config, QString* error)
{
  stop();
  m_config = config;

  if (m_config.mode == mcp::McpMode::Off || !m_config.httpEnabled)
  {
    return true;
  }

  if (m_config.httpHost != "127.0.0.1" && m_config.httpHost != "localhost")
  {
    if (error)
    {
      *error = "MCP HTTP server only supports localhost in this version";
    }
    return false;
  }

  m_server = std::make_unique<QTcpServer>();
  connect(
    m_server.get(),
    &QTcpServer::newConnection,
    this,
    &McpHttpServer::handleNewConnection);

  if (!m_server->listen(QHostAddress::LocalHost, m_config.httpPort))
  {
    if (error)
    {
      *error = m_server->errorString();
    }
    m_server.reset();
    return false;
  }

  return true;
}

void McpHttpServer::stop()
{
  if (m_server)
  {
    m_server->close();
    m_server.reset();
  }

  const auto sockets = findChildren<QTcpSocket*>();
  for (auto* socket : sockets)
  {
    socket->disconnectFromHost();
    socket->deleteLater();
  }
}

bool McpHttpServer::isListening() const
{
  return m_server != nullptr && m_server->isListening();
}

QString McpHttpServer::url() const
{
  return QString{"http://127.0.0.1:%1%2"}.arg(port()).arg(EndpointPath);
}

quint16 McpHttpServer::port() const
{
  return m_server && m_server->isListening() ? m_server->serverPort() : m_config.httpPort;
}

void McpHttpServer::handleNewConnection()
{
  while (auto* socket = m_server->nextPendingConnection())
  {
    socket->setParent(this);
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
      handleSocketReadyRead(*socket);
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
  }
}

void McpHttpServer::handleSocketReadyRead(QTcpSocket& socket)
{
  const auto requestBytes = socket.peek(MaxHeaderBytes + MaxBodyBytes);
  const auto headerEnd = requestBytes.indexOf("\r\n\r\n");
  if (headerEnd < 0)
  {
    if (requestBytes.size() > MaxHeaderBytes)
    {
      writeHttpResponse(
        socket,
        400,
        reasonPhrase(400),
        "application/json",
        jsonBody("HTTP header too large"));
      socket.disconnectFromHost();
    }
    return;
  }

  const auto headerBytes = requestBytes.left(headerEnd);
  const auto lines = headerBytes.split('\n');
  if (lines.isEmpty())
  {
    writeHttpResponse(
      socket, 400, reasonPhrase(400), "application/json", jsonBody("Empty HTTP request"));
    socket.disconnectFromHost();
    return;
  }

  const auto requestLine = lines.first().trimmed();
  const auto method = methodFromRequestLine(requestLine);
  const auto path = pathFromRequestLine(requestLine);

  const auto contentLengthText = headerValue(lines, "Content-Length");
  auto ok = false;
  const auto contentLength =
    contentLengthText.isEmpty() ? 0 : contentLengthText.toInt(&ok);
  if (
    !contentLengthText.isEmpty()
    && (!ok || contentLength < 0 || contentLength > MaxBodyBytes))
  {
    writeHttpResponse(
      socket,
      400,
      reasonPhrase(400),
      "application/json",
      jsonBody("Invalid Content-Length"));
    socket.disconnectFromHost();
    return;
  }

  const auto totalLength = headerEnd + 4 + contentLength;
  if (requestBytes.size() < totalLength)
  {
    return;
  }

  socket.read(totalLength);

  if (path != EndpointPath)
  {
    writeHttpResponse(
      socket,
      404,
      reasonPhrase(404),
      "application/json",
      jsonBody("Unknown MCP endpoint"));
    socket.disconnectFromHost();
    return;
  }

  const auto responseOrigin = allowedOrigin(lines);
  if (!responseOrigin)
  {
    writeHttpResponse(
      socket, 403, reasonPhrase(403), "application/json", jsonBody("Invalid Origin"));
    socket.disconnectFromHost();
    return;
  }

  if (method == "OPTIONS")
  {
    writeHttpResponse(socket, 204, reasonPhrase(204), {}, {}, *responseOrigin);
    socket.disconnectFromHost();
    return;
  }

  if (!hasValidAuthorization(lines, m_config.token))
  {
    writeHttpResponse(
      socket,
      401,
      reasonPhrase(401),
      "application/json",
      jsonBody("Missing or invalid bearer token"),
      *responseOrigin,
      "WWW-Authenticate: Bearer\r\n");
    socket.disconnectFromHost();
    return;
  }

  if (method == "GET")
  {
    writeSseStream(socket, *responseOrigin);
    return;
  }

  if (method != "POST")
  {
    writeHttpResponse(
      socket,
      405,
      reasonPhrase(405),
      "application/json",
      jsonBody("Method not allowed"),
      *responseOrigin);
    socket.disconnectFromHost();
    return;
  }

  auto parseError = QJsonParseError{};
  const auto body = requestBytes.mid(headerEnd + 4, contentLength);
  const auto document = QJsonDocument::fromJson(body, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
  {
    writeHttpResponse(
      socket,
      400,
      reasonPhrase(400),
      "application/json",
      QJsonDocument{mcp::jsonRpcError({}, -32700, "Parse error")}.toJson(
        QJsonDocument::Compact),
      *responseOrigin);
    socket.disconnectFromHost();
    return;
  }

  const auto response = mcp::handleMcpJsonRpcRequest(
    document.object(),
    m_config.mode,
    [this](const QString& toolName, const QJsonObject& arguments) {
      const auto request = mcp::McpBridgeRequest{
        "http",
        m_config.token,
        toolName,
        arguments,
        m_config.mode,
      };
      return m_bridgeServer.dispatchRequest(request);
    },
    m_config.toolProfile,
    [this](const QString& uri) { return m_bridgeServer.readResource(uri); });

  if (!response)
  {
    writeHttpResponse(socket, 202, reasonPhrase(202), "text/plain", {}, *responseOrigin);
    socket.disconnectFromHost();
    return;
  }

  writeHttpResponse(
    socket,
    200,
    reasonPhrase(200),
    "application/json",
    QJsonDocument{*response}.toJson(QJsonDocument::Compact),
    *responseOrigin);
  socket.disconnectFromHost();
}

void McpHttpServer::writeSseStream(
  QTcpSocket& socket, const QByteArray& allowedOrigin) const
{
  auto response = QByteArray{};
  response += "HTTP/1.1 200 " + reasonPhrase(200) + "\r\n";
  response += "Connection: keep-alive\r\n";
  response += "Cache-Control: no-cache, no-transform\r\n";
  response += "Content-Type: text/event-stream\r\n";
  if (!allowedOrigin.isEmpty())
  {
    response += "Access-Control-Allow-Origin: " + allowedOrigin + "\r\n";
    response += "Vary: Origin\r\n";
  }
  response += "\r\n";
  response += ": TrenchBroom MCP stream ready\r\n\r\n";
  socket.write(response);
  socket.flush();
}

void McpHttpServer::writeHttpResponse(
  QTcpSocket& socket,
  const int statusCode,
  const QByteArray& reason,
  const QByteArray& contentType,
  const QByteArray& body,
  const QByteArray& allowedOrigin,
  const QByteArray& extraHeaders) const
{
  auto response = QByteArray{};
  response += "HTTP/1.1 " + QByteArray::number(statusCode) + " " + reason + "\r\n";
  response += "Connection: close\r\n";
  if (!allowedOrigin.isEmpty())
  {
    response += "Access-Control-Allow-Origin: " + allowedOrigin + "\r\n";
    response += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    response +=
      "Access-Control-Allow-Headers: Authorization, Content-Type, "
      "MCP-Protocol-Version\r\n";
    response += "Vary: Origin\r\n";
  }
  response += extraHeaders;
  if (!contentType.isEmpty())
  {
    response += "Content-Type: " + contentType + "\r\n";
  }
  response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
  response += "\r\n";
  response += body;
  socket.write(response);
  socket.flush();
}

} // namespace tb::ui
