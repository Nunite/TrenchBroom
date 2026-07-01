#!/usr/bin/env python3
"""Generate a freestanding wall with a rectangular opening."""

from __future__ import annotations

import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR.parent / "lib"))

from ir_builder import DEFAULT_MATERIAL, integer, make_ir, op_box, run_recipe_cli, string, vec3  # noqa: E402


MANIFEST = {
    "id": "opening_wall",
    "name": "Opening wall",
    "version": "1.0.0",
    "summary": "Generate a freestanding segmented wall with a door/window opening.",
    "defaultParams": {
        "moduleId": "recipe-opening-wall",
        "min": [0, 0, 0],
        "max": [256, 16, 128],
        "openingMin": [96, 0, 0],
        "openingMax": [160, 16, 96],
        "grid": 16,
        "material": DEFAULT_MATERIAL,
    },
    "parameters": [
        {"name": "moduleId", "type": "string", "required": True},
        {"name": "min", "type": "vec3"},
        {"name": "max", "type": "vec3"},
        {"name": "openingMin", "type": "vec3"},
        {"name": "openingMax", "type": "vec3"},
        {"name": "grid", "type": "integer", "min": 1},
        {"name": "material", "type": "string"},
    ],
    "output": {
        "moduleIdParam": "moduleId",
        "requiredParts": ["opening_header"],
        "parts": ["wall_side_a", "wall_side_b", "opening_header"],
        "routeLike": False,
    },
    "expectedWarnings": [
        "opening_wall creates a new segmented wall; use geometry_csg_selection(subtract) to cut existing brushes."
    ],
    "recommendedValidation": [
        "ir_compile_preview_from_file",
        "ir_apply_from_file",
        "module_inspect",
        "map_validate(groupByType:true)",
        "module_render_review",
    ],
}


def _inside(inner_min: list[float], inner_max: list[float], outer_min: list[float], outer_max: list[float]) -> bool:
    return all(outer_min[i] <= inner_min[i] and inner_max[i] <= outer_max[i] for i in range(3))


def _add_if_not_empty(segments: list[tuple[str, list[float], list[float]]], part: str, lo: list[float], hi: list[float]) -> None:
    if all(lo[i] < hi[i] for i in range(3)):
        segments.append((part, lo, hi))


def build(params: dict) -> dict:
    module_id = string(params, "moduleId", "recipe-opening-wall")
    minimum = vec3(params, "min", [0, 0, 0])
    maximum = vec3(params, "max", [256, 16, 128])
    opening_min = vec3(params, "openingMin", [96, 0, 0])
    opening_max = vec3(params, "openingMax", [160, 16, 96])
    grid = integer(params, "grid", 16)
    material = string(params, "material", DEFAULT_MATERIAL)
    if any(minimum[i] >= maximum[i] for i in range(3)):
        raise ValueError("max must be greater than min on every axis")
    if any(opening_min[i] >= opening_max[i] for i in range(3)):
        raise ValueError("openingMax must be greater than openingMin on every axis")
    if not _inside(opening_min, opening_max, minimum, maximum):
        raise ValueError("opening bounds must be inside wall bounds")

    size_x = maximum[0] - minimum[0]
    size_y = maximum[1] - minimum[1]
    segments: list[tuple[str, list[float], list[float]]] = []
    if size_x >= size_y:
        _add_if_not_empty(segments, "wall_side_a", minimum, [opening_min[0], maximum[1], maximum[2]])
        _add_if_not_empty(segments, "wall_side_b", [opening_max[0], minimum[1], minimum[2]], maximum)
        _add_if_not_empty(
            segments,
            "opening_header",
            [opening_min[0], minimum[1], opening_max[2]],
            [opening_max[0], maximum[1], maximum[2]],
        )
    else:
        _add_if_not_empty(segments, "wall_side_a", minimum, [maximum[0], opening_min[1], maximum[2]])
        _add_if_not_empty(segments, "wall_side_b", [minimum[0], opening_max[1], minimum[2]], maximum)
        _add_if_not_empty(
            segments,
            "opening_header",
            [minimum[0], opening_min[1], opening_max[2]],
            [maximum[0], opening_max[1], maximum[2]],
        )
    if not segments:
        raise ValueError("opening produced no wall segments")

    operations = [
        op_box(lo, hi, part=part, role="boundary", order=index + 1, material=material)
        for index, (part, lo, hi) in enumerate(segments)
    ]
    return make_ir(
        name="MCP Recipe: Opening wall",
        module_id=module_id,
        role="boundary",
        material=material,
        grid=grid,
        operations=operations,
        extra_metadata={"recipe": "opening_wall"},
    )


def main() -> None:
    run_recipe_cli("Generate opening wall TrenchBroom MCP IR", MANIFEST, build)


if __name__ == "__main__":
    main()
