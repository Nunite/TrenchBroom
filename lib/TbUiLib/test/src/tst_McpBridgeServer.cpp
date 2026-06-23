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
      return McpBridgeToolResult::success(QJsonObject{
        {"application", "TrenchBroom"},
        {"mode", "ReadOnly"},
      });
    }
    if (toolName == "documents_list")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 0},
      });
    }
    if (toolName == "documents_activate")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"activated", true},
      });
    }
    if (toolName == "documents_open")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"opened", true},
      });
    }
    if (toolName == "documents_save")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"saved", true},
      });
    }
    if (toolName == "documents_close")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"closed", true},
      });
    }
    if (toolName == "documents_export")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"exported", true},
      });
    }
    if (toolName == "map_search")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 1},
        {"query", "worldspawn"},
      });
    }
    if (toolName == "selection_set")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"selectedCount", 0},
      });
    }
    if (toolName == "selection_filter")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 0},
      });
    }
    if (toolName == "selection_by_bounds")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 0},
      });
    }
    if (toolName == "selection_grow")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"selectedCount", 0},
      });
    }
    if (toolName == "viewport_focus")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"focused", true},
      });
    }
    if (toolName == "viewport_clear_marks")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"active", false},
      });
    }
    if (toolName == "overlay_set")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"active", true},
      });
    }
    if (toolName == "entity_create")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-1"},
        {"transactionName", "MCP: Create info_player_start"},
      });
    }
    if (toolName == "history_list")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 0},
      });
    }
    if (toolName == "asset_search")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 0},
      });
    }
    if (toolName == "texture_search")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 0},
      });
    }
    if (toolName == "asset_place_model")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-2"},
        {"transactionName", "MCP: Place model asset"},
      });
    }
    if (toolName == "blockout_validate")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"valid", true},
      });
    }
    if (toolName == "blockout_create_room")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-3"},
        {"transactionName", "MCP: Blockout room"},
      });
    }
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::ToolNotFound, QString{"Unknown test tool"});
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

  SECTION("serves read-only document and selection parity tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto activateResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "1", "secret", "documents_activate", QJsonObject{{"index", 0}}, mcp::McpMode::ReadOnly});
    CHECK(activateResponse.ok);
    CHECK(activateResponse.result.value("activated").toBool());

    const auto filterResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "2", "secret", "selection_filter", {}, mcp::McpMode::ReadOnly});
    CHECK(filterResponse.ok);

    const auto boundsResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "3",
      "secret",
      "selection_by_bounds",
      QJsonObject{
        {"min", QJsonArray{0, 0, 0}},
        {"max", QJsonArray{128, 128, 128}},
      },
      mcp::McpMode::ReadOnly});
    CHECK(boundsResponse.ok);

    const auto growResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "4", "secret", "selection_grow", {}, mcp::McpMode::ReadOnly});
    CHECK(growResponse.ok);

    const auto focusResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "5", "secret", "viewport_focus", {}, mcp::McpMode::ReadOnly});
    CHECK(focusResponse.ok);

    const auto clearResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "6", "secret", "viewport_clear_marks", {}, mcp::McpMode::ReadOnly});
    CHECK(clearResponse.ok);
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

  SECTION("serves selection_set")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "secret", "selection_set", {}, mcp::McpMode::ReadOnly});

    CHECK(response.ok);
    CHECK(response.result.value("selectedCount").toInt() == 0);
  }

  SECTION("serves overlay_set")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "overlay_set",
      QJsonObject{{"highlightObjectIds", QJsonArray{}}},
      mcp::McpMode::ReadOnly});

    CHECK(response.ok);
    CHECK(response.result.value("active").toBool());
  }

  SECTION("read-only mode rejects action_execute")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "action_execute",
      QJsonObject{{"actionId", "Menu/Edit/Undo"}},
      mcp::McpMode::Edit});

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

  SECTION("read-only mode rejects document mutation tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "documents_save",
      QJsonObject{{"path", "C:/tmp/test.map"}},
      mcp::McpMode::Edit});

    CHECK(!response.ok);
    REQUIRE(response.error);
    CHECK(response.error->code == mcp::McpErrorCode::Forbidden);
  }

  SECTION("edit mode serves edit tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::Edit}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "entity_create",
      QJsonObject{{"classname", "info_player_start"}},
      mcp::McpMode::Edit});

    CHECK(response.ok);
    CHECK(response.result.value("operationId").toString() == "mcp-op-1");
  }

  SECTION("edit mode serves document lifecycle tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::Edit}));

    const auto openResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "documents_open",
      QJsonObject{{"path", "C:/tmp/test.map"}},
      mcp::McpMode::Edit});
    CHECK(openResponse.ok);

    const auto saveResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "2",
      "secret",
      "documents_save",
      QJsonObject{{"path", "C:/tmp/test.map"}},
      mcp::McpMode::Edit});
    CHECK(saveResponse.ok);

    const auto exportResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "3",
      "secret",
      "documents_export",
      QJsonObject{{"path", "C:/tmp/export.map"}},
      mcp::McpMode::Edit});
    CHECK(exportResponse.ok);

    const auto closeResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "4",
      "secret",
      "documents_close",
      QJsonObject{{"discardChanges", true}},
      mcp::McpMode::Edit});
    CHECK(closeResponse.ok);
  }

  SECTION("read-only mode serves history_list")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "secret", "history_list", {}, mcp::McpMode::ReadOnly});

    CHECK(response.ok);
    CHECK(response.result.value("count").toInt() == 0);
  }

  SECTION("read-only mode serves asset and texture search")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto assetResponse = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "secret", "asset_search", {}, mcp::McpMode::ReadOnly});
    CHECK(assetResponse.ok);

    const auto textureResponse = server.dispatchRequest(
      mcp::McpBridgeRequest{"2", "secret", "texture_search", {}, mcp::McpMode::ReadOnly});
    CHECK(textureResponse.ok);
  }

  SECTION("edit mode serves asset placement")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::Edit}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "asset_place_model",
      QJsonObject{{"path", "models/player.mdl"}},
      mcp::McpMode::Edit});

    CHECK(response.ok);
    CHECK(response.result.value("operationId").toString() == "mcp-op-2");
  }

  SECTION("read-only mode serves blockout validation")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "blockout_validate",
      QJsonObject{
        {"type", "room"},
        {"min", QJsonArray{0, 0, 0}},
        {"max", QJsonArray{128, 128, 128}},
      },
      mcp::McpMode::ReadOnly});

    CHECK(response.ok);
    CHECK(response.result.value("valid").toBool());
  }

  SECTION("edit mode serves blockout creation")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::Edit}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "blockout_create_room",
      QJsonObject{
        {"min", QJsonArray{0, 0, 0}},
        {"max", QJsonArray{128, 128, 128}},
      },
      mcp::McpMode::Edit});

    CHECK(response.ok);
    CHECK(response.result.value("operationId").toString() == "mcp-op-3");
  }
}

} // namespace tb::ui
