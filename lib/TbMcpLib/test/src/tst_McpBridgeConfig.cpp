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

#include <QDir>
#include <QJsonObject>
#include <QTemporaryDir>

#include "mcp/McpBridgeConfig.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::mcp
{

TEST_CASE("McpBridgeConfig")
{
  SECTION("default config is disabled and authenticated")
  {
    const auto config = defaultBridgeConfig();

    CHECK(config.pipeName.startsWith("trenchbroom-mcp-"));
    CHECK(!config.token.isEmpty());
    CHECK(config.mode == McpMode::Off);
    CHECK(config.httpEnabled);
    CHECK(config.httpHost == "127.0.0.1");
    CHECK(config.httpPort == 37666);
  }

  SECTION("json roundtrip")
  {
    const auto config = McpBridgeConfig{
      "test-pipe", "secret-token", McpMode::ReadOnly, true, "localhost", 37667};

    const auto parsed = bridgeConfigFromJson(toJson(config));

    REQUIRE(parsed);
    CHECK(parsed->pipeName == config.pipeName);
    CHECK(parsed->token == config.token);
    CHECK(parsed->mode == config.mode);
    CHECK(parsed->httpEnabled == config.httpEnabled);
    CHECK(parsed->httpHost == config.httpHost);
    CHECK(parsed->httpPort == config.httpPort);
  }

  SECTION("legacy json gets default http settings")
  {
    const auto parsed = bridgeConfigFromJson(QJsonObject{
      {"pipeName", "test-pipe"},
      {"token", "secret-token"},
      {"mode", "ReadOnly"},
    });

    REQUIRE(parsed);
    CHECK(parsed->httpEnabled);
    CHECK(parsed->httpHost == "127.0.0.1");
    CHECK(parsed->httpPort == 37666);
  }

  SECTION("rejects invalid json")
  {
    auto error = QString{};

    CHECK(!bridgeConfigFromJson(QJsonObject{}, &error));
    CHECK(error.contains("pipeName"));
  }

  SECTION("read or create config")
  {
    auto tempDir = QTemporaryDir{};
    REQUIRE(tempDir.isValid());

    const auto path = QDir{tempDir.path()}.filePath("MCP/config.json");
    auto error = QString{};
    const auto created = readOrCreateBridgeConfig(path, &error);

    REQUIRE(created);
    CHECK(error.isEmpty());
    CHECK(created->mode == McpMode::Off);

    auto loaded = readBridgeConfig(path, &error);
    REQUIRE(loaded);
    CHECK(loaded->pipeName == created->pipeName);
    CHECK(loaded->token == created->token);
    CHECK(loaded->mode == created->mode);
  }
}

} // namespace tb::mcp
