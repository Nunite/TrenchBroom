#!/usr/bin/env python3
"""Generate a continuous tunnel shell along path_corner-style points."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR.parent / "lib"))

from ir_builder import DEFAULT_MATERIAL, boolean, integer, number, run_recipe_cli, string  # noqa: E402
from path_sweep_core import (  # noqa: E402
    build_sweep_ir,
    material_map,
    normalize_texture_policy,
    points3,
    rect_tunnel_profile,
)


MANIFEST = {
    "id": "path_tunnel",
    "name": "Path tunnel",
    "version": "1.1.0",
    "summary": "Generate a continuous annotated rectangular tunnel shell along a path_corner chain.",
    "defaultParams": {
        "moduleId": "recipe-path-tunnel",
        "routeId": "recipe-path-tunnel",
        "points": [[0, 0, 0], [256, 0, 0]],
        "width": 128,
        "height": 128,
        "wallThickness": 16,
        "floorThickness": 16,
        "ceilingThickness": 16,
        "qualityIntent": "balanced",
        "grid": 16,
        "material": DEFAULT_MATERIAL,
        "partMaterials": {},
        "texturePolicy": {"mode": "part_materials", "align": "none", "stretchAlongPath": False},
        "cornerMode": "miter",
        "miterLimit": 4,
        "capEnds": True,
    },
    "parameters": [
        {"name": "moduleId", "type": "string", "required": True},
        {"name": "routeId", "type": "string", "required": True},
        {"name": "points", "type": "array", "required": True},
        {"name": "width", "type": "number", "min": 32},
        {"name": "height", "type": "number", "min": 32},
        {"name": "wallThickness", "type": "number", "min": 1},
        {"name": "floorThickness", "type": "number", "min": 1},
        {"name": "ceilingThickness", "type": "number", "min": 1},
        {"name": "qualityIntent", "type": "string", "enum": ["draft", "balanced", "smooth"]},
        {"name": "grid", "type": "integer", "min": 1},
        {"name": "material", "type": "string"},
        {"name": "partMaterials", "type": "object"},
        {"name": "texturePolicy", "type": "object"},
        {"name": "cornerMode", "type": "string", "enum": ["miter"]},
        {"name": "miterLimit", "type": "number", "min": 1},
        {"name": "capEnds", "type": "boolean"},
    ],
    "output": {
        "moduleIdParam": "moduleId",
        "routeIdParam": "routeId",
        "requiredParts": ["floor", "ceiling", "wall_left", "wall_right"],
        "parts": ["floor", "ceiling", "wall_left", "wall_right", "cap_start", "cap_end"],
        "routeLike": True,
        "shellSeams": True,
        "textureMetadata": True,
    },
    "qualityPolicy": {
        "intentParam": "qualityIntent",
        "defaultIntent": "balanced",
    },
    "reviewPolicy": {"recommended": True, "required": False},
    "recommendedValidation": [
        "ir_compile_preview_from_file",
        "ir_apply_from_file(previewId)",
        "geometry_analyze_shell_seams(selector={moduleId}, detail:'summary')",
        "texture_apply_by_filter(selector={moduleId, part}) when post-apply material changes are needed",
        "texture_align_face(mode from texturePolicy.align) when exact UV review needs it",
        "module_render_review(edgeMode:'all')",
        "module_render_review(edgeMode:'silhouette')",
        "map_validate(groupByType:true)",
        "problems_check",
    ],
}


def build(params: dict[str, Any]) -> dict[str, Any]:
    module_id = string(params, "moduleId", "recipe-path-tunnel")
    route_id = string(params, "routeId", module_id)
    material = string(params, "material", DEFAULT_MATERIAL)
    width = number(params, "width", 128)
    height = number(params, "height", 128)
    wall_thickness = number(params, "wallThickness", 16)
    floor_thickness = number(params, "floorThickness", 16)
    ceiling_thickness = number(params, "ceilingThickness", 16)
    quality_intent = string(params, "qualityIntent", "balanced")
    profile = rect_tunnel_profile(
        width=width,
        height=height,
        wall_thickness=wall_thickness,
        floor_thickness=floor_thickness,
        ceiling_thickness=ceiling_thickness,
    )
    for strip in profile:
        strip["profileType"] = "rect_tunnel"
    return build_sweep_ir(
        name="MCP Recipe: Path tunnel",
        module_id=module_id,
        route_id=route_id,
        points=points3(params),
        profile_strips=profile,
        material=material,
        part_materials=material_map(params.get("partMaterials")),
        texture_policy=normalize_texture_policy(params.get("texturePolicy")),
        grid=integer(params, "grid", 16),
        corner_mode=string(params, "cornerMode", "miter"),
        miter_limit=number(params, "miterLimit", 4),
        cap_ends=boolean(params, "capEnds", True),
        cap_thickness=wall_thickness,
        quality_policy={"intent": quality_intent},
        recipe="path_tunnel",
    )


def main() -> None:
    run_recipe_cli("Generate path tunnel TrenchBroom MCP IR", MANIFEST, build)


if __name__ == "__main__":
    main()
