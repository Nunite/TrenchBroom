#!/usr/bin/env python3
"""Generate a generic terrain pass / canyon route IR."""

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
    "id": "terrain_pass",
    "name": "Terrain pass",
    "version": "1.0.0",
    "summary": "Generate a whitebox terrain pass with a walkable ribbon, side cliffs, rock blocks, markers, spawn, and light.",
    "defaultParams": {
        "moduleId": "recipe-terrain-pass",
        "routeId": "recipe-terrain-pass",
        "length": 1024,
        "width": 160,
        "height": 24,
        "cliffHeight": 192,
        "cliffThickness": 96,
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
        {"name": "cliffHeight", "type": "number", "min": 32},
        {"name": "cliffThickness", "type": "number", "min": 16},
        {"name": "rockCount", "type": "integer", "min": 0},
        {"name": "grid", "type": "integer", "min": 1},
        {"name": "material", "type": "string"},
        {"name": "rockMaterial", "type": "string"},
        {"name": "includeEntities", "type": "boolean"},
    ],
    "output": {
        "moduleIdParam": "moduleId",
        "routeIdParam": "routeId",
        "requiredParts": ["path", "left_cliff", "right_cliff"],
        "parts": ["path", "left_cliff", "right_cliff", "rock", "start_marker", "finish_marker"],
        "routeLike": True,
    },
    "expectedWarnings": [
        "This recipe is a whitebox terrain pass; use review renders for visual quality and route validation for walkability."
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
    module_id = string(params, "moduleId", "recipe-terrain-pass")
    route_id = string(params, "routeId", module_id)
    material = string(params, "material", DEFAULT_MATERIAL)
    rock_material = string(params, "rockMaterial", material)
    grid = integer(params, "grid", 16)
    length = number(params, "length", 1024)
    width = number(params, "width", 160)
    height = number(params, "height", 24)
    cliff_height = number(params, "cliffHeight", 192)
    cliff_thickness = number(params, "cliffThickness", 96)
    rock_count = max(0, integer(params, "rockCount", 6))
    include_entities = boolean(params, "includeEntities", True)

    half_width = width / 2
    operations: list[dict] = [
        {
            "type": "path_ribbon",
            "points2d": [[0, 0], [length * 0.35, 64], [length * 0.7, -48], [length, 0]],
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

    operations.append(
        op_box(
            [-cliff_thickness, half_width, 0],
            [length + cliff_thickness, half_width + cliff_thickness, cliff_height],
            part="left_cliff",
            role="boundary",
            order=10,
            material=rock_material,
        )
    )
    operations.append(
        op_box(
            [-cliff_thickness, -half_width - cliff_thickness, 0],
            [length + cliff_thickness, -half_width, cliff_height],
            part="right_cliff",
            role="boundary",
            order=11,
            material=rock_material,
        )
    )

    for index in range(rock_count):
        x = length * (index + 1) / (rock_count + 1)
        side = -1 if index % 2 else 1
        size = 32 + (index % 3) * 16
        y = side * (half_width + 24 + size / 2)
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
            entity("light", [length / 2, 0, cliff_height + 160], {"_light": "220 240 255 450"}),
        ]

    return make_ir(
        name="MCP Recipe: Terrain pass",
        module_id=module_id,
        route_id=route_id,
        role="walkable",
        material=material,
        grid=grid,
        operations=operations,
        entities=entities,
    )


def main() -> None:
    run_recipe_cli("Generate terrain pass TrenchBroom MCP IR", MANIFEST, build)


if __name__ == "__main__":
    main()
