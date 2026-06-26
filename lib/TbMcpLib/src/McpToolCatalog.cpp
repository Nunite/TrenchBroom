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

#include <QJsonDocument>
#include <QStringList>

#include <algorithm>
#include <array>

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

QJsonObject arrayProperty(const QString& description, const QJsonObject& items)
{
  return QJsonObject{
    {"type", "array"},
    {"description", description},
    {"items", items},
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

QJsonObject numberOrVec3Property(const QString& description)
{
  return QJsonObject{
    {"description", description},
    {"oneOf",
     QJsonArray{
       QJsonObject{{"type", "number"}},
       QJsonObject{
         {"type", "array"},
         {"items", QJsonObject{{"type", "number"}}},
         {"minItems", 3},
         {"maxItems", 3},
       },
     }},
  };
}

QJsonObject stringOrVec3Property(const QString& description)
{
  return QJsonObject{
    {"description", description},
    {"oneOf",
     QJsonArray{
       QJsonObject{{"type", "string"}},
       QJsonObject{
         {"type", "array"},
         {"items", QJsonObject{{"type", "number"}}},
         {"minItems", 3},
         {"maxItems", 3},
       },
     }},
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

QJsonObject kzMetadataSchema()
{
  return QJsonObject{
    {"type", "object"},
    {"description",
     "Session-level KZ route metadata. Known keys include routeId, intent, "
     "difficulty, movementType, takeoffEdge, landingWindow, incomingDirection, and "
     "outgoingDirection. Custom session-only keys are allowed for agent probes."},
    {"additionalProperties", true},
    {"properties",
     QJsonObject{
       {"routeId", stringProperty("Route or chain id, e.g. kz_intro_bhop.")},
       {"intent",
        stringProperty("Mapper intent, e.g. redirect_right or precision_land.")},
       {"difficulty", stringProperty("Difficulty label such as easy, average, hard.")},
       {"movementType", stringProperty("KZ movement type such as bhop, LJ, BJ, SBJ.")},
       {"takeoffEdge", stringProperty("Semantic takeoff edge id or label.")},
       {"landingWindow", stringProperty("Semantic landing window id or label.")},
       {"incomingDirection", vec3Property("Incoming route direction vector.")},
       {"outgoingDirection", vec3Property("Outgoing route direction vector.")},
     }},
  };
}

QJsonObject polygonBatchItemSchema()
{
  return objectSchema(
    {
      {"points2d", arrayProperty("Convex 2D footprint points as [x,y].")},
      {"minZ", numberProperty("Minimum platform Z in map units.")},
      {"maxZ", numberProperty("Maximum platform Z in map units.")},
      {"material", stringProperty("Optional per-platform material override.")},
      {"metadata", kzMetadataSchema()},
    },
    {"points2d", "minZ", "maxZ"});
}

QJsonObject blockoutBatchOperationSchema()
{
  return QJsonObject{
    {"type", "object"},
    {"description",
     "Typed Blockout IR operation object. Examples: "
     R"({"type":"box","min":[0,0,0],"max":[128,128,16],"material":"clip"}; )"
     R"({"type":"prism","points2d":[[0,0],[128,0],[64,64]],"minZ":0,"maxZ":64}; )"
     R"({"type":"cylinder_sector","center":[0,0,0],"innerRadius":64,)"
     R"("outerRadius":128,"startAngle":0,"endAngle":90,"minZ":0,"maxZ":16}; )"
     R"({"type":"room","min":[0,0,0],"max":[512,512,128],"thickness":16}; )"
     R"({"type":"corridor","min":[0,0,0],"max":[512,128,128],"thickness":16}; )"
     R"({"type":"curved_corridor","center":[0,0,0],"innerRadius":128,)"
     R"("outerRadius":256,"startAngle":0,"turnDegrees":90,"height":128,)"
     R"("segments":8,"wallThickness":16,"caps":"both"}; )"
     R"({"type":"stairs","min":[0,0,0],"max":[256,128,128],"steps":8,"axis":"x"}; )"
     R"({"type":"ramp","min":[0,0,0],"max":[256,128,64],"axis":"x"}; )"
     R"({"type":"doorway","min":[0,0,0],"max":[256,16,128],)"
     R"("doorMin":[96,0,0],"doorMax":[160,16,96]}; )"
     R"({"type":"cover","min":[0,0,0],"max":[64,32,48]}; )"
     R"({"type":"sky_shell","min":[-512,-512,0],"max":[512,512,256],"thickness":16}.)"},
    {"properties",
     QJsonObject{
       {"type",
        stringProperty(
          "Operation type: box, prism, polyhedron, cylinder_sector, room, corridor, "
          "curved_corridor, stairs, ramp, doorway, cover, or sky_shell.")},
       {"min", vec3Property("Minimum corner for box-like operations.")},
       {"max", vec3Property("Maximum corner for box-like operations.")},
       {"material", stringProperty("Per-operation material override.")},
       {"points2d", arrayProperty("Convex prism footprint points as [x,y].")},
       {"points", arrayProperty("Convex polyhedron points as [x,y,z].")},
       {"minZ", numberProperty("Minimum Z for prism or cylinder_sector.")},
       {"maxZ", numberProperty("Maximum Z for prism or cylinder_sector.")},
       {"center", vec3Property("Center for circular operations.")},
       {"innerRadius",
        numberProperty("Inner radius for cylinder_sector/curved_corridor.")},
       {"outerRadius",
        numberProperty("Outer radius for cylinder_sector/curved_corridor.")},
       {"startAngle", numberProperty("Start angle in degrees.")},
       {"endAngle", numberProperty("End angle in degrees for cylinder_sector.")},
       {"turnDegrees", numberProperty("Arc sweep in degrees for curved_corridor.")},
       {"height", numberProperty("Height for curved_corridor.")},
       {"segments", integerProperty("Segment count for curved_corridor.")},
       {"wallThickness", numberProperty("Wall thickness for curved_corridor.")},
       {"floorThickness", numberProperty("Floor thickness for curved_corridor.")},
       {"ceilingThickness", numberProperty("Ceiling thickness for curved_corridor.")},
       {"caps", stringProperty("curved_corridor caps: none, start, end, or both.")},
       {"steps", integerProperty("Stair step count.")},
       {"axis", stringProperty("Axis for stairs/ramp: x, y, or z.")},
       {"thickness", numberProperty("Shell thickness for room/corridor/sky_shell.")},
       {"doorMin", vec3Property("Door opening minimum corner for doorway.")},
       {"doorMax", vec3Property("Door opening maximum corner for doorway.")},
       {"snapMode", stringProperty("Circular vertex snap mode: grid, radial, or none.")},
     }},
    {"required", QJsonArray{"type"}},
    {"additionalProperties", true},
  };
}

QJsonObject boxBatchItemSchema()
{
  return objectSchema(
    {
      {"min", vec3Property("Box minimum corner in map units.")},
      {"max", vec3Property("Box maximum corner in map units.")},
      {"material", stringProperty("Optional per-box material override.")},
    },
    {"min", "max"});
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
        {"select",
         boolProperty("Replace current selection with matching selectable nodes.")},
        {"excludeWorld",
         boolProperty("Exclude node:world from returned objectIds. Defaults to true.")},
        {"selectableOnly",
         boolProperty(
           "Return only objects that can be selected directly. Defaults to false.")},
        {"leafOnly",
         boolProperty(
           "Return only leaf nodes with no children, useful before delete/transform.")},
        {"exactTypeOnly",
         boolProperty(
           "Require exact node type matching when type is provided. Defaults to true.")},
        {"limit", integerProperty("Maximum result count, defaults to 100.")},
      }),
    },
    {
      "selection_by_bounds",
      "Select leaf selectable objects whose logical bounds intersect or fit inside a "
      "box. Defaults exclude world/container matches.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"min", vec3Property("Bounds minimum corner.")},
          {"max", vec3Property("Bounds maximum corner.")},
          {"mode", stringProperty("Bounds mode: intersects or contains.")},
          {"excludeWorld",
           boolProperty("Exclude node:world. Defaults to true for bounds queries.")},
          {"selectableOnly",
           boolProperty(
             "Return only directly selectable nodes. Defaults to true for bounds "
             "queries.")},
          {"leafOnly",
           boolProperty("Return only leaf nodes. Defaults to true for bounds queries.")},
          {"exactTypeOnly",
           boolProperty("Require exact type matches when type is supplied.")},
          {"detail", stringProperty("summary or full. Defaults to summary.")},
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
      objectSchema({
        {"highlightObjectIds",
         arrayProperty("Object ids to highlight by logical bounds.")},
        {"labels",
         arrayProperty(
           "Label markers. Each item may contain text plus objectId or position.")},
        {"pointMarkers",
         arrayProperty("Point markers with position and optional label.")},
        {"boundsMarkers",
         arrayProperty("Bounds markers with min/max and optional label.")},
      }),
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
      "viewport_capture_current",
      "Capture the current TrenchBroom window as a PNG for MCP visual feedback.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"returnBase64",
         boolProperty("Return PNG data as base64 instead of a temp file path.")},
      }),
    },
    {
      "viewport_capture_3d",
      "Capture a visible 3D map viewport as a PNG for MCP visual feedback.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"returnBase64",
         boolProperty("Return PNG data as base64 instead of a temp file path.")},
      }),
    },
    {
      "viewport_capture_2d",
      "Capture a visible 2D map viewport as a PNG for MCP visual feedback.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"returnBase64",
         boolProperty("Return PNG data as base64 instead of a temp file path.")},
      }),
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
      "entity_create_checked",
      "Create a point entity after checking the active FGD schema supports the "
      "classname. This is the preferred one-step helper over manually calling "
      "fgd_entities_list, entity_schema, and entity_create_from_schema.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"classname", stringProperty("Point entity classname to validate.")},
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
          {"objectIds",
           arrayProperty("Optional brush object ids. Defaults to selection.")},
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
         arrayProperty(
           "Optional brush or brush entity object ids. Defaults to selection.")},
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
      "shape_library_list",
      "List KZ platform footprint grammars for route-aware polygon platforms.",
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
             "tetrahedron, prism, cylinder_sector, or from_planes.")},
          {"min", vec3Property("Minimum corner in map units.")},
          {"max", vec3Property("Maximum corner in map units.")},
          {"sides", integerProperty("Side count for circular primitives.")},
          {"rings", integerProperty("Ring count for UV sphere.")},
          {"iterations", integerProperty("Subdivision iterations for ico sphere.")},
          {"axis", stringProperty("Primitive axis: x, y, or z.")},
          {"thickness", numberProperty("Pipe wall thickness.")},
          {"planes", arrayProperty("Plane definitions for from_planes.")},
          {"points2d", arrayProperty("2D polygon points for prism.")},
          {"minZ", numberProperty("Minimum Z for prism or sector.")},
          {"maxZ", numberProperty("Maximum Z for prism or sector.")},
          {"center", vec3Property("Center point for cylinder sector.")},
          {"innerRadius", numberProperty("Inner radius for cylinder sector.")},
          {"outerRadius", numberProperty("Outer radius for cylinder sector.")},
          {"startAngle", numberProperty("Start angle in degrees.")},
          {"endAngle", numberProperty("End angle in degrees.")},
          {"snapMode",
           stringProperty(
             "XY vertex snap mode for cylinder_sector: grid, radial, or none.")},
          {"grid", numberProperty("Grid size for cylinder_sector snapping.")},
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
      "brush_create_boxes_batch",
      "Create many box brushes in one transaction. Prefer this for platform chains, "
      "jump blocks, and other repeated cuboids instead of many brush_create_box calls.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"boxes",
           arrayProperty(
             "Array of box objects: {\"min\":[x,y,z],\"max\":[x,y,z],"
             "\"material\":\"optional\"}.",
             boxBatchItemSchema())},
          {"name",
           stringProperty("Transaction label, defaults to MCP: Create box brush batch.")},
          {"material",
           stringProperty("Default material for boxes without a material field.")},
          {"grid", numberProperty("Grid size for snapping generated geometry.")},
          {"select", boolProperty("Select generated boxes.")},
          {"detail", stringProperty("summary, ids, or full. Defaults to summary.")},
        },
        {"boxes"}),
    },
    {
      "brush_create_polygon_batch",
      "Create many convex prism platforms from 2D polygons in one transaction. "
      "Prefer this for KZ diamond, trapezoid, chamfered, and route-guiding platforms "
      "instead of box-only chains.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"brushes",
           arrayProperty(
             "Array of platform objects: {points2d:[[x,y],...], minZ, maxZ, "
             "material?, metadata?}.",
             polygonBatchItemSchema())},
          {"grid", numberProperty("Grid size for snapping generated geometry.")},
          {"select", boolProperty("Select generated platforms.")},
          {"transactionName",
           stringProperty(
             "Transaction label, defaults to MCP: Create polygon platform batch.")},
          {"detail", stringProperty("summary, ids, or full. Defaults to summary.")},
          {"material",
           stringProperty("Default material for brushes without a material field.")},
        },
        {"brushes"}),
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
          {"iterations",
           integerProperty("Ico sphere iterations; when present uses ico.")},
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
      "brush_create_prism",
      "Create one convex vertical prism from a 2D polygon and Z range.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"points2d", arrayProperty("Convex 2D polygon points as [x,y].")},
          {"minZ", numberProperty("Minimum Z in map units.")},
          {"maxZ", numberProperty("Maximum Z in map units.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush.")},
        },
        {"points2d", "minZ", "maxZ"}),
    },
    {
      "brush_create_cylinder_sector",
      "Create one convex annular cylinder-sector brush.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"center", vec3Property("Sector center; X/Y are used for the footprint.")},
          {"innerRadius", numberProperty("Inner radius in map units.")},
          {"outerRadius", numberProperty("Outer radius in map units.")},
          {"startAngle", numberProperty("Start angle in degrees.")},
          {"endAngle", numberProperty("End angle in degrees.")},
          {"minZ", numberProperty("Minimum Z in map units.")},
          {"maxZ", numberProperty("Maximum Z in map units.")},
          {"snapMode",
           stringProperty(
             "XY vertex snap mode: grid, radial, or none. Defaults to grid.")},
          {"grid", numberProperty("Grid size for snapping generated geometry.")},
          {"material",
           stringProperty("Material name, defaults to the current material.")},
          {"select", boolProperty("Select the created brush.")},
        },
        {"center",
         "innerRadius",
         "outerRadius",
         "startAngle",
         "endAngle",
         "minZ",
         "maxZ"}),
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
      "operation_inspect",
      "Inspect an MCP operation by operationId. Use detail=summary by default and "
      "detail=ids/full only when object ids or debug data are needed.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"operationId",
           stringProperty("Operation id returned by a mutating MCP tool.")},
          {"detail", stringProperty("summary, ids, or full. Defaults to summary.")},
        },
        {"operationId"}),
    },
    {
      "operation_select",
      "Select live map objects created or changed by an MCP operation.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"operationId",
           stringProperty("Operation id returned by a mutating MCP tool.")},
        },
        {"operationId"}),
    },
    {
      "operation_validate",
      "Return generic validation status for an MCP operation.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"operationId",
           stringProperty("Operation id returned by a mutating MCP tool.")},
        },
        {"operationId"}),
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
      "prefabs_list",
      "List available prefabs. Reserved until TrenchBroom exposes a prefab provider.",
      McpMode::ReadOnly,
      false,
      false,
      objectSchema(),
    },
    {
      "prefab_create",
      "Create a prefab instance. Reserved until TrenchBroom exposes a prefab provider.",
      McpMode::Edit,
      true,
      false,
      objectSchema(
        {
          {"id", stringProperty("Prefab id from prefabs_list.")},
          {"origin", vec3Property("Prefab origin in map units.")},
        },
        {"id"}),
    },
    {
      "textures_list",
      "List loaded materials in the active document. With an empty query, returns the "
      "first N materials plus currentMaterial and fallbackMaterial.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"query", stringProperty("Optional text query for material name or path.")},
        {"limit", integerProperty("Maximum result count, defaults to 200.")},
      }),
    },
    {
      "texture_search",
      "Search loaded materials in the active document. If no material matches, use "
      "fallbackMaterial from the result for safe blockout geometry.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"query", stringProperty("Optional text query for material name or path.")},
        {"limit", integerProperty("Maximum result count, defaults to 50.")},
      }),
    },
    {
      "texture_lock_get",
      "Return current texture lock and UV lock state for modeling operations.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "texture_lock_set",
      "Set texture lock and/or UV lock for subsequent modeling operations.",
      McpMode::Edit,
      false,
      true,
      objectSchema({
        {"textureLock",
         boolProperty("Texture alignment lock, matching Menu/Edit/Texture Lock.")},
        {"uvLock", boolProperty("UV lock, matching Menu/Edit/UV Lock.")},
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
      "texture_apply_by_filter",
      "Apply a material to brushes matched by a safe selection_filter-style query. "
      "The tool only edits brush faces and ignores world/group/layer parents.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"material", stringProperty("Material name to apply.")},
          {"type",
           stringProperty(
             "Optional node type filter. Defaults to brush for safe texture edits.")},
          {"classname", stringProperty("Optional entity classname filter.")},
          {"targetname", stringProperty("Optional entity targetname filter.")},
          {"query", stringProperty("Optional text query.")},
          {"min", vec3Property("Optional bounds minimum corner.")},
          {"max", vec3Property("Optional bounds maximum corner.")},
          {"boundsMode", stringProperty("Bounds mode: intersects or contains.")},
          {"limit", integerProperty("Maximum matched brush count, defaults to 100.")},
        },
        {"material"}),
    },
    {
      "texture_replace",
      "Replace one material with another on selected faces/brushes or the whole map.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"find", stringProperty("Material name to replace.")},
          {"replace", stringProperty("Replacement material name.")},
          {"scope", stringProperty("Replacement scope: selection or map.")},
        },
        {"find", "replace"}),
    },
    {
      "texture_align_face",
      "Apply a basic UV alignment mode to a face or face selection.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"mode", stringProperty("Alignment mode: reset, paraxial, or parallel.")},
          {"objectId", stringProperty("Optional brush object id.")},
          {"faceIndex", integerProperty("Optional face index for objectId brush.")},
        },
        {"mode"}),
    },
    {
      "texture_copy_from_face",
      "Copy material and UV attributes from one face to selected or specified target "
      "faces.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"sourceObjectId", stringProperty("Source brush object id.")},
          {"sourceFaceIndex", integerProperty("Source brush face index.")},
          {"objectId", stringProperty("Optional target brush object id.")},
          {"faceIndex", integerProperty("Optional target face index.")},
        },
        {"sourceObjectId", "sourceFaceIndex"}),
    },
    {
      "face_list",
      "List faces for one brush, selected brushes, or all selected brush faces.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"objectId", stringProperty("Optional brush object id.")},
        {"limit", integerProperty("Maximum result count, defaults to 500.")},
      }),
    },
    {
      "face_select",
      "Select one or more brush faces.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"faces", arrayProperty("Array of {objectId, faceIndex} face references.")},
        {"objectId", stringProperty("Optional single brush object id.")},
        {"faceIndex", integerProperty("Optional single face index.")},
      }),
    },
    {
      "face_texture_set",
      "Set face material and basic UV attributes for selected or specified faces.",
      McpMode::Edit,
      true,
      true,
      objectSchema({
        {"material", stringProperty("Optional material name.")},
        {"xOffset", numberProperty("Optional X texture offset.")},
        {"yOffset", numberProperty("Optional Y texture offset.")},
        {"xScale", numberProperty("Optional X texture scale.")},
        {"yScale", numberProperty("Optional Y texture scale.")},
        {"rotation", numberProperty("Optional texture rotation.")},
        {"objectId", stringProperty("Optional brush object id.")},
        {"faceIndex", integerProperty("Optional face index for objectId brush.")},
      }),
    },
    {
      "objects_delete",
      "Delete one or more entity, brush, patch, group, or layer nodes.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"objectIds", arrayProperty("MCP object ids to delete.")},
        },
        {"objectIds"}),
    },
    {
      "objects_delete_by_filter",
      "Delete selectable objects matched by a safe selection_filter-style query. "
      "Defaults exclude node:world, require selectable matches, and remove redundant "
      "descendants before deleting.",
      McpMode::Edit,
      true,
      true,
      objectSchema({
        {"type",
         stringProperty(
           "Optional node type: layer, group, entity, brush, or patch. Strongly "
           "recommended for destructive calls.")},
        {"classname", stringProperty("Optional entity classname.")},
        {"targetname", stringProperty("Optional entity targetname.")},
        {"material", stringProperty("Optional brush material name.")},
        {"query", stringProperty("Optional text query.")},
        {"min", vec3Property("Optional bounds minimum corner.")},
        {"max", vec3Property("Optional bounds maximum corner.")},
        {"boundsMode", stringProperty("Bounds mode: intersects or contains.")},
        {"leafOnly", boolProperty("Return only leaf matches before deletion.")},
        {"limit", integerProperty("Maximum matched object count, defaults to 100.")},
      }),
    },
    {
      "objects_transform",
      "Transform one or more selectable objects using translate, rotate, or scale.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"objectIds", arrayProperty("MCP object ids to transform.")},
          {"operation",
           stringProperty("Transform operation: translate, rotate, or scale.")},
          {"delta", vec3Property("Translation delta in map units.")},
          {"axis", stringOrVec3Property("Rotation axis: x, y, z, or a [x,y,z] vector.")},
          {"angle", numberProperty("Rotation angle in degrees.")},
          {"scale", numberOrVec3Property("Scale factor number or [x,y,z] factors.")},
          {"center",
           vec3Property("Optional transform center. Defaults to object bounds center.")},
        },
        {"objectIds", "operation"}),
    },
    {
      "map_validate",
      "Validate the active map and return problem counts.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"includeHidden", boolProperty("Include hidden issues.")},
      }),
    },
    {
      "problems_check",
      "Return map validation problems with safe quick fix metadata.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"includeHidden", boolProperty("Include hidden issues.")},
        {"limit", integerProperty("Maximum result count, defaults to 500.")},
      }),
    },
    {
      "problems_fix",
      "Apply one safe quick fix to selected validation problem ids.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"problemIds", arrayProperty("Problem ids from problems_check.")},
          {"quickFix", stringProperty("Safe quick fix description to apply.")},
        },
        {"problemIds", "quickFix"}),
    },
    {
      "map_fix_all_safe",
      "Apply all currently available safe validation quick fixes.",
      McpMode::Edit,
      true,
      true,
      objectSchema({
        {"includeHidden", boolProperty("Include hidden issues.")},
      }),
    },
    {
      "compile_profiles_list",
      "List compilation profiles for the active document.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(),
    },
    {
      "compile_run",
      "Run a compilation profile through TrenchBroom's existing compile dialog.",
      McpMode::Edit,
      false,
      true,
      objectSchema({
        {"profile",
         stringProperty("Compilation profile name. Defaults to the first profile.")},
      }),
    },
    {
      "compile_log_tail",
      "Return recent text from the current compile dialog output.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"maxLines", integerProperty("Maximum lines to return, defaults to 80.")},
      }),
    },
    {
      "leaks_load_pointfile",
      "Load a compiler pointfile (.pts/.lin) into the active document.",
      McpMode::Edit,
      false,
      true,
      objectSchema(
        {
          {"path", stringProperty("Absolute path to the pointfile.")},
        },
        {"path"}),
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
      "blockout_create_batch",
      "Create multiple blockout operations in one transaction. Prefer this over many "
      "atomic brush_create calls for architectural generation.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"name", stringProperty("Transaction label, defaults to MCP: Blockout batch.")},
          {"grid", numberProperty("Grid size for snapping generated geometry.")},
          {"select", boolProperty("Select generated brushes.")},
          {"detail", stringProperty("summary, ids, or full. Defaults to summary.")},
          {"operations",
           arrayProperty(
             "Array of blockout operations. Supported types include box, prism, "
             "polyhedron, cylinder_sector, room, corridor, curved_corridor, stairs, "
             "ramp, doorway, cover, and sky_shell. Each item must be an object with "
             "a type field; use tb_tools_search(detail=schema, query="
             "\"blockout_create_batch operations\") for examples.",
             blockoutBatchOperationSchema())},
        },
        {"operations"}),
    },
    {
      "python_generate_blockout",
      "Run a local Python script in a subprocess to generate Blockout IR, then compile "
      "the returned operations through blockout_create_batch.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"script",
           stringProperty(
             "Python source. It must print a JSON object with an operations array to "
             "stdout.")},
          {"name",
           stringProperty("Transaction label, defaults to MCP: Python blockout.")},
          {"timeoutMs",
           integerProperty("Subprocess timeout in milliseconds. Defaults to 5000.")},
          {"grid", numberProperty("Grid size for snapping generated geometry.")},
          {"select", boolProperty("Select generated brushes.")},
          {"detail", stringProperty("summary, ids, or full. Defaults to summary.")},
          {"material", stringProperty("Default material passed to blockout batch.")},
        },
        {"script"}),
    },
    {
      "heightmap_import_grayscale",
      "Import a local grayscale image as brush terrain. Defaults to terraced_brushes; "
      "adaptive_surface approximates a smoother heightfield with finer brush cells in "
      "more complex areas.",
      McpMode::Edit,
      true,
      true,
      objectSchema(
        {
          {"imagePath", stringProperty("Local image path to read as a heightmap.")},
          {"origin", vec3Property("Terrain minimum origin, defaults to [0,0,0].")},
          {"cellSize", numberProperty("Map units per sampled pixel, defaults to 64.")},
          {"heightScale", numberProperty("Maximum generated height, defaults to 128.")},
          {"heightSteps", integerProperty("Quantization steps, defaults to 8.")},
          {"maxSize",
           integerProperty(
             "Maximum sampled image dimension before downsampling, defaults to 64.")},
          {"maxBrushes",
           integerProperty("Maximum merged brushes to create, defaults to 512.")},
          {"mode",
           stringProperty(
             "Import mode: terraced_brushes or adaptive_surface. Defaults to "
             "terraced_brushes.")},
          {"minCellSize",
           numberProperty(
             "adaptive_surface minimum cell size in map units. Defaults to cellSize.")},
          {"maxCellSize",
           numberProperty(
             "adaptive_surface maximum cell size in map units. Defaults to 4 * "
             "cellSize.")},
          {"errorTolerance",
           numberProperty(
             "adaptive_surface height error tolerance in map units before a cell "
             "subdivides. Lower values create more brushes.")},
          {"material", stringProperty("Brush material, defaults to current material.")},
          {"select", boolProperty("Select generated terrain brushes.")},
          {"detail", stringProperty("summary, ids, or full. Defaults to summary.")},
        },
        {"imagePath"}),
    },
    {
      "blockout_create_curved_corridor",
      "Create a curved corridor as one transaction using floor, ceiling, inner wall, "
      "outer wall, and optional caps.",
      McpMode::Edit,
      true,
      true,
      objectSchema({
        {"center", vec3Property("Corridor center; X/Y define arc origin.")},
        {"innerRadius", numberProperty("Playable inner radius.")},
        {"outerRadius", numberProperty("Playable outer radius.")},
        {"startAngle", numberProperty("Start angle in degrees.")},
        {"turnDegrees", numberProperty("Arc length in degrees.")},
        {"height", numberProperty("Playable height, defaults to 128.")},
        {"segments", integerProperty("Segment count, defaults to 12.")},
        {"wallThickness", numberProperty("Wall thickness, defaults to 16.")},
        {"floorThickness", numberProperty("Floor thickness, defaults to 16.")},
        {"ceilingThickness", numberProperty("Ceiling thickness, defaults to 16.")},
        {"caps", stringProperty("none, start, end, or both. Defaults to none.")},
        {"snapMode",
         stringProperty(
           "XY vertex snap mode: radial, grid, or none. Defaults to radial to keep "
           "arc boundaries visually continuous.")},
        {"grid", numberProperty("Grid size for Z/thickness snapping and grid mode.")},
        {"material", stringProperty("Brush material, defaults to current material.")},
        {"select", boolProperty("Select generated brushes.")},
        {"detail", stringProperty("summary, ids, or full. Defaults to summary.")},
      }),
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
             "IR type: room, corridor, stairs, ramp, doorway, cover, sky_shell, "
             "or spiral_stairs.")},
          {"min", vec3Property("Minimum corner.")},
          {"max", vec3Property("Maximum corner.")},
          {"doorMin", vec3Property("Door opening minimum corner for doorway.")},
          {"doorMax", vec3Property("Door opening maximum corner for doorway.")},
          {"thickness", numberProperty("Optional wall thickness.")},
          {"steps", integerProperty("Optional stair step count.")},
          {"innerRadius", numberProperty("Optional spiral stair inner radius.")},
          {"outerRadius", numberProperty("Optional spiral stair outer radius.")},
          {"stepHeight", numberProperty("Optional spiral stair step height.")},
          {"turnDegrees", numberProperty("Optional spiral stair turn angle.")},
        },
        {"type", "min", "max"}),
    },
    {
      "blockout_create_spiral_stairs",
      "Create deterministic solid spiral stairs with matching center column and landing.",
      McpMode::Edit,
      true,
      true,
      objectSchema({
        {"center", vec3Property("Stair center; X/Y define the rotation origin.")},
        {"innerRadius", numberProperty("Inner radius, defaults to 32.")},
        {"outerRadius", numberProperty("Outer radius, defaults to 128.")},
        {"steps", integerProperty("Step count, defaults to 24.")},
        {"stepHeight", numberProperty("Step height, defaults to 8.")},
        {"startAngle", numberProperty("Start angle in degrees, defaults to 0.")},
        {"turnDegrees", numberProperty("Total turn angle, defaults to 360.")},
        {"clockwise", boolProperty("Rotate clockwise instead of counter-clockwise.")},
        {"baseZ", numberProperty("Base Z, defaults to center.z.")},
        {"column", boolProperty("Create a center column, defaults to true.")},
        {"landing", boolProperty("Create an exit landing, defaults to true.")},
        {"material", stringProperty("Brush material, defaults to current material.")},
        {"select", boolProperty("Select generated brushes.")},
      }),
    },
    {
      "geometry_analyze_selection",
      "Analyze selected brush geometry for vertices, bounds, convexity, grid alignment, "
      "and materials.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"grid", numberProperty("Grid size for alignment checks, defaults to 1.")},
        {"detail", stringProperty("summary, ids, or full. Defaults to summary.")},
        {"maxBrushes",
         integerProperty(
           "Maximum per-brush entries returned for ids/full detail, defaults to 100.")},
        {"includeVertices",
         boolProperty("Include vertex arrays for detail=full, defaults to false.")},
      }),
    },
    {
      "brush_metadata_set",
      "Attach session-level KZ route metadata to live brush object ids.",
      McpMode::Edit,
      false,
      true,
      objectSchema(
        {
          {"objectIds", arrayProperty("Brush object ids to annotate.")},
          {"metadata", kzMetadataSchema()},
        },
        {"objectIds", "metadata"}),
    },
    {
      "brush_metadata_get",
      "Read session-level KZ route metadata for brush object ids.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema(
        {
          {"objectIds", arrayProperty("Brush object ids to inspect.")},
        },
        {"objectIds"}),
    },
    {
      "selection_by_metadata",
      "Find or select brushes by session-level KZ route metadata.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"routeId", stringProperty("Optional route id to match exactly.")},
        {"intent", stringProperty("Optional intent to match exactly.")},
        {"difficulty", stringProperty("Optional difficulty to match exactly.")},
        {"movementType", stringProperty("Optional movement type to match exactly.")},
        {"metadata", kzMetadataSchema()},
        {"select", boolProperty("Replace selection with matched live brush nodes.")},
        {"limit", integerProperty("Maximum result count, defaults to 100.")},
      }),
    },
    {
      "kz_distance_analyze_chain",
      "Compatibility alias for route_geometry_analyze_chain. Returns geometric route "
      "facts only; difficulty should be judged by the Agent using project/domain "
      "context.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"objectIds", arrayProperty("Optional ordered brush object ids to analyze.")},
        {"routeId",
         stringProperty("Optional metadata routeId. Used when objectIds is omitted.")},
        {"movementType", stringProperty("Optional movement type such as bhop or LJ.")},
        {"playerHull",
         vec3Property(
           "Optional player hull size [width, depth, height], defaults to [32,32,72].")},
      }),
    },
    {
      "route_geometry_analyze_chain",
      "Analyze ordered brush-platform geometry facts such as edge gap, effective "
      "distance, height delta, lateral offset, and landing window area. This tool "
      "does not classify gameplay difficulty or pass/fail viability.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"objectIds", arrayProperty("Optional ordered brush object ids to analyze.")},
        {"routeId",
         stringProperty("Optional metadata routeId. Used when objectIds is omitted.")},
        {"movementType",
         stringProperty("Optional movement label used only for result context.")},
        {"playerHull",
         vec3Property(
           "Optional player hull size [width, depth, height], defaults to [32,32,72].")},
      }),
    },
    {
      "blockout_validate_spiral_stairs",
      "Validate selected spiral-stair brushes generated through MCP.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"operationId", stringProperty("Optional MCP operation id to validate.")},
        {"center", vec3Property("Optional expected stair center.")},
        {"innerRadius", numberProperty("Optional expected inner radius.")},
        {"outerRadius", numberProperty("Optional expected outer radius.")},
        {"steps", integerProperty("Optional expected step count.")},
        {"stepHeight", numberProperty("Optional expected step height.")},
        {"turnDegrees", numberProperty("Optional expected turn angle.")},
        {"grid", numberProperty("Grid size for alignment checks, defaults to 1.")},
      }),
    },
    {
      "tb_tools_search",
      "Search available TrenchBroom MCP tools by name, category, or description. Use "
      "detail=schema when exact parameters are needed.",
      McpMode::ReadOnly,
      false,
      true,
      objectSchema({
        {"query", stringProperty("Optional text query.")},
        {"category", stringProperty("Optional category such as blockout or brush.")},
        {"detail", stringProperty("summary or schema. Defaults to summary.")},
      }),
    },
  };

  static const auto CatalogWithMetadata = [&] {
    auto tools = Catalog;
    for (auto& tool : tools)
    {
      if (tool.name.startsWith("brush_create"))
      {
        tool.category = "brush";
        tool.expert = true;
        tool.minimumProfile = McpToolProfile::Full;
        if (
          tool.name == "brush_create_boxes_batch"
          || tool.name == "brush_create_polygon_batch")
        {
          tool.expert = false;
          tool.minimumProfile = McpToolProfile::Modeling;
        }
      }
      else if (tool.name.startsWith("blockout_"))
      {
        tool.category = "blockout";
        tool.minimumProfile = McpToolProfile::Core;
      }
      else if (tool.name.startsWith("operation_") || tool.name.startsWith("history_"))
      {
        tool.category = "operation";
        tool.minimumProfile = McpToolProfile::Core;
      }
      else if (tool.name.startsWith("selection_") || tool.name.startsWith("viewport_"))
      {
        tool.category = "selection";
      }
      else if (tool.name.startsWith("asset_"))
      {
        tool.category = "asset";
      }
      else if (tool.name.startsWith("objects_"))
      {
        tool.category = "object";
      }
      else if (tool.name.startsWith("texture_") || tool.name.startsWith("face_"))
      {
        tool.category = "texture";
      }
      else if (tool.name.startsWith("entity_") || tool.name.startsWith("fgd_"))
      {
        tool.category = "entity";
      }
      else if (tool.name.startsWith("python_"))
      {
        tool.category = "python";
      }
      else if (tool.name.startsWith("heightmap_"))
      {
        tool.category = "heightmap";
        tool.minimumProfile = McpToolProfile::Modeling;
      }
      else if (
        tool.name == "shape_library_list" || tool.name.startsWith("brush_metadata_")
        || tool.name == "selection_by_metadata"
        || tool.name == "route_geometry_analyze_chain")
      {
        tool.category = "route";
        tool.minimumProfile = McpToolProfile::Modeling;
      }
      else if (tool.name == "kz_distance_analyze_chain")
      {
        tool.category = "route";
        tool.expert = true;
        tool.minimumProfile = McpToolProfile::Full;
      }
      else if (
        tool.name == "tb_status" || tool.name == "tb_doctor"
        || tool.name == "tb_tools_search")
      {
        tool.category = "core";
        tool.minimumProfile = McpToolProfile::Core;
      }
    }
    return tools;
  }();

  return CatalogWithMetadata;
}

