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
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTcpSocket>
#include <QUuid>

#include "ui/mcp/McpBridgeServer.h"
#include "ui/mcp/McpHttpServer.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace mcp = tb::mcp;

namespace
{

constexpr auto TestToken = "secret-token";

struct HttpResponse
{
  int statusCode = 0;
  QByteArray headers;
  QByteArray body;
};

mcp::McpBridgeConfig makeConfig(const mcp::McpMode mode)
{
  return mcp::McpBridgeConfig{
    QString{"trenchbroom-mcp-http-test-%1"}.arg(QUuid::createUuid().toString()),
    TestToken,
    mode,
    true,
    "127.0.0.1",
    0,
  };
}

McpBridgeServer makeBridgeServer()
{
  return McpBridgeServer{[](const QString& toolName, const QJsonObject&) {
    if (toolName == "tb_status")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"application", "TrenchBroom"},
      });
    }
    if (toolName == "entity_create")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-1"},
      });
    }

    return McpBridgeToolResult::success(QJsonObject{
      {"tool", toolName},
    });
  }};
}

QByteArray makeHttpRequest(
  const QByteArray& method,
  const QByteArray& path,
  const QByteArray& body = {},
  const QByteArray& token = TestToken,
  const QByteArray& origin = {})
{
  auto request = QByteArray{};
  request += method + " " + path + " HTTP/1.1\r\n";
  request += "Host: 127.0.0.1\r\n";
  request += "Connection: close\r\n";
  if (!token.isEmpty())
  {
    request += "Authorization: Bearer " + token + "\r\n";
  }
  if (!origin.isEmpty())
  {
    request += "Origin: " + origin + "\r\n";
  }
  if (!body.isEmpty())
  {
    request += "Content-Type: application/json\r\n";
  }
  request += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
  request += "\r\n";
  request += body;
  return request;
}

HttpResponse readResponse(QTcpSocket& socket, const int timeoutMs = 2000)
{
  auto bytes = QByteArray{};
  auto timer = QElapsedTimer{};
  timer.start();
  while (timer.elapsed() < timeoutMs
         && socket.state() != QAbstractSocket::UnconnectedState)
  {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (socket.waitForReadyRead(10))
    {
      bytes += socket.readAll();
    }
    if (bytes.contains("Content-Type: text/event-stream") && bytes.contains("\r\n\r\n"))
    {
      break;
    }
  }
  bytes += socket.readAll();

  const auto headerEnd = bytes.indexOf("\r\n\r\n");
  REQUIRE(headerEnd >= 0);
  const auto statusLine = bytes.left(bytes.indexOf("\r\n"));
  const auto statusParts = statusLine.split(' ');
  REQUIRE(statusParts.size() >= 2);

  auto ok = false;
  const auto statusCode = statusParts[1].toInt(&ok);
  REQUIRE(ok);

  return HttpResponse{statusCode, bytes.left(headerEnd), bytes.mid(headerEnd + 4)};
}

HttpResponse sendRequest(const quint16 port, const QByteArray& request)
{
  auto socket = QTcpSocket{};
  socket.connectToHost("127.0.0.1", port);
  REQUIRE(socket.waitForConnected(1000));
  REQUIRE(socket.write(request) == request.size());
  REQUIRE(socket.waitForBytesWritten(1000));
  return readResponse(socket);
}

QJsonObject jsonObject(const HttpResponse& response)
{
  auto parseError = QJsonParseError{};
  const auto document = QJsonDocument::fromJson(response.body, &parseError);
  REQUIRE(parseError.error == QJsonParseError::NoError);
  REQUIRE(document.isObject());
  return document.object();
}

QStringList toolNames(const QJsonArray& tools)
{
  auto result = QStringList{};
  for (const auto& tool : tools)
  {
    result.push_back(tool.toObject().value("name").toString());
  }
  return result;
}

QByteArray jsonRequest(
  const int id, const QString& method, const QJsonObject& params = {})
{
  auto request = QJsonObject{
    {"jsonrpc", "2.0"},
    {"id", id},
    {"method", method},
  };
  if (!params.isEmpty())
  {
    request.insert("params", params);
  }
  return QJsonDocument{request}.toJson(QJsonDocument::Compact);
}

} // namespace

