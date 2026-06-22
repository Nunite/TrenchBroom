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

#include <QJsonObject>

#include "ui/mcp/McpBridgeServer.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace mcp = tb::mcp;

TEST_CASE("McpBridgeServer")
{
  auto server = McpBridgeServer{[](const QString& toolName, const QJsonObject&) {
    if (toolName == "tb_status")
    {
      return QJsonObject{
        {"application", "TrenchBroom"},
        {"mode", "ReadOnly"},
      };
    }
    if (toolName == "documents_list")
    {
      return QJsonObject{
        {"count", 0},
      };
    }
    if (toolName == "map_search")
    {
      return QJsonObject{
        {"count", 1},
        {"query", "worldspawn"},
      };
    }
    return QJsonObject{};
  }};

  SECTION("off mode does not listen")
  {
    auto error = QString{};
    CHECK(server.start(
      mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::Off}, &error));
    CHECK(error.isEmpty());
    CHECK(!server.isListening());
  }

  SECTION("rejects wrong token")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "wrong", "tb_status", {}, mcp::McpMode::ReadOnly});

    CHECK(!response.ok);
    REQUIRE(response.error);
    CHECK(response.error->code == mcp::McpErrorCode::Unauthorized);
  }

  SECTION("serves tb_status")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "secret", "tb_status", {}, mcp::McpMode::ReadOnly});

    CHECK(response.ok);
    CHECK(response.result.value("application").toString() == "TrenchBroom");
  }

  SECTION("serves wired read-only tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "secret", "documents_list", {}, mcp::McpMode::ReadOnly});

    CHECK(response.ok);
    CHECK(response.result.value("count").toInt() == 0);
  }

  SECTION("serves map_search")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "map_search",
      QJsonObject{{"query", "worldspawn"}},
      mcp::McpMode::ReadOnly});

    CHECK(response.ok);
    CHECK(response.result.value("query").toString() == "worldspawn");
    CHECK(response.result.value("count").toInt() == 1);
  }

  SECTION("rejects unwired tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "secret", "selection_set", {}, mcp::McpMode::ReadOnly});

    CHECK(!response.ok);
    REQUIRE(response.error);
    CHECK(response.error->code == mcp::McpErrorCode::Forbidden);
  }

  SECTION("mode gating rejects edit tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "secret", "entity_create", {}, mcp::McpMode::Edit});

    CHECK(!response.ok);
    REQUIRE(response.error);
    CHECK(response.error->code == mcp::McpErrorCode::Forbidden);
  }
}

} // namespace tb::ui