QString toolProfileName(const McpToolProfile profile)
{
  switch (profile)
  {
  case McpToolProfile::Core:
    return "Core";
  case McpToolProfile::Modeling:
    return "Modeling";
  case McpToolProfile::Balanced:
    return "Balanced";
  case McpToolProfile::Full:
    return "Full";
  }
  return "Modeling";
}

std::optional<McpToolProfile> parseToolProfile(const QString& profile)
{
  const auto normalized = profile.trimmed().toLower();
  if (normalized == "core")
  {
    return McpToolProfile::Core;
  }
  if (normalized == "modeling")
  {
    return McpToolProfile::Modeling;
  }
  if (normalized == "balanced")
  {
    return McpToolProfile::Balanced;
  }
  if (normalized == "full")
  {
    return McpToolProfile::Full;
  }
  return std::nullopt;
}

int profileRank(const McpToolProfile profile)
{
  switch (profile)
  {
  case McpToolProfile::Core:
    return 0;
  case McpToolProfile::Modeling:
    return 1;
  case McpToolProfile::Balanced:
    return 2;
  case McpToolProfile::Full:
    return 3;
  }
  return 1;
}

bool visibleInModelingProfile(const McpToolDefinition& tool)
{
  static constexpr auto HiddenToolNames = std::array{
    "actions_list",
    "action_execute",
    "overlay_set",
    "overlay_clear",
    "viewport_focus",
    "viewport_clear_marks",
    "viewport_capture_current",
    "viewport_capture_3d",
    "viewport_capture_2d",
    "kz_distance_analyze_chain",
  };

  return !std::ranges::any_of(
    HiddenToolNames, [&](const auto* toolName) { return tool.name == toolName; });
}

