#!/usr/bin/env python3
"""Generate a simple cover block IR."""

from __future__ import annotations

import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR.parent / "lib"))

from ir_builder import DEFAULT_MATERIAL, integer, make_ir, op_box, run_recipe_cli, string, vec3  # noqa: E402


MANIFEST = {
    "id": "cover_block",
    "name": "Cover block",
    "version": "1.0.0",
    "summary": "Generate a simple low cover or obstacle block.",
    "defaultParams": {
        "moduleId": "recipe-cover-block",
        "min": [0, 0, 0],
        "max": [128, 48, 64],
        "grid": 16,
        "material": DEFAULT_MATERIAL,
    },
    "parameters": [
        {"name": "moduleId", "type": "string", "required": True},
        {"name": "min", "type": "vec3"},
        {"name": "max", "type": "vec3"},
        {"name": "grid", "type": "integer", "min": 1},
        {"name": "material", "type": "string"},
    ],
    "output": {
        "moduleIdParam": "moduleId",
        "requiredParts": ["cover"],
        "parts": ["cover"],
        "routeLike": False,
    },
    "recommendedValidation": [
        "ir_compile_preview_from_file",
        "ir_apply_from_file",
        "module_inspect",
        "map_validate(groupByType:true)",
        "module_render_review",
    ],
}


def build(params: dict) -> dict:
    module_id = string(params, "moduleId", "recipe-cover-block")
    minimum = vec3(params, "min", [0, 0, 0])
    maximum = vec3(params, "max", [128, 48, 64])
    if any(minimum[i] >= maximum[i] for i in range(3)):
        raise ValueError("max must be greater than min on every axis")
    grid = integer(params, "grid", 16)
    material = string(params, "material", DEFAULT_MATERIAL)
    return make_ir(
        name="MCP Recipe: Cover block",
        module_id=module_id,
        role="cover",
        material=material,
        grid=grid,
        operations=[
            op_box(minimum, maximum, part="cover", role="cover", order=1, material=material)
        ],
        extra_metadata={"recipe": "cover_block"},
    )


def main() -> None:
    run_recipe_cli("Generate cover block TrenchBroom MCP IR", MANIFEST, build)


if __name__ == "__main__":
    main()
