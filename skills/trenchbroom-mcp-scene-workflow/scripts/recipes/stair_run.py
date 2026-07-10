#!/usr/bin/env python3
"""Generate a straight stair run IR."""

from __future__ import annotations

import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR.parent / "lib"))

from ir_builder import DEFAULT_MATERIAL, integer, make_ir, op_box, run_recipe_cli, string, vec3  # noqa: E402


MANIFEST = {
    "id": "stair_run",
    "name": "Stair run",
    "version": "1.0.0",
    "summary": "Generate a straight run of box steps.",
    "defaultParams": {
        "moduleId": "recipe-stair-run",
        "routeId": "recipe-stair-run",
        "min": [0, 0, 0],
        "max": [256, 128, 128],
        "steps": 8,
        "axis": "x",
        "grid": 16,
        "material": DEFAULT_MATERIAL,
    },
    "parameters": [
        {"name": "moduleId", "type": "string", "required": True},
        {"name": "routeId", "type": "string"},
        {"name": "min", "type": "vec3"},
        {"name": "max", "type": "vec3"},
        {"name": "steps", "type": "integer", "min": 1},
        {"name": "axis", "type": "string"},
        {"name": "grid", "type": "integer", "min": 1},
        {"name": "material", "type": "string"},
    ],
    "output": {
        "moduleIdParam": "moduleId",
        "routeIdParam": "routeId",
        "requiredParts": ["steps"],
        "parts": ["steps"],
        "routeLike": True,
    },
    "qualityPolicy": {"intent": "balanced"},
    "reviewPolicy": {"recommended": True, "required": False},
    "recommendedValidation": [
        "ir_compile_preview_from_file",
        "ir_apply_from_file",
        "module_inspect",
        "geometry_analyze_route_continuity(selector={moduleId, part:'steps'}, routeMode:'stairs_or_steps')",
        "map_validate(groupByType:true)",
        "module_render_review",
    ],
}


def build(params: dict) -> dict:
    module_id = string(params, "moduleId", "recipe-stair-run")
    route_id = string(params, "routeId", module_id)
    minimum = vec3(params, "min", [0, 0, 0])
    maximum = vec3(params, "max", [256, 128, 128])
    steps = integer(params, "steps", 8)
    axis = string(params, "axis", "x")
    grid = integer(params, "grid", 16)
    material = string(params, "material", DEFAULT_MATERIAL)
    if axis not in ("x", "y"):
        raise ValueError("axis must be x or y")
    if steps < 1:
        raise ValueError("steps must be at least 1")
    if any(minimum[i] >= maximum[i] for i in range(3)):
        raise ValueError("max must be greater than min on every axis")

    min_x, min_y, min_z = minimum
    max_x, max_y, max_z = maximum
    run_min = min_x if axis == "x" else min_y
    run_max = max_x if axis == "x" else max_y
    run_step = (run_max - run_min) / steps
    rise_step = (max_z - min_z) / steps
    operations = []
    for index in range(steps):
        step_min_run = run_min + run_step * index
        step_max_run = run_min + run_step * (index + 1)
        step_max_z = min_z + rise_step * (index + 1)
        if axis == "x":
            lo = [step_min_run, min_y, min_z]
            hi = [step_max_run, max_y, step_max_z]
        else:
            lo = [min_x, step_min_run, min_z]
            hi = [max_x, step_max_run, step_max_z]
        operations.append(
            op_box(
                lo,
                hi,
                part="steps",
                role="walkable",
                order=index + 1,
                material=material,
                extra_metadata={"routeId": route_id},
            )
        )

    return make_ir(
        name="MCP Recipe: Stair run",
        module_id=module_id,
        route_id=route_id,
        role="walkable",
        material=material,
        grid=grid,
        operations=operations,
        extra_metadata={"recipe": "stair_run"},
    )


def main() -> None:
    run_recipe_cli("Generate stair run TrenchBroom MCP IR", MANIFEST, build)


if __name__ == "__main__":
    main()