bool visibleInProfile(const McpToolDefinition& tool, const McpToolProfile profile)
{
  if (profile == McpToolProfile::Full)
  {
    return true;
  }
  if (profile == McpToolProfile::Core)
  {
    return tool.name == "tb_status" || tool.name == "tb_doctor"
           || tool.name == "tb_tools_search" || tool.name == "blockout_create_batch"
           || tool.name.startsWith("operation_")
           || tool.name.startsWith("viewport_capture")
           || tool.name == "geometry_analyze_selection"
           || tool.name == "blockout_validate";
  }
  if (profile == McpToolProfile::Modeling)
  {
    return visibleInModelingProfile(tool);
  }
  return !tool.expert && profileRank(profile) >= profileRank(tool.minimumProfile);
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

QJsonObject toMcpToolDiagnosticJson(
  const McpToolDefinition& tool, const McpMode currentMode)
{
  return QJsonObject{
    {"name", tool.name},
    {"requiredMode", modeName(tool.requiredMode)},
    {"availableInCurrentMode", allowsMode(currentMode, tool.requiredMode)},
    {"mutatesDocument", tool.mutatesDocument},
    {"implemented", tool.implemented},
  };
}

QJsonArray toolsListJson(
  const McpMode mode, const bool implementedOnly, const McpToolProfile profile)
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
    if (!visibleInProfile(tool, profile))
    {
      continue;
    }
    result.push_back(toMcpToolJson(tool));
  }
  return result;
}

