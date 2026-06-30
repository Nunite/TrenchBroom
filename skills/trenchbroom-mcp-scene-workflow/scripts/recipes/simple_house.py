#!/usr/bin/env python3
"""Generate a simple whitebox house IR."""

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
    "id": "simple_house",
    "name": "Simple house",
    "version": "1.0.0",
    "summary": "Generate a small whitebox house from explicit floor, wall segments, door/window frames, roof slabs, spawn, and light.",
    "defaultParams": {
        "moduleId": "recipe-simple-house",
        "width": 512,
        "depth": 384,
        "height": 192,
        "wallThickness": 24,
        "floorThickness": 24,
        "roofThickness": 32,
        "doorWidth": 96,
        "doorHeight": 128,
        "windowWidth": 80,
        "windowHeight": 64,
        "grid": 16,
        "material": "__TB_empty",
        "trimMaterial": "__TB_empty",
        "includeEntities": True,
    },
    "parameters": [
        {"name": "moduleId", "type": "string", "required": True},
        {"name": "width", "type": "number", "min": 192},
        {"name": "depth", "type": "number", "min": 192},
        {"name": "height", "type": "number", "min": 96},
        {"name": "wallThickness", "type": "number", "min": 8},
        {"name": "floorThickness", "type": "number", "min": 8},
        {"name": "roofThickness", "type": "number", "min": 8},
        {"name": "doorWidth", "type": "number", "min": 32},
        {"name": "doorHeight", "type": "number", "min": 48},
        {"name": "windowWidth", "type": "number", "min": 32},
        {"name": "windowHeight", "type": "number", "min": 32},
        {"name": "grid", "type": "integer", "min": 1},
        {"name": "material", "type": "string"},
        {"name": "trimMaterial", "type": "string"},
        {"name": "includeEntities", "type": "boolean"},
    ],
    "output": {
        "moduleIdParam": "moduleId",
        "requiredParts": ["floor", "front_wall", "back_wall", "left_wall", "right_wall", "roof"],
        "parts": [
            "floor",
            "front_wall",
            "back_wall",
            "left_wall",
            "right_wall",
            "roof",
            "door_frame",
            "window_frame",
        ],
        "routeLike": False,
    },
    "expectedWarnings": [
        "This recipe uses explicit segmented walls; it does not CSG-cut openings into existing brushes."
    ],
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
    module_id = string(params, "moduleId", "recipe-simple-house")
    material = string(params, "material", DEFAULT_MATERIAL)
    trim_material = string(params, "trimMaterial", material)
    grid = integer(params, "grid", 16)
    width = number(params, "width", 512)
    depth = number(params, "depth", 384)
    height = number(params, "height", 192)
    wall = number(params, "wallThickness", 24)
    floor_thickness = number(params, "floorThickness", 24)
    roof_thickness = number(params, "roofThickness", 32)
    door_width = min(number(params, "doorWidth", 96), width - wall * 4)
    door_height = min(number(params, "doorHeight", 128), height - wall)
    window_width = min(number(params, "windowWidth", 80), width / 3)
    window_height = min(number(params, "windowHeight", 64), height / 2)
    include_entities = boolean(params, "includeEntities", True)

    half_w = width / 2
    half_d = depth / 2
    front_y = -half_d
    back_y = half_d
    z0 = floor_thickness
    operations: list[dict] = []
    order = 1

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

    add_box([-half_w, -half_d, 0], [half_w, half_d, floor_thickness], "floor", "walkable")

    # Front wall, segmented around the doorway.
    door_half = door_width / 2
    add_box([-half_w, front_y, z0], [-door_half, front_y + wall, height], "front_wall", "boundary")
    add_box([door_half, front_y, z0], [half_w, front_y + wall, height], "front_wall", "boundary")
    add_box([-door_half, front_y, door_height], [door_half, front_y + wall, height], "front_wall", "boundary")

    add_box([-half_w, back_y - wall, z0], [half_w, back_y, height], "back_wall", "boundary")
    add_box([-half_w, front_y, z0], [-half_w + wall, back_y, height], "left_wall", "boundary")
    add_box([half_w - wall, front_y, z0], [half_w, back_y, height], "right_wall", "boundary")

    # Window frames on side walls as explicit trim, not cut geometry.
    win_z0 = z0 + (height - z0 - window_height) / 2
    win_z1 = win_z0 + window_height
    for side, x in (("left", -half_w), ("right", half_w - wall)):
        frame_part = "window_frame"
        frame_x0 = x
        frame_x1 = x + wall
        y0 = -window_width / 2
        y1 = window_width / 2
        add_box([frame_x0, y0 - wall, win_z0 - wall], [frame_x1, y1 + wall, win_z0], frame_part, "detail", trim_material)
        add_box([frame_x0, y0 - wall, win_z1], [frame_x1, y1 + wall, win_z1 + wall], frame_part, "detail", trim_material)
        add_box([frame_x0, y0 - wall, win_z0], [frame_x1, y0, win_z1], frame_part, "detail", trim_material)
        add_box([frame_x0, y1, win_z0], [frame_x1, y1 + wall, win_z1], frame_part, "detail", trim_material)

    add_box([-door_half - wall, front_y - wall, z0], [-door_half, front_y, door_height], "door_frame", "guidance", trim_material)
    add_box([door_half, front_y - wall, z0], [door_half + wall, front_y, door_height], "door_frame", "guidance", trim_material)
    add_box([-door_half - wall, front_y - wall, door_height], [door_half + wall, front_y, door_height + wall], "door_frame", "guidance", trim_material)

    roof_z0 = height
    roof_z1 = height + roof_thickness
    add_box([-half_w - wall, -half_d - wall, roof_z0], [half_w + wall, 0, roof_z1], "roof", "structure")
    add_box([-half_w - wall, 0, roof_z0], [half_w + wall, half_d + wall, roof_z1], "roof", "structure")

    entities = []
    if include_entities:
        entities = [
            entity("info_player_start", [0, front_y - 64, floor_thickness + 32]),
            entity("light", [0, 0, height + 128], {"_light": "255 235 210 300"}),
        ]

    return make_ir(
        name="MCP Recipe: Simple house",
        module_id=module_id,
        material=material,
        grid=grid,
        operations=operations,
        entities=entities,
        role="structure",
    )


def main() -> None:
    run_recipe_cli("Generate simple house TrenchBroom MCP IR", MANIFEST, build)


if __name__ == "__main__":
    main()
