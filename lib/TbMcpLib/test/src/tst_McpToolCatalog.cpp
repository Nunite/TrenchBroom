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
  SECTION("parses and names tool profiles")
  {
    CHECK(toolProfileName(McpToolProfile::Core) == "Core");
    CHECK(toolProfileName(McpToolProfile::Modeling) == "Modeling");
    CHECK(toolProfileName(McpToolProfile::Balanced) == "Balanced");
    CHECK(toolProfileName(McpToolProfile::Full) == "Full");

    CHECK(parseToolProfile("Core") == McpToolProfile::Core);
    CHECK(parseToolProfile("Modeling") == McpToolProfile::Modeling);
    CHECK(parseToolProfile("modeling") == McpToolProfile::Modeling);
    CHECK(parseToolProfile("Balanced") == McpToolProfile::Balanced);
    CHECK(parseToolProfile("Full") == McpToolProfile::Full);
  }

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
    CHECK(findToolDefinition("entity_create_checked"));
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
    CHECK(findToolDefinition("brush_create_prism"));
    CHECK(findToolDefinition("brush_create_cylinder_sector"));
    CHECK(findToolDefinition("brush_create_boxes_batch"));
    CHECK(findToolDefinition("brush_create_polygon_batch"));
    CHECK(findToolDefinition("shape_library_list"));
    CHECK(findToolDefinition("brush_metadata_set"));
    CHECK(findToolDefinition("brush_metadata_get"));
    CHECK(findToolDefinition("selection_by_metadata"));
    CHECK(findToolDefinition("route_geometry_analyze_chain"));
    CHECK(findToolDefinition("kz_distance_analyze_chain"));
    CHECK(findToolDefinition("brush_create_arch"));
    CHECK(findToolDefinition("brush_create_torus"));
    CHECK(findToolDefinition("objects_delete"));
    CHECK(findToolDefinition("objects_delete_by_filter"));
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
    CHECK(findToolDefinition("texture_lock_get"));
    CHECK(findToolDefinition("texture_lock_set"));
    CHECK(findToolDefinition("texture_apply_by_filter"));
    CHECK(findToolDefinition("geometry_analyze_selection"));
    CHECK(findToolDefinition("blockout_create_spiral_stairs"));
    CHECK(findToolDefinition("blockout_validate_spiral_stairs"));
    CHECK(findToolDefinition("operation_inspect"));
    CHECK(findToolDefinition("operation_select"));
    CHECK(findToolDefinition("operation_validate"));
    CHECK(findToolDefinition("blockout_create_batch"));
    CHECK(findToolDefinition("blockout_create_curved_corridor"));
    CHECK(findToolDefinition("python_generate_blockout"));
    CHECK(findToolDefinition("heightmap_import_grayscale"));
    CHECK(findToolDefinition("tb_tools_search"));
    CHECK(findToolDefinition("actions_list"));
    CHECK(findToolDefinition("overlay_set"));
  }

  SECTION("read-only mode lists implemented read-only tools only")
  {
    const auto tools = toolsListJson(McpMode::ReadOnly, true, McpToolProfile::Balanced);
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
    CHECK(names.contains("viewport_capture_3d"));
    CHECK(names.contains("viewport_capture_2d"));
    CHECK(names.contains("fgd_entities_list"));
    CHECK(names.contains("entity_schema"));
    CHECK(names.contains("brush_types_list"));
    CHECK(names.contains("shape_library_list"));
    CHECK(names.contains("brush_metadata_get"));
    CHECK(names.contains("selection_by_metadata"));
    CHECK(names.contains("route_geometry_analyze_chain"));
    CHECK(!names.contains("kz_distance_analyze_chain"));
    CHECK(names.contains("overlay_set"));
    CHECK(names.contains("history_list"));
    CHECK(names.contains("asset_search"));
    CHECK(names.contains("textures_list"));
    CHECK(names.contains("texture_search"));
    CHECK(names.contains("texture_lock_get"));
    CHECK(names.contains("face_list"));
    CHECK(names.contains("face_select"));
    CHECK(names.contains("map_validate"));
    CHECK(names.contains("problems_check"));
    CHECK(names.contains("compile_profiles_list"));
    CHECK(names.contains("compile_log_tail"));
    CHECK(names.contains("blockout_validate"));
    CHECK(names.contains("geometry_analyze_selection"));
    CHECK(names.contains("blockout_validate_spiral_stairs"));
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
    CHECK(!names.contains("brush_create_prism"));
    CHECK(!names.contains("brush_create_cylinder_sector"));
    CHECK(!names.contains("brush_create_polygon_batch"));
    CHECK(!names.contains("brush_metadata_set"));
    CHECK(!names.contains("blockout_create_spiral_stairs"));
    CHECK(!names.contains("asset_place_model"));
    CHECK(!names.contains("prefabs_list"));
    CHECK(!names.contains("prefab_create"));
    CHECK(!names.contains("texture_apply"));
    CHECK(!names.contains("texture_lock_set"));
    CHECK(!names.contains("texture_replace"));
    CHECK(!names.contains("face_texture_set"));
    CHECK(!names.contains("objects_delete"));
    CHECK(!names.contains("objects_transform"));
    CHECK(!names.contains("problems_fix"));
    CHECK(!names.contains("map_fix_all_safe"));
    CHECK(!names.contains("compile_run"));
    CHECK(!names.contains("leaks_load_pointfile"));
    CHECK(!names.contains("blockout_create_room"));
    CHECK(!names.contains("python_generate_blockout"));
    CHECK(!names.contains("action_execute"));
    CHECK(!names.contains("history_undo_mcp"));
  }

  SECTION("balanced edit mode lists common tools and hides expert brush tools")
  {
    const auto tools = toolsListJson(McpMode::Edit, true, McpToolProfile::Balanced);
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
    CHECK(names.contains("entity_create_checked"));
    CHECK(names.contains("entity_tie_brushes"));
    CHECK(names.contains("entity_untie_brushes"));
    CHECK(names.contains("brush_types_list"));
    CHECK(!names.contains("brush_create"));
    CHECK(!names.contains("brush_create_box"));
    CHECK(!names.contains("brush_create_wedge"));
    CHECK(!names.contains("brush_create_cylinder"));
    CHECK(!names.contains("brush_create_cone"));
    CHECK(!names.contains("brush_create_pipe"));
    CHECK(!names.contains("brush_create_sphere"));
    CHECK(!names.contains("brush_create_pyramid"));
    CHECK(!names.contains("brush_create_tetrahedron"));
    CHECK(!names.contains("brush_create_from_planes"));
    CHECK(!names.contains("brush_create_prism"));
    CHECK(!names.contains("brush_create_cylinder_sector"));
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
    CHECK(names.contains("blockout_create_spiral_stairs"));
    CHECK(names.contains("blockout_create_batch"));
    CHECK(names.contains("blockout_create_curved_corridor"));
    CHECK(names.contains("python_generate_blockout"));
    CHECK(names.contains("heightmap_import_grayscale"));
    CHECK(names.contains("blockout_validate"));
    CHECK(names.contains("geometry_analyze_selection"));
    CHECK(names.contains("blockout_validate_spiral_stairs"));
    CHECK(names.contains("history_undo_mcp"));
    CHECK(names.contains("history_redo_mcp"));
    CHECK(names.contains("viewport_capture_current"));
    CHECK(names.contains("viewport_capture_3d"));
    CHECK(names.contains("viewport_capture_2d"));
  }

  SECTION("modeling profile is the default and keeps modeling tools visible")
  {
    const auto tools = toolsListJson(McpMode::Edit);
    const auto explicitTools =
      toolsListJson(McpMode::Edit, true, McpToolProfile::Modeling);
    CHECK(tools == explicitTools);

    auto names = QStringList{};
    for (const auto& tool : tools)
    {
      names.push_back(tool.toObject().value("name").toString());
    }

    CHECK(names.contains("tb_status"));
    CHECK(names.contains("tb_doctor"));
    CHECK(names.contains("tb_tools_search"));
    CHECK(names.contains("selection_get"));
    CHECK(names.contains("selection_set"));
    CHECK(names.contains("selection_filter"));
    CHECK(names.contains("selection_by_bounds"));
    CHECK(names.contains("selection_grow"));
    CHECK(names.contains("map_snapshot"));
    CHECK(names.contains("map_search"));
    CHECK(names.contains("fgd_entities_list"));
    CHECK(names.contains("entity_schema"));
    CHECK(names.contains("entity_create_checked"));
    CHECK(names.contains("entity_create_from_schema"));
    CHECK(names.contains("entity_tie_brushes"));
    CHECK(names.contains("entity_untie_brushes"));
    CHECK(!names.contains("entity_create"));
    CHECK(!names.contains("entity_update"));
    CHECK(!names.contains("entity_delete"));
    CHECK(names.contains("operation_inspect"));
    CHECK(names.contains("operation_select"));
    CHECK(names.contains("operation_validate"));
    CHECK(names.contains("history_list"));
    CHECK(names.contains("history_undo_mcp"));
    CHECK(names.contains("history_redo_mcp"));
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
    CHECK(names.contains("brush_create_prism"));
    CHECK(names.contains("brush_create_cylinder_sector"));
    CHECK(names.contains("brush_create_boxes_batch"));
    CHECK(names.contains("brush_create_polygon_batch"));
    CHECK(names.contains("brush_create_from_planes"));
    CHECK(names.contains("shape_library_list"));
    CHECK(names.contains("brush_metadata_set"));
    CHECK(names.contains("brush_metadata_get"));
    CHECK(names.contains("selection_by_metadata"));
    CHECK(names.contains("route_geometry_analyze_chain"));
    CHECK(!names.contains("kz_distance_analyze_chain"));
    CHECK(names.contains("blockout_create_batch"));
    CHECK(names.contains("python_generate_blockout"));
    CHECK(names.contains("heightmap_import_grayscale"));
    CHECK(names.contains("geometry_analyze_selection"));
    CHECK(names.contains("blockout_validate"));
    CHECK(names.contains("objects_delete"));
    CHECK(names.contains("objects_delete_by_filter"));
    CHECK(names.contains("objects_transform"));
    CHECK(names.contains("textures_list"));
    CHECK(names.contains("texture_search"));
    CHECK(names.contains("texture_lock_get"));
    CHECK(names.contains("texture_lock_set"));
    CHECK(names.contains("texture_apply"));
    CHECK(names.contains("texture_apply_by_filter"));
    CHECK(names.contains("texture_replace"));
    CHECK(names.contains("texture_align_face"));
    CHECK(names.contains("texture_copy_from_face"));
    CHECK(names.contains("face_list"));
    CHECK(names.contains("face_select"));
    CHECK(names.contains("face_texture_set"));
    CHECK(names.contains("map_validate"));
    CHECK(names.contains("problems_check"));

    CHECK(!names.contains("actions_list"));
    CHECK(!names.contains("action_execute"));
    CHECK(!names.contains("asset_search"));
    CHECK(!names.contains("asset_place_model"));
    CHECK(!names.contains("asset_place_sprite"));
    CHECK(!names.contains("asset_place_sound"));
    CHECK(!names.contains("blockout_create_room"));
    CHECK(!names.contains("blockout_create_corridor"));
    CHECK(!names.contains("blockout_create_stairs"));
    CHECK(!names.contains("blockout_create_ramp"));
    CHECK(!names.contains("blockout_create_doorway"));
    CHECK(!names.contains("blockout_create_cover"));
    CHECK(!names.contains("blockout_create_sky_shell"));
    CHECK(!names.contains("blockout_create_spiral_stairs"));
    CHECK(!names.contains("blockout_create_curved_corridor"));
    CHECK(!names.contains("blockout_validate_spiral_stairs"));
    CHECK(!names.contains("documents_open"));
    CHECK(!names.contains("documents_save"));
    CHECK(!names.contains("documents_close"));
    CHECK(!names.contains("documents_export"));
    CHECK(!names.contains("compile_profiles_list"));
    CHECK(!names.contains("compile_run"));
    CHECK(!names.contains("compile_log_tail"));
    CHECK(!names.contains("leaks_load_pointfile"));
    CHECK(!names.contains("problems_fix"));
    CHECK(!names.contains("map_fix_all_safe"));
    CHECK(!names.contains("viewport_focus"));
    CHECK(!names.contains("viewport_clear_marks"));
    CHECK(!names.contains("viewport_capture_3d"));
    CHECK(!names.contains("overlay_set"));
    CHECK(!names.contains("overlay_clear"));
  }

  SECTION("modeling read-only profile exposes entity schema but not entity creation")
  {
    const auto tools = toolsListJson(McpMode::ReadOnly, true, McpToolProfile::Modeling);
    auto names = QStringList{};
    for (const auto& tool : tools)
    {
      names.push_back(tool.toObject().value("name").toString());
    }

    CHECK(names.contains("fgd_entities_list"));
    CHECK(names.contains("entity_schema"));
    CHECK(!names.contains("entity_create"));
    CHECK(!names.contains("entity_create_from_schema"));
  }

  SECTION("heightmap import tool requires image path and is modeling visible")
  {
    const auto tool = findToolDefinition("heightmap_import_grayscale");
    REQUIRE(tool);

    CHECK(tool->category == "heightmap");
    CHECK(tool->requiredMode == McpMode::Edit);
    CHECK(tool->mutatesDocument);

    const auto required = tool->inputSchema.value("required").toArray();
    CHECK(required.contains("imagePath"));

    const auto properties = tool->inputSchema.value("properties").toObject();
    CHECK(properties.contains("mode"));
    CHECK(properties.contains("minCellSize"));
    CHECK(properties.contains("maxCellSize"));
    CHECK(properties.contains("errorTolerance"));

    const auto readOnlyTools =
      toolsListJson(McpMode::ReadOnly, true, McpToolProfile::Modeling);
    auto readOnlyNames = QStringList{};
    for (const auto& entry : readOnlyTools)
    {
      readOnlyNames.push_back(entry.toObject().value("name").toString());
    }
    CHECK(!readOnlyNames.contains("heightmap_import_grayscale"));

    const auto editTools = toolsListJson(McpMode::Edit, true, McpToolProfile::Modeling);
    auto editNames = QStringList{};
    for (const auto& entry : editTools)
    {
      editNames.push_back(entry.toObject().value("name").toString());
    }
    CHECK(editNames.contains("heightmap_import_grayscale"));
  }

  SECTION("core profile keeps only compact discovery and batch-oriented tools")
  {
    const auto tools = toolsListJson(McpMode::Edit, true, McpToolProfile::Core);
    auto names = QStringList{};
    for (const auto& tool : tools)
    {
      names.push_back(tool.toObject().value("name").toString());
    }

    CHECK(names.contains("tb_status"));
    CHECK(names.contains("tb_doctor"));
    CHECK(names.contains("tb_tools_search"));
    CHECK(names.contains("blockout_create_batch"));
    CHECK(names.contains("operation_inspect"));
    CHECK(names.contains("operation_select"));
    CHECK(names.contains("operation_validate"));
    CHECK(names.contains("viewport_capture_current"));
    CHECK(names.contains("geometry_analyze_selection"));
    CHECK(names.contains("blockout_validate"));
    CHECK(!names.contains("python_generate_blockout"));
    CHECK(!names.contains("documents_open"));
    CHECK(!names.contains("entity_create"));
    CHECK(!names.contains("brush_create"));
    CHECK(!names.contains("blockout_create_room"));
  }

  SECTION("full profile exposes implemented expert brush tools")
  {
    const auto tools = toolsListJson(McpMode::Edit, true, McpToolProfile::Full);
    auto names = QStringList{};
    for (const auto& tool : tools)
    {
      names.push_back(tool.toObject().value("name").toString());
    }

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
    CHECK(names.contains("brush_create_prism"));
    CHECK(names.contains("brush_create_cylinder_sector"));
    CHECK(names.contains("brush_create_polygon_batch"));
    CHECK(!names.contains("brush_create_arch"));
    CHECK(!names.contains("brush_create_torus"));
  }

  SECTION("python blockout tool requires script and is discoverable from core profile")
  {
    const auto tool = findToolDefinition("python_generate_blockout");

    REQUIRE(tool);
    CHECK(tool->category == "python");
    CHECK(tool->requiredMode == McpMode::Edit);
    CHECK(tool->mutatesDocument);

    const auto required = tool->inputSchema.value("required").toArray();
    CHECK(required.contains("script"));

    const auto tools =
      toolsSearchJson("python", "", "schema", McpMode::Edit, McpToolProfile::Core);
    auto found = QJsonObject{};
    for (const auto& entry : tools)
    {
      const auto object = entry.toObject();
      if (object.value("name").toString() == "python_generate_blockout")
      {
        found = object;
        break;
      }
    }

    REQUIRE(!found.isEmpty());
    CHECK(found.value("category").toString() == "python");
    CHECK(!found.value("visibleInCurrentProfile").toBool());
    CHECK(found.value("inputSchema").isObject());
  }

  SECTION("curved corridor exposes snap mode schema")
  {
    const auto tool = findToolDefinition("blockout_create_curved_corridor");
    REQUIRE(tool);

    const auto properties = tool->inputSchema.value("properties").toObject();
    CHECK(properties.value("snapMode").isObject());
    CHECK(properties.value("snapMode")
            .toObject()
            .value("description")
            .toString()
            .contains("radial"));
    CHECK(properties.value("grid").isObject());

    const auto sectorTool = findToolDefinition("brush_create_cylinder_sector");
    REQUIRE(sectorTool);
    CHECK(sectorTool->inputSchema.value("properties")
            .toObject()
            .value("snapMode")
            .isObject());
  }

  SECTION("tool search can discover hidden tools from modeling profile")
  {
    const auto tools = toolsSearchJson(
      "viewport_capture_3d", "", "schema", McpMode::Edit, McpToolProfile::Modeling);
    auto found = QJsonObject{};
    for (const auto& tool : tools)
    {
      const auto object = tool.toObject();
      if (object.value("name").toString() == "viewport_capture_3d")
      {
        found = object;
        break;
      }
    }

    REQUIRE(!found.isEmpty());
    CHECK(!found.value("visibleInCurrentProfile").toBool());
    CHECK(found.value("inputSchema").isObject());
  }

  SECTION("tool search can discover hidden expert tools")
  {
    const auto tools = toolsSearchJson(
      "from_planes", "brush", "schema", McpMode::Edit, McpToolProfile::Balanced);
    auto found = QJsonObject{};
    for (const auto& tool : tools)
    {
      const auto object = tool.toObject();
      if (object.value("name").toString() == "brush_create_from_planes")
      {
        found = object;
        break;
      }
    }

    REQUIRE(!found.isEmpty());
    CHECK(found.value("category").toString() == "brush");
    CHECK(found.value("expert").toBool());
    CHECK(!found.value("visibleInCurrentProfile").toBool());
    CHECK(found.value("inputSchema").isObject());
  }

  SECTION("tool search matches tokenized schema queries")
  {
    const auto tools = toolsSearchJson(
      "blockout_create_batch operations box format",
      "blockout",
      "schema",
      McpMode::Edit,
      McpToolProfile::Modeling);
    auto found = QJsonObject{};
    for (const auto& tool : tools)
    {
      const auto object = tool.toObject();
      if (object.value("name").toString() == "blockout_create_batch")
      {
        found = object;
        break;
      }
    }

    REQUIRE(!found.isEmpty());
    CHECK(found.value("category").toString() == "blockout");
    CHECK(found.value("visibleInCurrentProfile").toBool());
    CHECK(found.value("inputSchema").isObject());

    const auto schema = found.value("inputSchema").toObject();
    const auto operations =
      schema.value("properties").toObject().value("operations").toObject();
    CHECK(operations.value("items").isObject());
    const auto itemDescription =
      operations.value("items").toObject().value("description").toString();
    CHECK(itemDescription.contains(R"("type":"box")"));
    CHECK(itemDescription.contains(R"("type":"curved_corridor")"));
    CHECK(
      operations.value("items").toObject().value("required").toArray().contains("type"));
  }

  SECTION("tool search returns exact tool names from multi-tool queries")
  {
    const auto tools = toolsSearchJson(
      "brush_create_polygon_batch kz_distance_analyze_chain selection_by_metadata",
      "",
      "summary",
      McpMode::Edit,
      McpToolProfile::Modeling);
    auto names = QStringList{};
    for (const auto& tool : tools)
    {
      names.push_back(tool.toObject().value("name").toString());
    }

    CHECK(names.contains("brush_create_polygon_batch"));
    CHECK(names.contains("kz_distance_analyze_chain"));
    CHECK(names.contains("selection_by_metadata"));
  }

  SECTION("tool search exact names survive category hints")
  {
    const auto tools = toolsSearchJson(
      "blockout_create_batch operations box format",
      "blockout",
      "schema",
      McpMode::Edit,
      McpToolProfile::Modeling);
    auto names = QStringList{};
    for (const auto& tool : tools)
    {
      names.push_back(tool.toObject().value("name").toString());
    }

    CHECK(names.contains("blockout_create_batch"));
  }

  SECTION("tool search exact names can find hidden profile tools")
  {
    const auto tools = toolsSearchJson(
      "viewport_capture_3d", "", "summary", McpMode::Edit, McpToolProfile::Modeling);
    REQUIRE(tools.size() == 1);
    const auto tool = tools.first().toObject();
    CHECK(tool.value("name").toString() == "viewport_capture_3d");
    CHECK(!tool.value("visibleInCurrentProfile").toBool());
  }

  SECTION("safe batch modeling helpers have structured schemas")
  {
    const auto boxesTool = findToolDefinition("brush_create_boxes_batch");
    REQUIRE(boxesTool);
    CHECK(boxesTool->requiredMode == McpMode::Edit);
    CHECK(boxesTool->mutatesDocument);
    CHECK(boxesTool->category == "brush");
    const auto boxesSchema = boxesTool->inputSchema.value("properties").toObject();
    CHECK(boxesSchema.value("boxes").toObject().value("items").isObject());
    CHECK(boxesSchema.value("boxes")
            .toObject()
            .value("items")
            .toObject()
            .value("required")
            .toArray()
            .contains("min"));

    const auto polygonTool = findToolDefinition("brush_create_polygon_batch");
    REQUIRE(polygonTool);
    CHECK(polygonTool->requiredMode == McpMode::Edit);
    CHECK(polygonTool->mutatesDocument);
    CHECK(polygonTool->category == "brush");
    const auto polygonSchema = polygonTool->inputSchema.value("properties").toObject();
    const auto polygonItem =
      polygonSchema.value("brushes").toObject().value("items").toObject();
    CHECK(polygonItem.value("required").toArray().contains("points2d"));
    CHECK(polygonItem.value("required").toArray().contains("minZ"));
    CHECK(polygonItem.value("required").toArray().contains("maxZ"));
    CHECK(polygonItem.value("properties").toObject().contains("metadata"));
    const auto metadataSchema =
      polygonItem.value("properties").toObject().value("metadata").toObject();
    CHECK(metadataSchema.value("additionalProperties").toBool());
    CHECK(metadataSchema.value("description").toString().contains("Custom"));

    const auto deleteTool = findToolDefinition("objects_delete_by_filter");
    REQUIRE(deleteTool);
    CHECK(deleteTool->category == "object");
    CHECK(deleteTool->requiredMode == McpMode::Edit);

    const auto textureTool = findToolDefinition("texture_apply_by_filter");
    REQUIRE(textureTool);
    CHECK(textureTool->category == "texture");
    CHECK(textureTool->inputSchema.value("required").toArray().contains("material"));

    const auto entityTool = findToolDefinition("entity_create_checked");
    REQUIRE(entityTool);
    CHECK(entityTool->category == "entity");
    CHECK(entityTool->inputSchema.value("required").toArray().contains("classname"));
  }

  SECTION("mode gating rejects edit tools in read-only mode")
  {
    const auto editTool = findToolDefinition("entity_create");
    const auto polygonBatchTool = findToolDefinition("brush_create_polygon_batch");
    const auto metadataSetTool = findToolDefinition("brush_metadata_set");
    const auto metadataGetTool = findToolDefinition("brush_metadata_get");
    const auto routeGeometryTool = findToolDefinition("route_geometry_analyze_chain");
    const auto kzDistanceTool = findToolDefinition("kz_distance_analyze_chain");

    REQUIRE(editTool);
    REQUIRE(polygonBatchTool);
    REQUIRE(metadataSetTool);
    REQUIRE(metadataGetTool);
    REQUIRE(routeGeometryTool);
    REQUIRE(kzDistanceTool);
    CHECK(!canCallTool(*editTool, McpMode::ReadOnly));
    CHECK(canCallTool(*editTool, McpMode::Edit));
    CHECK(!canCallTool(*polygonBatchTool, McpMode::ReadOnly));
    CHECK(canCallTool(*polygonBatchTool, McpMode::Edit));
    CHECK(!canCallTool(*metadataSetTool, McpMode::ReadOnly));
    CHECK(canCallTool(*metadataSetTool, McpMode::Edit));
    CHECK(canCallTool(*metadataGetTool, McpMode::ReadOnly));
    CHECK(canCallTool(*routeGeometryTool, McpMode::ReadOnly));
    CHECK(canCallTool(*kzDistanceTool, McpMode::ReadOnly));
  }

  SECTION("route metadata tools are modeling-visible and KZ alias is searchable")
  {
    const auto editTools = toolsListJson(McpMode::Edit, true, McpToolProfile::Modeling);
    auto editNames = QStringList{};
    for (const auto& tool : editTools)
    {
      editNames.push_back(tool.toObject().value("name").toString());
    }
    CHECK(editNames.contains("shape_library_list"));
    CHECK(editNames.contains("brush_create_polygon_batch"));
    CHECK(editNames.contains("brush_metadata_set"));
    CHECK(editNames.contains("brush_metadata_get"));
    CHECK(editNames.contains("selection_by_metadata"));
    CHECK(editNames.contains("route_geometry_analyze_chain"));
    CHECK(!editNames.contains("kz_distance_analyze_chain"));

    const auto searchResults = toolsSearchJson(
      "route geometry", "route", "schema", McpMode::Edit, McpToolProfile::Modeling);
    auto found = QJsonObject{};
    for (const auto& tool : searchResults)
    {
      const auto object = tool.toObject();
      if (object.value("name").toString() == "route_geometry_analyze_chain")
      {
        found = object;
        break;
      }
    }

    REQUIRE(!found.isEmpty());
    CHECK(found.value("visibleInCurrentProfile").toBool());
    CHECK(found.value("inputSchema")
            .toObject()
            .value("properties")
            .toObject()
            .contains("routeId"));

    const auto aliasResults = toolsSearchJson(
      "kz_distance_analyze_chain", "", "schema", McpMode::Edit, McpToolProfile::Modeling);
    auto alias = QJsonObject{};
    for (const auto& tool : aliasResults)
    {
      const auto object = tool.toObject();
      if (object.value("name").toString() == "kz_distance_analyze_chain")
      {
        alias = object;
        break;
      }
    }

    REQUIRE(!alias.isEmpty());
    CHECK(!alias.value("visibleInCurrentProfile").toBool());
  }

  SECTION("tool json uses MCP inputSchema shape")
  {
    const auto tool = findToolDefinition("map_search");

    REQUIRE(tool);
    const auto json = toMcpToolJson(*tool);

    CHECK(json.value("name").toString() == "map_search");
    CHECK(json.value("inputSchema").toObject().value("type").toString() == "object");
  }

  SECTION("overlay_set schema exposes marker inputs")
  {
    const auto tool = findToolDefinition("overlay_set");

    REQUIRE(tool);
    const auto properties = tool->inputSchema.value("properties").toObject();

    CHECK(properties.contains("highlightObjectIds"));
    CHECK(properties.contains("labels"));
    CHECK(properties.contains("pointMarkers"));
    CHECK(properties.contains("boundsMarkers"));
  }

  SECTION("tool diagnostics include unsupported roadmap tools")
  {
    const auto diagnostics = toolDiagnosticsJson(McpMode::Edit);
    auto archDiagnostic = QJsonObject{};
    for (const auto& entry : diagnostics)
    {
      const auto object = entry.toObject();
      if (object.value("name").toString() == "brush_create_arch")
      {
        archDiagnostic = object;
        break;
      }
    }

    REQUIRE(!archDiagnostic.isEmpty());
    CHECK(archDiagnostic.value("requiredMode").toString() == "Edit");
    CHECK(archDiagnostic.value("availableInCurrentMode").toBool());
    CHECK(archDiagnostic.value("mutatesDocument").toBool());
    CHECK_FALSE(archDiagnostic.value("implemented").toBool());
  }
}

} // namespace tb::mcp