QJsonArray toolsListJson(const McpMode mode, const bool implementedOnly)
{
  return toolsListJson(mode, implementedOnly, McpToolProfile::Modeling);
}

QJsonArray toolsListJson(const McpMode mode)
{
  return toolsListJson(mode, true, McpToolProfile::Modeling);
}

namespace
{

bool isWeakToolSearchToken(const QString& token)
{
  static constexpr auto WeakTokens = std::array{
    "argument",
    "arguments",
    "format",
    "input",
    "inputs",
    "json",
    "mcp",
    "output",
    "outputs",
    "parameter",
    "parameters",
    "schema",
    "tool",
    "tools",
  };

  return std::ranges::any_of(
    WeakTokens, [&](const auto* weakToken) { return token == weakToken; });
}

QStringList toolSearchTokens(const QString& query)
{
  auto tokens = QStringList{};
  auto token = QString{};

  const auto flushToken = [&] {
    if (token.size() >= 2 && !isWeakToolSearchToken(token))
    {
      tokens.push_back(token);
    }
    token.clear();
  };

  for (const auto ch : query.trimmed().toLower())
  {
    if (ch.isLetterOrNumber())
    {
      token.push_back(ch);
    }
    else
    {
      flushToken();
    }
  }
  flushToken();

  tokens.removeDuplicates();
  return tokens;
}

QString searchableToolText(const McpToolDefinition& tool, const bool includeSchema)
{
  auto expandedName = tool.name;
  expandedName.replace("_", " ");

  auto text =
    QStringList{
      tool.name,
      expandedName,
      tool.description,
      tool.category,
    }
      .join("\n")
      .toLower();

  if (includeSchema)
  {
    text += "\n";
    text +=
      QString::fromUtf8(QJsonDocument{tool.inputSchema}.toJson(QJsonDocument::Compact))
        .toLower();
  }

  return text;
}

bool toolMatchesSearch(
  const McpToolDefinition& tool, const QStringList& tokens, const bool includeSchema)
{
  if (tokens.isEmpty())
  {
    return true;
  }

  const auto text = searchableToolText(tool, includeSchema);
  return std::ranges::all_of(
    tokens, [&](const auto& token) { return text.contains(token); });
}

bool queryContainsExactToolName(const QString& normalizedQuery, const QString& toolName)
{
  const auto normalizedName = toolName.toLower();
  auto start = qsizetype{0};
  while ((start = normalizedQuery.indexOf(normalizedName, start)) >= 0)
  {
    const auto before =
      start == 0 ? QChar{} : normalizedQuery.at(static_cast<qsizetype>(start - 1));
    const auto afterIndex = start + normalizedName.size();
    const auto after =
      afterIndex >= normalizedQuery.size() ? QChar{} : normalizedQuery.at(afterIndex);
    const auto beforeBoundary =
      before.isNull() || (!before.isLetterOrNumber() && before != '_');
    const auto afterBoundary =
      after.isNull() || (!after.isLetterOrNumber() && after != '_');
    if (beforeBoundary && afterBoundary)
    {
      return true;
    }
    ++start;
  }
  return false;
}

} // namespace

