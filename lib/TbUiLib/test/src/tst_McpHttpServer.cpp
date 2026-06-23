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
  const QString& token = TestToken)
{
  auto request = QByteArray{};
  request += method + " " + path + " HTTP/1.1\r\n";
  request += "Host: 127.0.0.1\r\n";
  request += "Connection: close\r\n";
  if (!token.isNull())
  {
    request += "Authorization: Bearer " + token.toUtf8() + "\r\n";
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

HttpResponse sendRequest(const quint16 port, const QByteArray& request)
{
  auto socket = QTcpSocket{};
  socket.connectToHost("127.0.0.1", port);
  REQUIRE(socket.waitForConnected(1000));
  REQUIRE(socket.write(request) == request.size());
  REQUIRE(socket.waitForBytesWritten(1000));

  auto bytes = QByteArray{};
  auto timer = QElapsedTimer{};
  timer.start();
  while (timer.elapsed() < 2000 && socket.state() != QAbstractSocket::UnconnectedState)
  {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (socket.waitForReadyRead(10))
    {
      bytes += socket.readAll();
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

  return HttpResponse{statusCode, bytes.mid(headerEnd + 4)};
}

QJsonObject jsonObject(const HttpResponse& response)
{
  auto parseError = QJsonParseError{};
  const auto document = QJsonDocument::fromJson(response.body, &parseError);
  REQUIRE(parseError.error == QJsonParseError::NoError);
  REQUIRE(document.isObject());
  return document.object();
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

TEST_CASE("McpHttpServer")
{
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
    CHECK(!result.value("tools").toArray().isEmpty());
    CHECK(result.value("trenchBroomMode").toString() == "ReadOnly");
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

  SECTION("get is rejected")
  {
    auto bridgeServer = makeBridgeServer();
    auto httpServer = McpHttpServer{bridgeServer};
    const auto config = makeConfig(mcp::McpMode::ReadOnly);
    REQUIRE(bridgeServer.start(config));
    REQUIRE(httpServer.start(config));

    const auto response = sendRequest(httpServer.port(), makeHttpRequest("GET", "/mcp"));

    CHECK(response.statusCode == 405);
  }

  SECTION("wrong token is unauthorized")
  {
    auto bridgeServer = makeBridgeServer();
    auto httpServer = McpHttpServer{bridgeServer};
    const auto config = makeConfig(mcp::McpMode::ReadOnly);
    REQUIRE(bridgeServer.start(config));
    REQUIRE(httpServer.start(config));

    const auto response = sendRequest(
      httpServer.port(),
      makeHttpRequest("POST", "/mcp", jsonRequest(3, "tools/list"), "wrong"));

    CHECK(response.statusCode == 401);
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
