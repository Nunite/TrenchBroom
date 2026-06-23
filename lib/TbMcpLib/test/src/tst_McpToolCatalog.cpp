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
    CHECK(findToolDefinition("documents_open"));
    CHECK(findToolDefinition("documents_activate"));
    CHECK(findToolDefinition("documents_save"));
    CHECK(findToolDefinition("documents_close"));
    CHECK(findToolDefinition("documents_export"));
    CHECK(findToolDefinition("map_snapshot"));
    CHECK(findToolDefinition("map_search"));
    CHECK(findToolDefinition("selection_get"));
    CHECK(findToolDefinition("selection_filter"));
    CHECK(findToolDefinition("selection_by_bounds"));
    CHECK(findToolDefinition("selection_grow"));
    CHECK(findToolDefinition("viewport_focus"));
    CHECK(findToolDefinition("viewport_clear_marks"));
    CHECK(findToolDefinition("viewport_capture_current"));
    CHECK(findToolDefinition("viewport_capture_3d"));
    CHECK(findToolDefinition("viewport_capture_2d"));
    CHECK(findToolDefinition("fgd_entities_list"));
    CHECK(findToolDefinition("entity_schema"));
    CHECK(findToolDefinition("entity_create_from_schema"));
    CHECK(findToolDefinition("entity_tie_brushes"));
    CHECK(findToolDefinition("entity_untie_brushes"));
    CHECK(findToolDefinition("brush_types_list"));
    CHECK(findToolDefinition("brush_create"));
    CHECK(findToolDefinition("brush_create_cone"));
    CHECK(findToolDefinition("brush_create_pipe"));
    CHECK(findToolDefinition("brush_create_sphere"));
    CHECK(findToolDefinition("brush_create_pyramid"));
    CHECK(findToolDefinition("brush_create_tetrahedron"));
    CHECK(findToolDefinition("brush_create_from_planes"));
    CHECK(findToolDefinition("brush_create_arch"));
    CHECK(findToolDefinition("brush_create_torus"));
    CHECK(findToolDefinition("objects_delete"));
    CHECK(findToolDefinition("objects_transform"));
    CHECK(findToolDefinition("map_validate"));
    CHECK(findToolDefinition("problems_check"));
    CHECK(findToolDefinition("problems_fix"));
    CHECK(findToolDefinition("map_fix_all_safe"));
    CHECK(findToolDefinition("compile_profiles_list"));
    CHECK(findToolDefinition("compile_run"));
    CHECK(findToolDefinition("compile_log_tail"));
    CHECK(findToolDefinition("leaks_load_pointfile"));
    CHECK(findToolDefinition("prefabs_list"));
    CHECK(findToolDefinition("prefab_create"));
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
    CHECK(names.contains("documents_activate"));
    CHECK(names.contains("map_snapshot"));
    CHECK(names.contains("map_search"));
    CHECK(names.contains("selection_set"));
    CHECK(names.contains("selection_filter"));
    CHECK(names.contains("selection_by_bounds"));
    CHECK(names.contains("selection_grow"));
    CHECK(names.contains("viewport_focus"));
    CHECK(names.contains("viewport_clear_marks"));
    CHECK(names.contains("viewport_capture_current"));
    CHECK(!names.contains("viewport_capture_3d"));
    CHECK(!names.contains("viewport_capture_2d"));
    CHECK(names.contains("fgd_entities_list"));
    CHECK(names.contains("entity_schema"));
    CHECK(names.contains("brush_types_list"));
    CHECK(names.contains("overlay_set"));
    CHECK(names.contains("history_list"));
    CHECK(names.contains("asset_search"));
    CHECK(names.contains("textures_list"));
    CHECK(names.contains("texture_search"));
    CHECK(names.contains("face_list"));
    CHECK(names.contains("face_select"));
    CHECK(names.contains("map_validate"));
    CHECK(names.contains("problems_check"));
    CHECK(names.contains("compile_profiles_list"));
    CHECK(names.contains("compile_log_tail"));
    CHECK(names.contains("blockout_validate"));
    CHECK(!names.contains("documents_open"));
    CHECK(!names.contains("documents_save"));
    CHECK(!names.contains("documents_close"));
    CHECK(!names.contains("documents_export"));
    CHECK(!names.contains("entity_create_from_schema"));
    CHECK(!names.contains("entity_tie_brushes"));
    CHECK(!names.contains("entity_untie_brushes"));
    CHECK(!names.contains("brush_create"));
    CHECK(!names.contains("entity_create"));
    CHECK(!names.contains("brush_create_box"));
    CHECK(!names.contains("asset_place_model"));
    CHECK(!names.contains("prefabs_list"));
    CHECK(!names.contains("prefab_create"));
    CHECK(!names.contains("texture_apply"));
    CHECK(!names.contains("texture_replace"));
    CHECK(!names.contains("face_texture_set"));
    CHECK(!names.contains("objects_delete"));
    CHECK(!names.contains("objects_transform"));
    CHECK(!names.contains("problems_fix"));
    CHECK(!names.contains("map_fix_all_safe"));
    CHECK(!names.contains("compile_run"));
    CHECK(!names.contains("leaks_load_pointfile"));
    CHECK(!names.contains("blockout_create_room"));
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
    CHECK(names.contains("documents_open"));
    CHECK(names.contains("documents_save"));
    CHECK(names.contains("documents_close"));
    CHECK(names.contains("documents_export"));
    CHECK(names.contains("entity_create"));
    CHECK(names.contains("entity_update"));
    CHECK(names.contains("entity_delete"));
    CHECK(names.contains("entity_create_from_schema"));
    CHECK(names.contains("entity_tie_brushes"));
    CHECK(names.contains("entity_untie_brushes"));
    CHECK(names.contains("brush_types_list"));
    CHECK(names.contains("brush_create"));
    CHECK(names.contains("brush_create_box"));
    CHECK(names.contains("brush_create_wedge"));
    CHECK(names.contains("brush_create_cylinder"));
    CHECK(names.contains("brush_create_cone"));
    CHECK(names.contains("brush_create_pipe"));
    CHECK(names.contains("brush_create_sphere"));
    CHECK(names.contains("brush_create_pyramid"));
    CHECK(names.contains("brush_create_tetrahedron"));
    CHECK(names.contains("brush_create_from_planes"));
    CHECK(!names.contains("brush_create_arch"));
    CHECK(!names.contains("brush_create_torus"));
    CHECK(names.contains("asset_place_model"));
    CHECK(names.contains("asset_place_sprite"));
    CHECK(names.contains("asset_place_sound"));
    CHECK(!names.contains("prefabs_list"));
    CHECK(!names.contains("prefab_create"));
    CHECK(names.contains("textures_list"));
    CHECK(names.contains("texture_apply"));
    CHECK(names.contains("texture_replace"));
    CHECK(names.contains("texture_align_face"));
    CHECK(names.contains("texture_copy_from_face"));
    CHECK(names.contains("face_list"));
    CHECK(names.contains("face_select"));
    CHECK(names.contains("face_texture_set"));
    CHECK(names.contains("objects_delete"));
    CHECK(names.contains("objects_transform"));
    CHECK(names.contains("map_validate"));
    CHECK(names.contains("problems_check"));
    CHECK(names.contains("problems_fix"));
    CHECK(names.contains("map_fix_all_safe"));
    CHECK(names.contains("compile_profiles_list"));
    CHECK(names.contains("compile_run"));
    CHECK(names.contains("compile_log_tail"));
    CHECK(names.contains("leaks_load_pointfile"));
    CHECK(names.contains("blockout_create_room"));
    CHECK(names.contains("blockout_create_corridor"));
    CHECK(names.contains("blockout_create_stairs"));
    CHECK(names.contains("blockout_create_ramp"));
    CHECK(names.contains("blockout_create_doorway"));
    CHECK(names.contains("blockout_create_cover"));
    CHECK(names.contains("blockout_create_sky_shell"));
    CHECK(names.contains("blockout_validate"));
    CHECK(names.contains("history_undo_mcp"));
    CHECK(names.contains("history_redo_mcp"));
    CHECK(names.contains("viewport_capture_current"));
    CHECK(!names.contains("viewport_capture_3d"));
    CHECK(!names.contains("viewport_capture_2d"));
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