QJsonArray toolsSearchJson(
  const QString& query,
  const QString& category,
  const QString& detail,
  const McpMode mode,
  const McpToolProfile profile)
{
  const auto normalizedQuery = query.trimmed().toLower();
  const auto normalizedCategory = category.trimmed().toLower();
  const auto includeSchema =
    detail.trimmed().toLower() == "schema" || detail.trimmed().toLower() == "full";
  const auto tokens = toolSearchTokens(normalizedQuery);

  auto result = QJsonArray{};
  const auto appendTool = [&](const auto& tool) {
    auto object = QJsonObject{
      {"name", tool.name},
      {"description", tool.description},
      {"category", tool.category},
      {"expert", tool.expert},
      {"requiredMode", modeName(tool.requiredMode)},
      {"visibleInCurrentProfile", visibleInProfile(tool, profile)},
    };
    if (includeSchema)
    {
      object.insert("inputSchema", tool.inputSchema);
    }
    result.push_back(std::move(object));
  };

  for (const auto& tool : defaultToolCatalog())
  {
    if (!tool.implemented || !allowsMode(mode, tool.requiredMode))
    {
      continue;
    }
    const auto exactNameMatch = !normalizedQuery.isEmpty()
                                && queryContainsExactToolName(normalizedQuery, tool.name);
    if (!visibleInProfile(tool, profile) && normalizedQuery.isEmpty())
    {
      continue;
    }
    if (
      !exactNameMatch && !normalizedCategory.isEmpty()
      && tool.category.compare(normalizedCategory, Qt::CaseInsensitive) != 0)
    {
      continue;
    }
    if (
      !exactNameMatch && !normalizedQuery.isEmpty()
      && !toolMatchesSearch(tool, tokens, includeSchema))
    {
      continue;
    }

    appendTool(tool);
  }
  return result;
}

QJsonArray toolDiagnosticsJson(const McpMode currentMode)
{
  auto result = QJsonArray{};
  for (const auto& tool : defaultToolCatalog())
  {
    result.push_back(toMcpToolDiagnosticJson(tool, currentMode));
  }
  return result;
}

} // namespace tb::mcp
