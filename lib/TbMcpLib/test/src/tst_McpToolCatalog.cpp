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

#include "mcp/McpToolCatalog.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::mcp
{

TEST_CASE("McpToolCatalog")
{
  SECTION("contains first-phase tools")
  {
    CHECK(findToolDefinition("tb_status"));
    CHECK(findToolDefinition("documents_list"));
    CHECK(findToolDefinition("map_snapshot"));
    CHECK(findToolDefinition("map_search"));
    CHECK(findToolDefinition("selection_get"));
    CHECK(findToolDefinition("actions_list"));
    CHECK(findToolDefinition("overlay_set"));
  }

  SECTION("read-only mode lists implemented read-only tools only")
  {
    const auto tools = toolsListJson(McpMode::ReadOnly);
    auto names = QStringList{};
    for (const auto& tool : tools)
    {
      names.push_back(tool.toObject().value("name").toString());
    }

    CHECK(names.contains("tb_status"));
    CHECK(names.contains("map_snapshot"));
    CHECK(names.contains("map_search"));
    CHECK(names.contains("selection_set"));
    CHECK(names.contains("overlay_set"));
    CHECK(names.contains("history_list"));
    CHECK(!names.contains("entity_create"));
    CHECK(!names.contains("brush_create_box"));
    CHECK(!names.contains("action_execute"));
    CHECK(!names.contains("history_undo_mcp"));
  }

  SECTION("edit mode lists implemented edit tools")
  {
    const auto tools = toolsListJson(McpMode::Edit);
    auto names = QStringList{};
    for (const auto& tool : tools)
    {
      names.push_back(tool.toObject().value("name").toString());
    }

    CHECK(names.contains("action_execute"));
    CHECK(names.contains("entity_create"));
    CHECK(names.contains("entity_update"));
    CHECK(names.contains("entity_delete"));
    CHECK(names.contains("brush_create_box"));
    CHECK(names.contains("brush_create_wedge"));
    CHECK(names.contains("brush_create_cylinder"));
    CHECK(names.contains("history_undo_mcp"));
    CHECK(names.contains("history_redo_mcp"));
  }

  SECTION("mode gating rejects edit tools in read-only mode")
  {
    const auto editTool = findToolDefinition("entity_create");

    REQUIRE(editTool);
    CHECK(!canCallTool(*editTool, McpMode::ReadOnly));
    CHECK(canCallTool(*editTool, McpMode::Edit));
  }

  SECTION("tool json uses MCP inputSchema shape")
  {
    const auto tool = findToolDefinition("map_search");

    REQUIRE(tool);
    const auto json = toMcpToolJson(*tool);

    CHECK(json.value("name").toString() == "map_search");
    CHECK(json.value("inputSchema").toObject().value("type").toString() == "object");
  }
}

} // namespace tb::mcp
