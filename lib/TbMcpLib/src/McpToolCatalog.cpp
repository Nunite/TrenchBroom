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

#include "mcp/McpToolCatalog.h"

#include <algorithm>

namespace tb::mcp
{
namespace
{

QJsonObject objectSchema(QJsonObject properties = {}, QJsonArray required = {})
{
  return QJsonObject{
    {"type", "object"},
    {"properties", properties},
    {"required", required},
    {"additionalProperties", false},
  };
}

QJsonObject stringProperty(const QString& description)
{
  return QJsonObject{
    {"type", "string"},
    {"description", description},
  };
}

QJsonObject numberProperty(const QString& description)
{
  return QJsonObject{
    {"type", "number"},
    {"description", description},
  };
}

QJsonObject integerProperty(const QString& description)
{
  return QJsonObject{
    {"type", "integer"},
    {"description", description},
  };
}

QJsonObject boolProperty(const QString& description)
{
  return QJsonObject{
    {"type", "boolean"},
    {"description", description},
  };
}

QJsonObject arrayProperty(const QString& description)
{
  return QJsonObject{
    {"type", "array"},
    {"description", description},
  };
}

QJsonObject vec3Property(const QString& description)
{
  return QJsonObject{
    {"type", "array"},
    {"description", description},
    {"items", QJsonObject{{"type", "number"}}},
    {"minItems", 3},
    {"maxItems", 3},
  };
}

QJsonObject stringObjectProperty(const QString& description)
{
  return QJsonObject{
    {"type", "object"},
    {"description", description},
    {"additionalProperties", QJsonObject{{"type", "string"}}},
  };
}

} // namespace

const std::vector<McpToolDefinition>& defaultToolCatalog()
{
  static const auto Catalog = std::vector<McpToolDefinition>{
    {
      "tb_status",
      "Return TrenchBroom MCP bridge status and active document summary.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "tb_doctor",
      "Diagnose MCP bridge configuration, mode, token presence, and tool availability.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "documents_list",
      "List documents currently opened in TrenchBroom.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "documents_open",
      "Open a map document by absolute path.",
      McpMode::Edit,
      false,
      true,
      objectSchema(
        {
          {"path", stringProperty("Absolute path to the map document.")},
        },
        {"path"}),
    },
    {
      "documents_activate",
      "Activate an open document by document index or path.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"index", integerProperty("Document index from documents_list.")},
        {"path", stringProperty("Document path from documents_list.")},
      }),
    },
    {
      "documents_save",
      "Save an open document. Transient documents require an absolute path.",
      McpMode::Edit,
      false,
      true,
      objectSchema({
        {"index", integerProperty("Optional document index from documents_list.")},
        {"path", stringProperty("Optional absolute save path.")},
      }),
    },
    {
      "documents_close",
      "Close an open document. Dirty documents require discardChanges=true.",
      McpMode::Edit,
      false,
      true,
      objectSchema({
        {"index", integerProperty("Optional document index from documents_list.")},
        {"discardChanges", boolProperty("Allow closing a modified document.")},
      }),
    },
    {
      "documents_export",
      "Export an open document to a map file by absolute path.",
      McpMode::Edit,
      false,
      true,
      objectSchema(
        {
          {"index", integerProperty("Optional document index from documents_list.")},
          {"path", stringProperty("Absolute export path.")},
          {"stripTbProperties",
           boolProperty("Strip TrenchBroom-specific properties from exported map.")},
        },
        {"path"}),
    },
    {
      "document_snapshot",
      "Return metadata for the active document.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "map_snapshot",
      "Return a compact map summary for the active document.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "map_search",
      "Search entities, brushes, and properties in the active map.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"query", stringProperty("Text, classname, targetname, or property query.")},
        },
        {"query"}),
    },
    {
      "selection_get",
      "Return the current editor selection.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "selection_set",
      "Set the current editor selection using MCP object ids.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"objectIds", arrayProperty("MCP object ids to select.")},
        },
        {"objectIds"}),
    },
    {
      "selection_filter",
      "Filter map objects by type, classname, targetname, material, bounds, or text.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"type",
         stringProperty(
           "Optional node type: world, layer, group, entity, brush, or patch.")},
        {"classname", stringProperty("Optional entity classname.")},
        {"targetname", stringProperty("Optional entity targetname.")},
        {"material", stringProperty("Optional brush material name.")},
        {"query", stringProperty("Optional text query.")},
        {"min", vec3Property("Optional bounds minimum corner.")},
        {"max", vec3Property("Optional bounds maximum corner.")},
        {"boundsMode", stringProperty("Bounds mode: intersects or contains.")},
        {"select", boolProperty("Replace current selection with matching selectable nodes.")},
        {"limit", integerProperty("Maximum result count, defaults to 100.")},
      }),
    },
    {
      "selection_by_bounds",
      "Select objects whose logical bounds intersect or fit inside a box.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"min", vec3Property("Bounds minimum corner.")},
          {"max", vec3Property("Bounds maximum corner.")},
          {"mode", stringProperty("Bounds mode: intersects or contains.")},
        },
        {"min", "max"}),
    },
    {
      "selection_grow",
      "Grow the current node selection to parents, children, or siblings.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"mode", stringProperty("Growth mode: parents, children, or siblings.")},
      }),
    },
    {
      "viewport_focus",
      "Focus the editor viewport on object ids or the current selection.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"objectIds", arrayProperty("Optional MCP object ids to focus.")},
      }),
    },
    {
      "viewport_clear_marks",
      "Clear MCP overlay markers, optionally clearing the editor selection.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"clearSelection", boolProperty("Also clear the current editor selection.")},
      }),
    },
    {
      "actions_list",
      "List executable TrenchBroom actions for the current context.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "action_execute",
      "Execute a TrenchBroom action by id if it is enabled in the current context.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"actionId", stringProperty("Action identifier from actions_list.")},
        },
        {"actionId"}),
    },
    {
      "overlay_set",
      "Set MCP overlay labels or highlighted object ids.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "overlay_clear",
      "Clear MCP overlay state.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "entity_create",
      "Create an entity in the active document.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"classname", stringProperty("Entity classname.")},
          {"origin", vec3Property("Entity origin in map units.")},
          {"properties", stringObjectProperty("Entity key/value properties.")},
          {"select", boolProperty("Select the created entity.")},
        },
        {"classname"}),
    },
    {
      "entity_update",
      "Update entity key/value properties.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"objectId", stringProperty("MCP object id for a world or entity node.")},
          {"properties", stringObjectProperty("Properties to add or update.")},
          {"removeKeys", arrayProperty("Property keys to remove.")},
        },
        {"objectId"}),
    },
    {
      "entity_delete",
      "Delete an entity, brush, patch, group, or layer node.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"objectId", stringProperty("MCP object id to delete.")},
        },
        {"objectId"}),
    },
    {
      "fgd_entities_list",
      "List entity classes from the active game entity definitions.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"type", stringProperty("Optional entity definition type: point or brush.")},
        {"query", stringProperty("Optional classname or description query.")},
        {"limit", integerProperty("Maximum result count, defaults to 200.")},
      }),
    },
    {
      "entity_schema",
      "Return FGD schema details for one entity classname.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"classname", stringProperty("Entity classname.")},
        },
        {"classname"}),
    },
    {
      "entity_create_from_schema",
      "Create an entity using FGD defaults plus supplied properties.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"classname", stringProperty("Point entity classname.")},
          {"origin", vec3Property("Entity origin in map units.")},
          {"properties", stringObjectProperty("Properties to add or update.")},
          {"select", boolProperty("Select the created entity.")},
        },
        {"classname"}),
    },
    {
      "entity_tie_brushes",
      "Tie selected or specified brushes to a brush entity.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"classname", stringProperty("Brush entity classname.")},
          {"objectIds", arrayProperty("Optional brush object ids. Defaults to selection.")},
        },
        {"classname"}),
    },
    {
      "entity_untie_brushes",
      "Move brushes out of brush entities back to the world or current parent.",
      McpMode::Edit,
      true,
      true,
      objectSchema({
        {"objectIds",
         arrayProperty("Optional brush or brush entity object ids. Defaults to selection.")},
      }),
    },
    {
      "brush_types_list",
      "List brush primitive types supported by MCP.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "brush_create",
      "Create a brush primitive using a type parameter.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"type",
           stringProperty(
             "Primitive type: box, wedge, cylinder, cone, pipe, sphere, pyramid, "
             "tetrahedron, or from_planes.")},
          {"min", vec3Property("Minimum corner in map units.")},
          {"max", vec3Property("Maximum corner in map units.")},
          {"sides", integerProperty("Side count for circular primitives.")},
          {"rings", integerProperty("Ring count for UV sphere.")},
          {"iterations", integerProperty("Subdivision iterations for ico sphere.")},
          {"axis", stringProperty("Primitive axis: x, y, or z.")},
          {"thickness", numberProperty("Pipe wall thickness.")},
          {"planes", arrayProperty("Plane definitions for from_planes.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush or brushes.")},
        },
        {"type"}),
    },
    {
      "brush_create_box",
      "Create a box brush in the active document.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Minimum corner in map units.")},
          {"max", vec3Property("Maximum corner in map units.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush.")},
        },
        {"min", "max"}),
    },
    {
      "brush_create_wedge",
      "Create a wedge brush in the active document.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Minimum corner in map units.")},
          {"max", vec3Property("Maximum corner in map units.")},
          {"axis", stringProperty("Ramp direction axis: x, y, or z.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush.")},
        },
        {"min", "max"}),
    },
    {
      "brush_create_cylinder",
      "Create a cylinder brush in the active document.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Minimum corner in map units.")},
          {"max", vec3Property("Maximum corner in map units.")},
          {"sides", integerProperty("Cylinder side count, defaults to 16.")},
          {"axis", stringProperty("Cylinder axis: x, y, or z.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush.")},
        },
        {"min", "max"}),
    },
    {
      "brush_create_cone",
      "Create a cone brush in the active document.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Minimum corner in map units.")},
          {"max", vec3Property("Maximum corner in map units.")},
          {"sides", integerProperty("Base side count, defaults to 16.")},
          {"axis", stringProperty("Cone axis: x, y, or z.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush.")},
        },
        {"min", "max"}),
    },
    {
      "brush_create_pipe",
      "Create a hollow cylinder from multiple convex brushes.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Minimum corner in map units.")},
          {"max", vec3Property("Maximum corner in map units.")},
          {"sides", integerProperty("Side count, defaults to 16.")},
          {"thickness", numberProperty("Pipe wall thickness, defaults to 16.")},
          {"axis", stringProperty("Pipe axis: x, y, or z.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select generated brushes.")},
        },
        {"min", "max"}),
    },
    {
      "brush_create_sphere",
      "Create a UV or ico sphere brush.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Minimum corner in map units.")},
          {"max", vec3Property("Maximum corner in map units.")},
          {"sides", integerProperty("UV sphere side count, defaults to 12.")},
          {"rings", integerProperty("UV sphere ring count, defaults to 6.")},
          {"iterations", integerProperty("Ico sphere iterations; when present uses ico.")},
          {"axis", stringProperty("UV sphere axis: x, y, or z.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush.")},
        },
        {"min", "max"}),
    },
    {
      "brush_create_pyramid",
      "Create a square pyramid brush.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Minimum corner in map units.")},
          {"max", vec3Property("Maximum corner in map units.")},
          {"axis", stringProperty("Pyramid axis: x, y, or z.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush.")},
        },
        {"min", "max"}),
    },
    {
      "brush_create_tetrahedron",
      "Create a tetrahedron brush inside the given bounds.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Minimum corner in map units.")},
          {"max", vec3Property("Maximum corner in map units.")},
          {"axis", stringProperty("Tetrahedron axis: x, y, or z.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush.")},
        },
        {"min", "max"}),
    },
    {
      "brush_create_from_planes",
      "Create one expert brush from plane point triples.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"planes", arrayProperty("Array of planes, each with three points.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush.")},
        },
        {"planes"}),
    },
    {
      "brush_create_arch",
      "Create an arch brush primitive. Listed as unsupported until a stable native "
      "arch generator is exposed.",
      McpMode::Edit,
      true,
      false,
      objectSchema(),
    },
    {
      "brush_create_torus",
      "Create a torus brush primitive. Listed as unsupported because BSP brushes cannot "
      "represent it as a single convex brush.",
      McpMode::Edit,
      true,
      false,
      objectSchema(),
    },
    {
      "history_list",
      "List MCP operations recorded for the active bridge session.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "history_undo_mcp",
      "Undo the latest MCP operation if it is still on top of the native undo stack.",
      McpMode::Edit,
      true,
      true,
      objectSchema(),
    },
    {
      "history_redo_mcp",
      "Redo the latest MCP operation undone by history_undo_mcp.",
      McpMode::Edit,
      true,
      true,
      objectSchema(),
    },
    {
      "asset_search",
      "Search GoldSrc model, sprite, and sound assets using the unified asset browser "
      "rules.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"query", stringProperty("Optional text query for asset path or display name.")},
        {"type", stringProperty("Optional asset type: model, sprite, or sound.")},
        {"limit", integerProperty("Maximum result count, defaults to 50.")},
      }),
    },
    {
      "asset_place_model",
      "Create a point entity for a .mdl asset.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"path", stringProperty("Model path from asset_search.")},
          {"origin", vec3Property("Entity origin in map units.")},
          {"classname", stringProperty("Optional classname, defaults to cycler_sprite.")},
          {"property", stringProperty("Optional model property key, defaults to model.")},
          {"select", boolProperty("Select the created entity.")},
        },
        {"path"}),
    },
    {
      "asset_place_sprite",
      "Create a point entity for a .spr asset.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"path", stringProperty("Sprite path from asset_search.")},
          {"origin", vec3Property("Entity origin in map units.")},
          {"classname", stringProperty("Optional classname, defaults to cycler_sprite.")},
          {"property",
           stringProperty("Optional sprite property key, defaults to model.")},
          {"select", boolProperty("Select the created entity.")},
        },
        {"path"}),
    },
    {
      "asset_place_sound",
      "Create an ambient_generic entity for a .wav asset.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"path", stringProperty("Sound path from asset_search.")},
          {"origin", vec3Property("Entity origin in map units.")},
          {"classname",
           stringProperty("Optional classname, defaults to ambient_generic.")},
          {"property",
           stringProperty("Optional sound property key, defaults to message.")},
          {"select", boolProperty("Select the created entity.")},
        },
        {"path"}),
    },
    {
      "texture_search",
      "Search loaded materials in the active document.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"query", stringProperty("Optional text query for material name or path.")},
        {"limit", integerProperty("Maximum result count, defaults to 50.")},
      }),
    },
    {
      "texture_apply",
      "Apply a material to selected faces, selected brushes, or all faces of one brush.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"material", stringProperty("Material name to apply.")},
          {"objectId",
           stringProperty("Optional brush object id. If omitted, uses selection.")},
          {"faceIndex", integerProperty("Optional face index for objectId brush.")},
        },
        {"material"}),
    },
    {
      "blockout_create_room",
      "Create a room from Blockout IR.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Inner room minimum corner.")},
          {"max", vec3Property("Inner room maximum corner.")},
          {"thickness", numberProperty("Wall thickness, defaults to 16.")},
          {"material", stringProperty("Brush material, defaults to current material.")},
          {"select", boolProperty("Select generated brushes.")},
        },
        {"min", "max"}),
    },
    {
      "blockout_create_corridor",
      "Create a rectangular corridor shell from Blockout IR.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Inner corridor minimum corner.")},
          {"max", vec3Property("Inner corridor maximum corner.")},
          {"thickness", numberProperty("Wall thickness, defaults to 16.")},
          {"material", stringProperty("Brush material, defaults to current material.")},
          {"select", boolProperty("Select generated brushes.")},
        },
        {"min", "max"}),
    },
    {
      "blockout_create_stairs",
      "Create box-based stairs from Blockout IR.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Staircase minimum corner.")},
          {"max", vec3Property("Staircase maximum corner.")},
          {"steps", integerProperty("Step count, defaults to 8.")},
          {"axis", stringProperty("Run direction axis: x or y.")},
          {"material", stringProperty("Brush material, defaults to current material.")},
          {"select", boolProperty("Select generated brushes.")},
        },
        {"min", "max"}),
    },
    {
      "blockout_create_ramp",
      "Create a wedge ramp from Blockout IR.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Ramp minimum corner.")},
          {"max", vec3Property("Ramp maximum corner.")},
          {"axis", stringProperty("Run direction axis: x or y.")},
          {"material", stringProperty("Brush material, defaults to current material.")},
          {"select", boolProperty("Select generated brush.")},
        },
        {"min", "max"}),
    },
    {
      "blockout_create_doorway",
      "Create a wall with a rectangular doorway by splitting it into convex boxes.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Wall minimum corner.")},
          {"max", vec3Property("Wall maximum corner.")},
          {"doorMin", vec3Property("Door opening minimum corner.")},
          {"doorMax", vec3Property("Door opening maximum corner.")},
          {"material", stringProperty("Brush material, defaults to current material.")},
          {"select", boolProperty("Select generated brushes.")},
        },
        {"min", "max", "doorMin", "doorMax"}),
    },
    {
      "blockout_create_cover",
      "Create a low cover box from Blockout IR.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Cover minimum corner.")},
          {"max", vec3Property("Cover maximum corner.")},
          {"material", stringProperty("Brush material, defaults to current material.")},
          {"select", boolProperty("Select generated brush.")},
        },
        {"min", "max"}),
    },
    {
      "blockout_create_sky_shell",
      "Create a sky shell around a playable volume.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"min", vec3Property("Inner playable minimum corner.")},
          {"max", vec3Property("Inner playable maximum corner.")},
          {"thickness", numberProperty("Sky shell thickness, defaults to 16.")},
          {"material", stringProperty("Sky material, defaults to sky.")},
          {"select", boolProperty("Select generated brushes.")},
        },
        {"min", "max"}),
    },
    {
      "blockout_validate",
      "Validate Blockout IR dimensions without modifying the map.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"type",
           stringProperty(
             "IR type: room, corridor, stairs, ramp, doorway, cover, sky_shell.")},
          {"min", vec3Property("Minimum corner.")},
          {"max", vec3Property("Maximum corner.")},
          {"doorMin", vec3Property("Door opening minimum corner for doorway.")},
          {"doorMax", vec3Property("Door opening maximum corner for doorway.")},
          {"thickness", numberProperty("Optional wall thickness.")},
          {"steps", integerProperty("Optional stair step count.")},
        },
        {"type", "min", "max"}),
    },
  };

  return Catalog;
}

std::optional<McpToolDefinition> findToolDefinition(const QString& name)
{
  const auto& catalog = defaultToolCatalog();
  const auto it =
    std::ranges::find_if(catalog, [&](const auto& tool) { return tool.name == name; });
  if (it == catalog.end())
  {
    return std::nullopt;
  }
  return *it;
}

bool canCallTool(const McpToolDefinition& tool, const McpMode mode)
{
  return tool.implemented && allowsMode(mode, tool.requiredMode);
}

QJsonObject toMcpToolJson(const McpToolDefinition& tool)
{
  return QJsonObject{
    {"name", tool.name},
    {"description", tool.description},
    {"inputSchema", tool.inputSchema},
  };
}

QJsonArray toolsListJson(const McpMode mode, const bool implementedOnly)
{
  auto result = QJsonArray{};
  for (const auto& tool : defaultToolCatalog())
  {
    if (implementedOnly && !tool.implemented)
    {
      continue;
    }
    if (!allowsMode(mode, tool.requiredMode))
    {
      continue;
    }
    result.push_back(toMcpToolJson(tool));
  }
  return result;
}

} // namespace tb::mcp
