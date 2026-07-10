#!/usr/bin/env python3
"""Generate a rectangular room/corridor/sky shell IR."""

from __future__ import annotations

import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR.parent / "lib"))

from ir_builder import (  # noqa: E402
    DEFAULT_MATERIAL,
    integer,
    make_ir,
    number,
    op_box,
    run_recipe_cli,
    string,
    vec3,
)


MANIFEST = {
    "id": "rect_shell",
    "name": "Rect shell",
    "version": "1.0.0",
    "summary": "Generate a rectangular shell for room, corridor, or sky shell drafts.",
    "defaultParams": {
        "moduleId": "recipe-rect-shell",
        "kind": "room",
        "min": [0, 0, 0],
        "max": [512, 512, 128],
        "thickness": 16,
        "grid": 16,
        "material": DEFAULT_MATERIAL,
        "skyMaterial": "sky",
    },
    "parameters": [
        {"name": "moduleId", "type": "string", "required": True},
        {"name": "kind", "type": "string"},
        {"name": "min", "type": "vec3"},
        {"name": "max", "type": "vec3"},
        {"name": "thickness", "type": "number", "min": 1},
        {"name": "grid", "type": "integer", "min": 1},
        {"name": "material", "type": "string"},
        {"name": "skyMaterial", "type": "string"},
    ],
    "output": {
        "moduleIdParam": "moduleId",
        "requiredParts": ["floor", "ceiling", "wall_north", "wall_south", "wall_east", "wall_west"],
        "parts": ["floor", "ceiling", "wall_north", "wall_south", "wall_east", "wall_west"],
        "routeLike": False,
    },
    "qualityPolicy": {"intent": "balanced"},
    "reviewPolicy": {"recommended": True, "required": False},
    "recommendedValidation": [
        "ir_compile_preview_from_file",
        "ir_apply_from_file",
        "module_inspect",
        "map_validate(groupByType:true)",
        "module_render_review",
    ],
}


def _check_bounds(minimum: list[float], maximum: list[float]) -> None:
    if any(minimum[i] >= maximum[i] for i in range(3)):
        raise ValueError("max must be greater than min on every axis")


def build(params: dict) -> dict:
    module_id = string(params, "moduleId", "recipe-rect-shell")
    kind = string(params, "kind", "room")
    if kind not in ("room", "corridor", "sky_shell"):
        raise ValueError("kind must be room, corridor, or sky_shell")
    minimum = vec3(params, "min", [0, 0, 0])
    maximum = vec3(params, "max", [512, 512, 128])
    _check_bounds(minimum, maximum)
    thickness = number(params, "thickness", 16)
    grid = integer(params, "grid", 16)
    material = string(params, "skyMaterial", "sky") if kind == "sky_shell" else string(params, "material", DEFAULT_MATERIAL)

    min_x, min_y, min_z = minimum
    max_x, max_y, max_z = maximum
    shell = [
        ("floor", [min_x - thickness, min_y - thickness, min_z - thickness], [max_x + thickness, max_y + thickness, min_z]),
        ("ceiling", [min_x - thickness, min_y - thickness, max_z], [max_x + thickness, max_y + thickness, max_z + thickness]),
        ("wall_west", [min_x - thickness, min_y - thickness, min_z], [min_x, max_y + thickness, max_z]),
        ("wall_east", [max_x, min_y - thickness, min_z], [max_x + thickness, max_y + thickness, max_z]),
        ("wall_south", [min_x, min_y - thickness, min_z], [max_x, min_y, max_z]),
        ("wall_north", [min_x, max_y, min_z], [max_x, max_y + thickness, max_z]),
    ]
    operations = [
        op_box(lo, hi, part=part, role="boundary", order=index + 1, material=material)
        for index, (part, lo, hi) in enumerate(shell)
    ]

    return make_ir(
        name=f"MCP Recipe: Rect shell {kind}",
        module_id=module_id,
        role="boundary",
        material=material,
        grid=grid,
        operations=operations,
        extra_metadata={"recipe": "rect_shell", "kind": kind},
    )


def main() -> None:
    run_recipe_cli("Generate rectangular shell TrenchBroom MCP IR", MANIFEST, build)


if __name__ == "__main__":
    main()
