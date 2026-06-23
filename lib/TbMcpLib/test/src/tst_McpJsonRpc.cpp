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
    CHECK(result.value("capabilities").toObject().contains("tools"));
  }

  SECTION("tools list remains visible when TrenchBroom mode is off")
  {
    const auto result = mcpToolsListResult(McpMode::Off);
    const auto tools = result.value("tools").toArray();

    CHECK(result.value("trenchBroomMode").toString() == "Off");
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
}

} // namespace tb::mcp
