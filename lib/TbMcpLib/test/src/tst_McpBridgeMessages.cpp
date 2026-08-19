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

#include <QJsonObject>

#include "mcp/McpBridgeMessages.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::mcp
{

TEST_CASE("McpBridgeMessages")
{
  SECTION("request json roundtrip")
  {
    const auto request = McpBridgeRequest{
      "1",
      "tb_status",
      QJsonObject{{"verbose", true}},
      McpMode::ReadOnly,
    };

    const auto parsed = bridgeRequestFromJson(toJson(request));

    REQUIRE(parsed);
    CHECK(parsed->id == request.id);
    CHECK(parsed->tool == request.tool);
    CHECK(parsed->params.value("verbose").toBool());
    REQUIRE(parsed->requestedMode);
    CHECK(*parsed->requestedMode == McpMode::ReadOnly);
    CHECK(parsed->type == McpBridgeRequestType::ToolCall);
    CHECK_FALSE(toJson(request).contains("token"));
    CHECK(toJson(request).value("type").toString() == "tool_call");
  }

  SECTION("typed resource requests roundtrip")
  {
    const auto listRequest = McpBridgeRequest{
      "list-1",
      {},
      QJsonObject{{"cursor", "opaque-cursor"}},
      std::nullopt,
      McpBridgeRequestType::ResourcesList,
    };
    const auto readRequest = McpBridgeRequest{
      "read-1",
      {},
      QJsonObject{{"uri", "tbmcp://operation/mcp-op-1"}},
      std::nullopt,
      McpBridgeRequestType::ResourceRead,
    };

    const auto parsedList = bridgeRequestFromJson(toJson(listRequest));
    const auto parsedRead = bridgeRequestFromJson(toJson(readRequest));

    REQUIRE(parsedList);
    CHECK(parsedList->type == McpBridgeRequestType::ResourcesList);
    CHECK(parsedList->tool.isEmpty());
    CHECK(parsedList->params.value("cursor").toString() == "opaque-cursor");
    REQUIRE(parsedRead);
    CHECK(parsedRead->type == McpBridgeRequestType::ResourceRead);
    CHECK(parsedRead->params.value("uri").toString() == "tbmcp://operation/mcp-op-1");
  }

  SECTION("tokenless legacy requests without a type remain tool calls")
  {
    const auto parsed = bridgeRequestFromJson(QJsonObject{
      {"id", "legacy-1"},
      {"tool", "tb_status"},
      {"params", QJsonObject{}},
    });

    REQUIRE(parsed);
    CHECK(parsed->type == McpBridgeRequestType::ToolCall);
    CHECK(parsed->tool == "tb_status");
  }

  SECTION("rejects unknown request types")
  {
    auto error = QString{};
    const auto parsed = bridgeRequestFromJson(
      QJsonObject{
        {"id", "1"},
        {"type", "unknown"},
        {"params", QJsonObject{}},
      },
      &error);

    CHECK_FALSE(parsed);
    CHECK(error.contains("type"));
  }

  SECTION("rejects invalid request params")
  {
    auto error = QString{};
    auto json = QJsonObject{
      {"id", "1"},
      {"tool", "tb_status"},
      {"params", true},
    };

    CHECK(!bridgeRequestFromJson(json, &error));
    CHECK(error.contains("params"));
  }

  SECTION("success response json roundtrip")
  {
    const auto response =
      McpBridgeResponse::success("1", QJsonObject{{"mode", "ReadOnly"}});

    const auto parsed = bridgeResponseFromJson(toJson(response));

    REQUIRE(parsed);
    CHECK(parsed->id == response.id);
    CHECK(parsed->ok);
    CHECK(parsed->result.value("mode").toString() == "ReadOnly");
    CHECK(!parsed->error);
  }

  SECTION("failure response json roundtrip")
  {
    const auto response = McpBridgeResponse::failure(
      "1",
      McpError{
        McpErrorCode::Forbidden,
        "denied",
        QJsonObject{{"stderrBytes", 12}},
      });

    const auto parsed = bridgeResponseFromJson(toJson(response));

    REQUIRE(parsed);
    CHECK(parsed->id == response.id);
    CHECK(!parsed->ok);
    REQUIRE(parsed->error);
    CHECK(parsed->error->code == McpErrorCode::Forbidden);
    CHECK(parsed->error->message == "denied");
    CHECK(parsed->error->details.value("stderrBytes").toInt() == 12);
  }
}

} // namespace tb::mcp
