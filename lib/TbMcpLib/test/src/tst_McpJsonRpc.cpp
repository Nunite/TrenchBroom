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

#include <QJsonArray>
#include <QJsonObject>

#include "mcp/McpJsonRpc.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::mcp
{

TEST_CASE("McpJsonRpc")
{
  SECTION("initialize declares tools capability")
  {
    const auto result = mcpInitializeResult({});
    const auto capabilities = result.value("capabilities").toObject();
    CHECK(capabilities.contains("tools"));
    CHECK(capabilities.contains("resources"));
  }

  SECTION("tools list remains visible when TrenchBroom mode is off")
  {
    const auto result = mcpToolsListResult(McpMode::Off);
    const auto tools = result.value("tools").toArray();

    CHECK(result.value("trenchBroomMode").toString() == "Off");
    CHECK(result.value("toolProfile").toString() == "Modeling");
    CHECK(!tools.isEmpty());
    CHECK(tools.first().toObject().value("name").toString() == "tb_status");
  }

  SECTION("tools call forwards arguments to caller")
  {
    auto called = false;
    const auto result = mcpToolCallResult(
      QJsonObject{
        {"name", "tb_status"},
        {"arguments", QJsonObject{{"hello", "world"}}},
      },
      [&](const QString& toolName, const QJsonObject& arguments) {
        called = true;
        CHECK(toolName == "tb_status");
        CHECK(arguments.value("hello").toString() == "world");
        return McpBridgeResponse::success("request", QJsonObject{{"ok", true}});
      });

    CHECK(called);
    CHECK(!result.value("isError").toBool());
    CHECK(result.value("structuredContent").toObject().value("ok").toBool());
  }

  SECTION("tools call includes operation resource link")
  {
    const auto result = mcpToolCallResult(
      QJsonObject{
        {"name", "blockout_create_batch"},
        {"arguments", QJsonObject{}},
      },
      [](const QString&, const QJsonObject&) {
        return McpBridgeResponse::success(
          "request",
          QJsonObject{
            {"operationId", "mcp-op-1"},
            {"transactionName", "MCP: Test"},
            {"brushCount", 2},
            {"resourceUri", "tbmcp://operation/mcp-op-1"},
          });
      });

    CHECK(!result.value("isError").toBool());
    const auto content = result.value("content").toArray();
    REQUIRE(content.size() == 2);
    CHECK(content[0].toObject().value("type").toString() == "text");
    CHECK(content[0].toObject().value("text").toString().contains("brushCount=2"));
    CHECK(content[1].toObject().value("type").toString() == "resource_link");
    CHECK(content[1].toObject().value("uri").toString() == "tbmcp://operation/mcp-op-1");
  }

  SECTION("notification does not produce a response")
  {
    const auto response = handleMcpJsonRpcRequest(
      QJsonObject{
        {"jsonrpc", "2.0"},
        {"method", "notifications/cancelled"},
      },
      McpMode::ReadOnly,
      [](const QString&, const QJsonObject&) {
        return McpBridgeResponse::success("request", {});
      });

    CHECK(!response);
  }

  SECTION("resources read returns JSON resource contents")
  {
    const auto response = handleMcpJsonRpcRequest(
      QJsonObject{
        {"jsonrpc", "2.0"},
        {"id", 5},
        {"method", "resources/read"},
        {"params", QJsonObject{{"uri", "tbmcp://operation/mcp-op-1"}}},
      },
      McpMode::ReadOnly,
      [](const QString&, const QJsonObject&) {
        return McpBridgeResponse::success("request", {});
      },
      McpToolProfile::Modeling,
      [](const QString& uri) -> std::optional<QJsonObject> {
        if (uri == "tbmcp://operation/mcp-op-1")
        {
          return QJsonObject{{"operationId", "mcp-op-1"}};
        }
        return std::nullopt;
      });

    REQUIRE(response);
    const auto result = response->value("result").toObject();
    const auto contents = result.value("contents").toArray();
    REQUIRE(contents.size() == 1);
    CHECK(contents[0].toObject().value("uri").toString() == "tbmcp://operation/mcp-op-1");
    CHECK(contents[0].toObject().value("mimeType").toString() == "application/json");
    CHECK(contents[0].toObject().value("text").toString().contains("mcp-op-1"));
  }
}

} // namespace tb::mcp
