#!/usr/bin/env python3
"""Generate a whitebox cave pass IR."""

from __future__ import annotations

import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR.parent / "lib"))

from ir_builder import (  # noqa: E402
    DEFAULT_MATERIAL,
    boolean,
    entity,
    integer,
    make_ir,
    number,
    op_box,
    run_recipe_cli,
    string,
)


MANIFEST = {
    "id": "cave_pass",
    "name": "Cave pass",
    "version": "1.0.0",
    "summary": "Generate a whitebox cave corridor with walkable path, side walls, ceiling bands, rock blocks, markers, spawn, and light.",
    "defaultParams": {
        "moduleId": "recipe-cave-pass",
        "routeId": "recipe-cave-pass",
        "length": 1024,
        "width": 160,
        "height": 32,
        "wallHeight": 192,
        "wallThickness": 96,
        "ceilingThickness": 48,
        "segmentCount": 5,
        "rockCount": 6,
        "grid": 16,
        "material": "__TB_empty",
        "rockMaterial": "__TB_empty",
        "includeEntities": True,
    },
    "parameters": [
        {"name": "moduleId", "type": "string", "required": True},
        {"name": "routeId", "type": "string", "required": True},
        {"name": "length", "type": "number", "min": 256},
        {"name": "width", "type": "number", "min": 64},
        {"name": "height", "type": "number", "min": 1},
        {"name": "wallHeight", "type": "number", "min": 64},
        {"name": "wallThickness", "type": "number", "min": 16},
        {"name": "ceilingThickness", "type": "number", "min": 16},
        {"name": "segmentCount", "type": "integer", "min": 2},
        {"name": "rockCount", "type": "integer", "min": 0},
        {"name": "grid", "type": "integer", "min": 1},
        {"name": "material", "type": "string"},
        {"name": "rockMaterial", "type": "string"},
        {"name": "includeEntities", "type": "boolean"},
    ],
    "output": {
        "moduleIdParam": "moduleId",
        "routeIdParam": "routeId",
        "requiredParts": ["path", "left_wall", "right_wall", "ceiling"],
        "parts": [
            "path",
            "left_wall",
            "right_wall",
            "ceiling",
            "rock",
            "start_marker",
            "finish_marker",
        ],
        "routeLike": True,
    },
    "expectedWarnings": [
        "This is a whitebox cave pass, not organic rock generation; accept only after review output reads like a cave."
    ],
    "recommendedValidation": [
        "ir_compile_preview_from_file",
        "ir_apply_from_file",
        "module_inspect",
        "selector_preview(selector={moduleId, part:'path'})",
        "geometry_analyze_route_continuity(orderBy:'metadataOrder')",
        "map_validate(groupByType:true)",
        "module_render_review",
    ],
}


def build(params: dict) -> dict:
    module_id = string(params, "moduleId", "recipe-cave-pass")
    route_id = string(params, "routeId", module_id)
    material = string(params, "material", DEFAULT_MATERIAL)
    rock_material = string(params, "rockMaterial", material)
    grid = integer(params, "grid", 16)
    length = number(params, "length", 1024)
    width = number(params, "width", 160)
    height = number(params, "height", 32)
    wall_height = number(params, "wallHeight", 192)
    wall_thickness = number(params, "wallThickness", 96)
    ceiling_thickness = number(params, "ceilingThickness", 48)
    segment_count = max(2, integer(params, "segmentCount", 5))
    rock_count = max(0, integer(params, "rockCount", 6))
    include_entities = boolean(params, "includeEntities", True)

    half_width = width / 2
    segment_length = length / segment_count
    operations: list[dict] = [
        {
            "type": "path_ribbon",
            "points2d": [[0, 0], [length * 0.3, 48], [length * 0.65, -40], [length, 0]],
            "width": width,
            "minZ": 0,
            "maxZ": height,
            "parts": ["floor"],
            "material": material,
            "metadata": {
                "part": "path",
                "role": "walkable",
                "routeId": route_id,
                "order": 1,
            },
            "partMetadata": {
                "floor": {
                    "part": "path",
                    "role": "walkable",
                    "routeId": route_id,
                    "order": 1,
                }
            },
        }
    ]

    order = 10
    for index in range(segment_count):
        x0 = index * segment_length - wall_thickness
        x1 = (index + 1) * segment_length + wall_thickness
        pinch = 16 * (index % 3)
        ceiling_drop = 16 * ((index + 1) % 3)
        operations.append(
            op_box(
                [x0, half_width - pinch, 0],
                [x1, half_width + wall_thickness, wall_height - ceiling_drop],
                part="left_wall",
                role="boundary",
                order=order,
                material=rock_material,
            )
        )
        order += 1
        operations.append(
            op_box(
                [x0, -half_width - wall_thickness, 0],
                [x1, -half_width + pinch, wall_height - ceiling_drop],
                part="right_wall",
                role="boundary",
                order=order,
                material=rock_material,
            )
        )
        order += 1
        operations.append(
            op_box(
                [x0, -half_width - wall_thickness, wall_height - ceiling_drop],
                [x1, half_width + wall_thickness, wall_height + ceiling_thickness],
                part="ceiling",
                role="boundary",
                order=order,
                material=rock_material,
            )
        )
        order += 1

    for index in range(rock_count):
        x = length * (index + 1) / (rock_count + 1)
        side = -1 if index % 2 else 1
        size = 32 + (index % 3) * 16
        y = side * (half_width - size / 2)
        operations.append(
            op_box(
                [x - size / 2, y - size / 2, height],
                [x + size / 2, y + size / 2, height + size],
                part="rock",
                role="decoration",
                order=100 + index,
                material=rock_material,
            )
        )

    operations.append(
        op_box(
            [-32, -32, height],
            [32, 32, height + 16],
            part="start_marker",
            role="guidance",
            order=1000,
            material=material,
        )
    )
    operations.append(
        op_box(
            [length - 32, -32, height],
            [length + 32, 32, height + 16],
            part="finish_marker",
            role="guidance",
            order=1001,
            material=material,
        )
    )

    entities = []
    if include_entities:
        entities = [
            entity("info_player_start", [0, 0, height + 48]),
            entity("light", [length / 2, 0, wall_height - 24], {"_light": "180 205 255 350"}),
        ]

    return make_ir(
        name="MCP Recipe: Cave pass",
        module_id=module_id,
        route_id=route_id,
        role="walkable",
        material=material,
        grid=grid,
        operations=operations,
        entities=entities,
    )


def main() -> None:
    run_recipe_cli("Generate cave pass TrenchBroom MCP IR", MANIFEST, build)


if __name__ == "__main__":
    main()
