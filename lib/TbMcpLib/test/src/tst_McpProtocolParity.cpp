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

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include "mcp/McpBridgeClient.h"
#include "mcp/McpJsonRpc.h"

#include <functional>
#include <memory>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace tb::mcp
{
namespace
{

McpBridgeResponse protocolBridgeResponse(const McpBridgeRequest& request)
{
  switch (request.type)
  {
  case McpBridgeRequestType::ToolCall:
    if (request.tool == "tb_status")
    {
      return McpBridgeResponse::success(
        request.id, QJsonObject{{"application", "TrenchBroom"}});
    }
    return McpBridgeResponse::failure(
      request.id, McpError{McpErrorCode::ToolNotFound, "Unknown tool"});
  case McpBridgeRequestType::ResourcesList:
    return McpBridgeResponse::success(
      request.id,
      QJsonObject{{
        "resources",
        QJsonArray{QJsonObject{
          {"uri", "tbmcp://review/review-1"},
          {"name", "review-1"},
          {"mimeType", "application/json"},
          {"type", "review"},
        }},
      }});
  case McpBridgeRequestType::ResourceRead:
    if (request.params.value("uri").toString() == "tbmcp://review/review-1")
    {
      return McpBridgeResponse::success(
        request.id, QJsonObject{{"reviewId", "review-1"}});
    }
    return McpBridgeResponse::failure(
      request.id, McpError{McpErrorCode::InvalidParams, "Resource not found"});
  }
  return McpBridgeResponse::failure(
    request.id, McpError{McpErrorCode::InvalidRequest, "Unknown request"});
}

class ProtocolConnection : public McpBridgeConnection
{
private:
  QByteArray m_response;

public:
  void connectToServer(const QString&) override {}
  bool waitForConnected(int) override { return true; }
  qint64 write(const QByteArray& data) override
  {
    auto parseError = QJsonParseError{};
    const auto document = QJsonDocument::fromJson(data.trimmed(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
      return -1;
    }
    const auto request = bridgeRequestFromJson(document.object());
    if (!request)
    {
      return -1;
    }
    m_response = QJsonDocument{toJson(protocolBridgeResponse(*request))}.toJson(
                   QJsonDocument::Compact)
                 + '\n';
    return data.size();
  }
  bool waitForBytesWritten(int) override { return true; }
  bool canReadLine() const override { return !m_response.isEmpty(); }
  bool waitForReadyRead(int) override { return !m_response.isEmpty(); }
  QByteArray readLine() override { return std::exchange(m_response, QByteArray{}); }
  QString errorString() const override { return "protocol test connection error"; }
};

using ProtocolAdapter =
  std::function<std::optional<QJsonObject>(const QJsonObject& request)>;

QJsonObject request(const int id, const QString& method, const QJsonObject& params = {})
{
  return QJsonObject{
    {"jsonrpc", "2.0"},
    {"id", id},
    {"method", method},
    {"params", params},
  };
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

void runProtocolContract(const ProtocolAdapter& adapter)
{
  const auto initialize = adapter(request(1, "initialize"));
  REQUIRE(initialize);
  CHECK(initialize->value("result")
          .toObject()
          .value("capabilities")
          .toObject()
          .contains("resources"));

  const auto tools = adapter(request(2, "tools/list"));
  REQUIRE(tools);
  const auto toolsResult = tools->value("result").toObject();
  CHECK(toolsResult.value("toolProfile").toString() == "Core");
  const auto names = toolNames(toolsResult.value("tools").toArray());
  CHECK(names.contains("tb_status"));
  CHECK_FALSE(names.contains("documents_open"));

  const auto called = adapter(request(
    3, "tools/call", QJsonObject{{"name", "tb_status"}, {"arguments", QJsonObject{}}}));
  REQUIRE(called);
  CHECK_FALSE(called->value("result").toObject().value("isError").toBool());

  const auto listed = adapter(request(4, "resources/list"));
  REQUIRE(listed);
  const auto resources = listed->value("result").toObject().value("resources").toArray();
  REQUIRE(resources.size() == 1);
  CHECK(
    resources.first().toObject().value("uri").toString() == "tbmcp://review/review-1");

  const auto read = adapter(
    request(5, "resources/read", QJsonObject{{"uri", "tbmcp://review/review-1"}}));
  REQUIRE(read);
  const auto contents = read->value("result").toObject().value("contents").toArray();
  REQUIRE(contents.size() == 1);
  CHECK(contents.first().toObject().value("text").toString().contains("review-1"));

  const auto unknown = adapter(request(6, "unknown/method"));
  REQUIRE(unknown);
  CHECK(unknown->value("error").toObject().value("code").toInt() == -32601);

  const auto notification = adapter(QJsonObject{
    {"jsonrpc", "2.0"},
    {"method", "notifications/cancelled"},
  });
  CHECK_FALSE(notification);
}

} // namespace

TEST_CASE(
  "MCP HTTP and stdio adapters share one protocol contract", "[McpProtocolParity]")
{
  auto config = McpBridgeConfig{"test-pipe", McpMode::ReadOnly, true, "127.0.0.1", 0};
  config.toolProfile = McpToolProfile::Core;

  SECTION("direct protocol adapter")
  {
    runProtocolContract([&](const QJsonObject& json) {
      return handleMcpJsonRpcRequest(
        json,
        config.mode,
        [](const McpBridgeRequestType type, const QString& tool, QJsonObject params) {
          return protocolBridgeResponse(McpBridgeRequest{
            "direct",
            tool,
            std::move(params),
            McpMode::ReadOnly,
            type,
          });
        },
        config.toolProfile);
    });
  }

  SECTION("stdio adapter")
  {
    const auto client = McpBridgeClient{
      [] { return std::make_unique<ProtocolConnection>(); }, McpBridgeClientTimeouts{}};
    runProtocolContract([&](const QJsonObject& json) {
      return handleMcpJsonRpcRequest(
        json,
        config.mode,
        [&](const McpBridgeRequestType type, const QString& tool, QJsonObject params) {
          return client.request(config, type, tool, std::move(params));
        },
        config.toolProfile);
    });
  }
}

} // namespace tb::mcp
