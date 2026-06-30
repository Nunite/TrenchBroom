#!/usr/bin/env python3
"""Generate a generic whitebox temple courtyard IR."""

from __future__ import annotations

import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR.parent / "lib"))

from ir_builder import (  # noqa: E402
    DEFAULT_MATERIAL,
    common_arg_parser,
    entity,
    integer,
    load_params,
    make_ir,
    number,
    op_box,
    run_recipe_cli,
    string,
)


MANIFEST = {
    "id": "temple_courtyard",
    "name": "Temple courtyard",
    "version": "1.0.0",
    "summary": "Generate a whitebox courtyard with base, main hall, roof, gate, entry axis, steps, columns, boundary walls, spawn, and lights.",
    "defaultParams": {
        "moduleId": "recipe-temple-courtyard",
        "width": 1024,
        "depth": 768,
        "baseHeight": 48,
        "hallWidth": 448,
        "hallDepth": 320,
        "hallHeight": 256,
        "roofHeight": 64,
        "columnCount": 6,
        "columnSize": 32,
        "grid": 16,
        "material": "__TB_empty",
        "accentMaterial": "__TB_empty",
        "includeEntities": True,
    },
    "parameters": [
        {"name": "moduleId", "type": "string", "required": True},
        {"name": "width", "type": "number", "min": 256},
        {"name": "depth", "type": "number", "min": 256},
        {"name": "baseHeight", "type": "number", "min": 1},
        {"name": "hallWidth", "type": "number", "min": 128},
        {"name": "hallDepth", "type": "number", "min": 128},
        {"name": "hallHeight", "type": "number", "min": 64},
        {"name": "roofHeight", "type": "number", "min": 1},
        {"name": "columnCount", "type": "integer", "min": 2},
        {"name": "columnSize", "type": "number", "min": 8},
        {"name": "grid", "type": "integer", "min": 1},
        {"name": "material", "type": "string"},
        {"name": "accentMaterial", "type": "string"},
        {"name": "includeEntities", "type": "boolean"},
    ],
    "output": {
        "moduleIdParam": "moduleId",
        "requiredParts": [
            "courtyard_base",
            "hall_plinth",
            "main_hall_mass",
            "main_roof",
            "front_gate",
            "entry_path",
            "front_steps",
        ],
        "parts": [
            "courtyard_base",
            "hall_plinth",
            "main_hall_mass",
            "main_roof",
            "front_gate",
            "entry_path",
            "front_steps",
            "front_column",
            "courtyard_column",
            "front_wall",
            "back_wall",
            "left_wall",
            "right_wall",
        ],
        "routeLike": False,
    },
    "expectedWarnings": [],
    "recommendedValidation": [
        "ir_compile_preview_from_file",
        "ir_apply_from_file",
        "module_inspect",
        "selector_preview(selector={moduleId})",
        "map_validate(groupByType:true)",
        "module_render_review",
    ],
}


def build(params: dict) -> dict:
    module_id = string(params, "moduleId", "recipe-temple-courtyard")
    material = string(params, "material", DEFAULT_MATERIAL)
    accent = string(params, "accentMaterial", material)
    grid = integer(params, "grid", 16)
    width = number(params, "width", 1024)
    depth = number(params, "depth", 768)
    base_height = number(params, "baseHeight", 48)
    hall_width = number(params, "hallWidth", 448)
    hall_depth = number(params, "hallDepth", 320)
    hall_height = number(params, "hallHeight", 256)
    roof_height = number(params, "roofHeight", 64)
    column_count = integer(params, "columnCount", 6)
    column_size = number(params, "columnSize", 32)
    include_entities = bool(params.get("includeEntities", True))

    half_w = width / 2
    half_d = depth / 2
    hall_min_x = -hall_width / 2
    hall_max_x = hall_width / 2
    hall_min_y = depth * 0.12
    hall_max_y = hall_min_y + hall_depth
    order = 1
    operations: list[dict] = []

    def add_box(minimum, maximum, part, role, mat=None):
        nonlocal order
        operations.append(
            op_box(
                minimum,
                maximum,
                part=part,
                role=role,
                order=order,
                material=mat or material,
            )
        )
        order += 1

    add_box([-half_w, -half_d, 0], [half_w, half_d, base_height], "courtyard_base", "walkable")
    add_box([hall_min_x - 64, hall_min_y - 64, base_height], [hall_max_x + 64, hall_max_y + 64, base_height + 32], "hall_plinth", "walkable")
    add_box([hall_min_x, hall_min_y, base_height + 32], [hall_max_x, hall_max_y, base_height + hall_height], "main_hall_mass", "structure")
    add_box([hall_min_x - 48, hall_min_y - 48, base_height + hall_height], [hall_max_x + 48, hall_max_y + 48, base_height + hall_height + roof_height], "main_roof", "structure", accent)

    # Gate and entry axis.
    gate_y = -half_d + 96
    add_box([-128, gate_y - 24, base_height], [128, gate_y + 24, base_height + 128], "front_gate", "guidance", accent)
    add_box([-80, gate_y - 32, base_height], [80, hall_min_y, base_height + 16], "entry_path", "guidance", accent)

    # Stair steps.
    step_depth = 32
    for i in range(4):
        z0 = base_height + i * 16
        add_box(
            [-160, hall_min_y - 64 - (i + 1) * step_depth, z0],
            [160, hall_min_y - 64 - i * step_depth, z0 + 16],
            "front_steps",
            "walkable",
            accent,
        )

    # Column rows in front of the hall and along the courtyard edges.
    if column_count < 2:
        column_count = 2
    spacing = hall_width / (column_count - 1)
    for i in range(column_count):
        x = hall_min_x + spacing * i
        add_box(
            [x - column_size / 2, hall_min_y - 48, base_height + 32],
            [x + column_size / 2, hall_min_y - 48 + column_size, base_height + hall_height],
            "front_column",
            "structure",
            accent,
        )

    for x in (-half_w + 96, half_w - 96):
        for y in (-half_d + 192, -half_d + 384, half_d - 192):
            add_box(
                [x - column_size / 2, y - column_size / 2, base_height],
                [x + column_size / 2, y + column_size / 2, base_height + 144],
                "courtyard_column",
                "structure",
                accent,
            )

    # Low boundary walls.
    wall_h = 96
    wall_t = 24
    add_box([-half_w, -half_d, base_height], [half_w, -half_d + wall_t, base_height + wall_h], "front_wall", "boundary")
    add_box([-half_w, half_d - wall_t, base_height], [half_w, half_d, base_height + wall_h], "back_wall", "boundary")
    add_box([-half_w, -half_d, base_height], [-half_w + wall_t, half_d, base_height + wall_h], "left_wall", "boundary")
    add_box([half_w - wall_t, -half_d, base_height], [half_w, half_d, base_height + wall_h], "right_wall", "boundary")

    entities = []
    if include_entities:
        entities = [
            entity("info_player_start", [0, -half_d + 160, base_height + 32]),
            entity("light", [0, 0, 512], {"_light": "255 230 200 450"}),
            entity("light", [0, hall_min_y + hall_depth / 2, base_height + hall_height + 160], {"_light": "255 230 200 350"}),
        ]

    return make_ir(
        name="MCP Recipe: Temple courtyard",
        module_id=module_id,
        material=material,
        grid=grid,
        operations=operations,
        entities=entities,
        role="structure",
    )


def main() -> None:
    run_recipe_cli("Generate temple courtyard TrenchBroom MCP IR", MANIFEST, build)


if __name__ == "__main__":
    main()