TEST_CASE("McpHttpServer", "[McpHttpServer]")
{
  SECTION("uses bounded production defaults")
  {
    const auto limits = McpHttpServerLimits{};
    CHECK(limits.maxHeaderBytes == 64 * 1024);
    CHECK(limits.maxBodyBytes == 4 * 1024 * 1024);
    CHECK(limits.incompleteRequestTimeoutMs == 10'000);
    CHECK(limits.maxOrdinaryConnections == 32);
    CHECK(limits.maxSseConnections == 8);
  }

  SECTION("accepts a valid fragmented request")
  {
    auto bridgeServer = makeBridgeServer();
    auto httpServer =
      McpHttpServer{bridgeServer, McpHttpServerLimits{512, 512, 10'000, 32, 8}};
    const auto config = makeConfig(mcp::McpMode::ReadOnly);
    REQUIRE(bridgeServer.start(config));
    REQUIRE(httpServer.start(config));

    const auto request = makeHttpRequest("POST", "/mcp", jsonRequest(20, "initialize"));
    auto socket = QTcpSocket{};
    socket.connectToHost("127.0.0.1", httpServer.port());
    REQUIRE(socket.waitForConnected(1000));
    const auto split = request.size() / 2;
    REQUIRE(socket.write(request.left(split)) == split);
    REQUIRE(socket.waitForBytesWritten(1000));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    CHECK(socket.bytesAvailable() == 0);
    REQUIRE(socket.write(request.mid(split)) == request.size() - split);
    REQUIRE(socket.waitForBytesWritten(1000));
    CHECK(readResponse(socket).statusCode == 200);
  }

  SECTION("rejects a header beyond the configured budget")
  {
    auto bridgeServer = makeBridgeServer();
    auto httpServer =
      McpHttpServer{bridgeServer, McpHttpServerLimits{96, 512, 10'000, 32, 8}};
    const auto config = makeConfig(mcp::McpMode::ReadOnly);
    REQUIRE(bridgeServer.start(config));
    REQUIRE(httpServer.start(config));
    auto request = makeHttpRequest("POST", "/mcp", jsonRequest(21, "initialize"));
    request.replace(
      "Connection: close\r\n",
      "Connection: close\r\nX-Fill: " + QByteArray(128, 'x') + "\r\n");
    CHECK(sendRequest(httpServer.port(), request).statusCode == 431);
  }

  SECTION("rejects a body length beyond the configured budget")
  {
    auto bridgeServer = makeBridgeServer();
    auto httpServer =
      McpHttpServer{bridgeServer, McpHttpServerLimits{512, 32, 10'000, 32, 8}};
    const auto config = makeConfig(mcp::McpMode::ReadOnly);
    REQUIRE(bridgeServer.start(config));
    REQUIRE(httpServer.start(config));
    auto request = makeHttpRequest("POST", "/mcp", {});
    request.replace("Content-Length: 0", "Content-Length: 33");
    CHECK(sendRequest(httpServer.port(), request).statusCode == 413);
  }

  SECTION("rejects duplicate content length fields")
  {
    auto bridgeServer = makeBridgeServer();
    auto httpServer = McpHttpServer{bridgeServer};
    const auto config = makeConfig(mcp::McpMode::ReadOnly);
    REQUIRE(bridgeServer.start(config));
    REQUIRE(httpServer.start(config));
    auto request = makeHttpRequest("POST", "/mcp", {});
    request.replace(
      "Content-Length: 0\r\n", "Content-Length: 0\r\nContent-Length: 0\r\n");
    CHECK(sendRequest(httpServer.port(), request).statusCode == 400);
  }

  SECTION("rejects unsupported transfer encoding")
  {
    auto bridgeServer = makeBridgeServer();
    auto httpServer = McpHttpServer{bridgeServer};
    const auto config = makeConfig(mcp::McpMode::ReadOnly);
    REQUIRE(bridgeServer.start(config));
    REQUIRE(httpServer.start(config));
    auto request = makeHttpRequest("POST", "/mcp", {});
    request.replace(
      "Content-Length: 0\r\n", "Transfer-Encoding: chunked\r\nContent-Length: 0\r\n");
    CHECK(sendRequest(httpServer.port(), request).statusCode == 400);
  }

  SECTION("off mode does not listen")
  {
    auto bridgeServer = makeBridgeServer();
    auto httpServer = McpHttpServer{bridgeServer};
    auto error = QString{};
    const auto config = makeConfig(mcp::McpMode::Off);

    CHECK(bridgeServer.start(config, &error));
    CHECK(httpServer.start(config, &error));
    CHECK(!httpServer.isListening());
  }

  SECTION("post initialize returns JSON-RPC response")
  {
    auto bridgeServer = makeBridgeServer();
    auto httpServer = McpHttpServer{bridgeServer};
    const auto config = makeConfig(mcp::McpMode::ReadOnly);
    REQUIRE(bridgeServer.start(config));
    REQUIRE(httpServer.start(config));

    const auto response = sendRequest(
      httpServer.port(), makeHttpRequest("POST", "/mcp", jsonRequest(1, "initialize")));

    CHECK(response.statusCode == 200);
    const auto json = jsonObject(response);
    CHECK(
      json.value("result").toObject().value("capabilities").toObject().contains("tools"));
  }

  SECTION("tools list is visible")
  {
    auto bridgeServer = makeBridgeServer();
    auto httpServer = McpHttpServer{bridgeServer};
    const auto config = makeConfig(mcp::McpMode::ReadOnly);
    REQUIRE(bridgeServer.start(config));
    REQUIRE(httpServer.start(config));

    const auto response = sendRequest(
      httpServer.port(), makeHttpRequest("POST", "/mcp", jsonRequest(2, "tools/list")));

    CHECK(response.statusCode == 200);
    const auto result = jsonObject(response).value("result").toObject();
    const auto names = toolNames(result.value("tools").toArray());
    CHECK(!names.isEmpty());
    CHECK(result.value("trenchBroomMode").toString() == "ReadOnly");
    CHECK(names.contains("tb_status"));
    CHECK(!names.contains("entity_create_checked"));
    CHECK(!names.contains("blockout_create_batch"));
  }

  SECTION("notification returns accepted")
  {
    auto bridgeServer = makeBridgeServer();
    auto httpServer = McpHttpServer{bridgeServer};
    const auto config = makeConfig(mcp::McpMode::ReadOnly);
    REQUIRE(bridgeServer.start(config));
    REQUIRE(httpServer.start(config));

    const auto body = QJsonDocument{
      QJsonObject{
        {"jsonrpc", "2.0"},
        {"method", "notifications/initialized"},
      }}.toJson(QJsonDocument::Compact);
    const auto response =
      sendRequest(httpServer.port(), makeHttpRequest("POST", "/mcp", body));

    CHECK(response.statusCode == 202);
    CHECK(response.body.isEmpty());
  }

  SECTION("generic notification returns accepted")
  {
    auto bridgeServer = makeBridgeServer();
    auto httpServer = McpHttpServer{bridgeServer};
    const auto config = makeConfig(mcp::McpMode::ReadOnly);
    REQUIRE(bridgeServer.start(config));
    REQUIRE(httpServer.start(config));

    const auto body = QJsonDocument{
      QJsonObject{
        {"jsonrpc", "2.0"},
        {"method", "notifications/cancelled"},
      }}.toJson(QJsonDocument::Compact);
    const auto response =
      sendRequest(httpServer.port(), makeHttpRequest("POST", "/mcp", body));

    CHECK(response.statusCode == 202);
    CHECK(response.body.isEmpty());
  }

  SECTION("get opens SSE stream")
  {
    auto bridgeServer = makeBridgeServer();
    auto httpServer = McpHttpServer{bridgeServer};
    const auto config = makeConfig(mcp::McpMode::ReadOnly);
    REQUIRE(bridgeServer.start(config));
    REQUIRE(httpServer.start(config));

    const auto response = sendRequest(httpServer.port(), makeHttpRequest("GET", "/mcp"));

    CHECK(response.statusCode == 200);
    CHECK(response.headers.contains("Content-Type: text/event-stream"));
    CHECK(response.body.contains("TrenchBroom MCP stream ready"));
  }

  SECTION("get rejects requests without authorization")
  {
    auto bridgeServer = makeBridgeServer();
    auto httpServer = McpHttpServer{bridgeServer};
    const auto config = makeConfig(mcp::McpMode::ReadOnly);
    REQUIRE(bridgeServer.start(config));
    REQUIRE(httpServer.start(config));

    const auto response =
      sendRequest(httpServer.port(), makeHttpRequest("GET", "/mcp", {}, {}));

    CHECK(response.statusCode == 401);
    CHECK(response.headers.contains("WWW-Authenticate: Bearer"));
  }

  SECTION("localhost requests require authorization header")
  {
    auto bridgeServer = makeBridgeServer();
    auto httpServer = McpHttpServer{bridgeServer};
    const auto config = makeConfig(mcp::McpMode::ReadOnly);
    REQUIRE(bridgeServer.start(config));
    REQUIRE(httpServer.start(config));

    const auto response = sendRequest(
      httpServer.port(),
      makeHttpRequest("POST", "/mcp", jsonRequest(3, "tools/list"), {}));

    CHECK(response.statusCode == 401);
    CHECK(response.headers.contains("WWW-Authenticate: Bearer"));
  }

  SECTION("localhost requests reject an invalid bearer token")
  {
    auto bridgeServer = makeBridgeServer();
    auto httpServer = McpHttpServer{bridgeServer};
    const auto config = makeConfig(mcp::McpMode::ReadOnly);
    REQUIRE(bridgeServer.start(config));
    REQUIRE(httpServer.start(config));

    const auto response = sendRequest(
      httpServer.port(),
      makeHttpRequest("POST", "/mcp", jsonRequest(3, "tools/list"), "wrong-token"));

    CHECK(response.statusCode == 401);
  }

  SECTION("cors preflight echoes an allowed origin")
  {
    auto bridgeServer = makeBridgeServer();
    auto httpServer = McpHttpServer{bridgeServer};
    const auto config = makeConfig(mcp::McpMode::ReadOnly);
    REQUIRE(bridgeServer.start(config));
    REQUIRE(httpServer.start(config));

    const auto response = sendRequest(
      httpServer.port(),
      makeHttpRequest("OPTIONS", "/mcp", {}, {}, "http://localhost:3000"));

    CHECK(response.statusCode == 204);
    CHECK(
      response.headers.contains("Access-Control-Allow-Origin: http://localhost:3000"));
    CHECK(response.headers.contains("Access-Control-Allow-Headers: Authorization"));
  }

  SECTION("cors rejects non-loopback origins")
  {
    auto bridgeServer = makeBridgeServer();
    auto httpServer = McpHttpServer{bridgeServer};
    const auto config = makeConfig(mcp::McpMode::ReadOnly);
    REQUIRE(bridgeServer.start(config));
    REQUIRE(httpServer.start(config));

    const auto response = sendRequest(
      httpServer.port(),
      makeHttpRequest(
        "POST", "/mcp", jsonRequest(3, "tools/list"), TestToken, "https://example.com"));

    CHECK(response.statusCode == 403);
    CHECK_FALSE(response.headers.contains("Access-Control-Allow-Origin"));
  }

  SECTION("reports an active HTTP instance without replacing it")
  {
    auto firstBridgeServer = makeBridgeServer();
    auto firstHttpServer = McpHttpServer{firstBridgeServer};
    auto firstConfig = makeConfig(mcp::McpMode::ReadOnly);
    REQUIRE(firstBridgeServer.start(firstConfig));
    REQUIRE(firstHttpServer.start(firstConfig));

    auto secondBridgeServer = makeBridgeServer();
    auto secondHttpServer = McpHttpServer{secondBridgeServer};
    auto secondConfig = makeConfig(mcp::McpMode::ReadOnly);
    secondConfig.httpPort = firstHttpServer.port();
    auto error = QString{};

    CHECK_FALSE(secondHttpServer.start(secondConfig, &error));
    CHECK(error.contains("already listening"));
    CHECK(firstHttpServer.isListening());
  }

  SECTION("read-only mode rejects edit tools")
  {
    auto bridgeServer = makeBridgeServer();
    auto httpServer = McpHttpServer{bridgeServer};
    const auto config = makeConfig(mcp::McpMode::ReadOnly);
    REQUIRE(bridgeServer.start(config));
    REQUIRE(httpServer.start(config));

    const auto response = sendRequest(
      httpServer.port(),
      makeHttpRequest(
        "POST",
        "/mcp",
        jsonRequest(
          4,
          "tools/call",
          QJsonObject{
            {"name", "entity_create"},
            {"arguments", QJsonObject{{"classname", "info_player_start"}}},
          })));

    CHECK(response.statusCode == 200);
    const auto result = jsonObject(response).value("result").toObject();
    CHECK(result.value("isError").toBool());
    CHECK(
      result.value("structuredContent").toObject().value("code").toString()
      == "Forbidden");
  }
}

} // namespace tb::ui
