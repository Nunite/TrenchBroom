#!/usr/bin/env python3
"""Sweep an annotated profile shell along path_corner-style points."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR.parent / "lib"))

from ir_builder import DEFAULT_MATERIAL, boolean, integer, number, run_recipe_cli, string  # noqa: E402
from path_sweep_core import (  # noqa: E402
    arch_tunnel_profile,
    build_sweep_ir,
    custom_profile,
    material_map,
    normalize_texture_policy,
    pipe_profile,
    points3,
    rect_tunnel_profile,
)


MANIFEST = {
    "id": "path_sweep",
    "name": "Path sweep",
    "version": "1.0.0",
    "summary": "Sweep a generic profile shell along a path with seam and texture metadata.",
    "defaultParams": {
        "moduleId": "recipe-path-sweep",
        "routeId": "recipe-path-sweep",
        "points": [[0, 0, 0], [256, 0, 0]],
        "profileType": "rect_tunnel",
        "width": 128,
        "height": 128,
        "wallThickness": 16,
        "floorThickness": 16,
        "ceilingThickness": 16,
        "archSegments": 6,
        "pipeSegments": 12,
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
        {"name": "profileType", "type": "string", "enum": ["rect_tunnel", "arch_tunnel", "pipe", "custom"]},
        {"name": "profile", "type": "object"},
        {"name": "width", "type": "number", "min": 32},
        {"name": "height", "type": "number", "min": 32},
        {"name": "wallThickness", "type": "number", "min": 1},
        {"name": "floorThickness", "type": "number", "min": 0},
        {"name": "ceilingThickness", "type": "number", "min": 0},
        {"name": "archSegments", "type": "integer", "min": 2},
        {"name": "pipeSegments", "type": "integer", "min": 6},
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
        "parts": [],
        "routeLike": True,
        "shellSeams": True,
        "textureMetadata": True,
    },
    "qualityPolicy": {"intent": "balanced"},
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


def _profile_strips(params: dict[str, Any]) -> list[dict[str, Any]]:
    profile_type = string(params, "profileType", "rect_tunnel")
    width = number(params, "width", 128)
    height = number(params, "height", 128)
    wall_thickness = number(params, "wallThickness", 16)
    floor_thickness = number(params, "floorThickness", 16)
    ceiling_thickness = number(params, "ceilingThickness", 16)
    if profile_type == "rect_tunnel":
        strips = rect_tunnel_profile(
            width=width,
            height=height,
            wall_thickness=wall_thickness,
            floor_thickness=floor_thickness,
            ceiling_thickness=ceiling_thickness,
        )
    elif profile_type == "arch_tunnel":
        strips = arch_tunnel_profile(
            width=width,
            height=height,
            wall_thickness=wall_thickness,
            floor_thickness=floor_thickness,
            ceiling_thickness=ceiling_thickness,
            arch_segments=integer(params, "archSegments", 6),
        )
    elif profile_type == "pipe":
        strips = pipe_profile(
            width=width,
            height=height,
            wall_thickness=wall_thickness,
            pipe_segments=integer(params, "pipeSegments", 12),
        )
    elif profile_type == "custom":
        strips = custom_profile(params.get("profile"))
    else:
        raise ValueError("profileType must be rect_tunnel, arch_tunnel, pipe, or custom")
    for strip in strips:
        strip["profileType"] = profile_type
    return strips


def build(params: dict[str, Any]) -> dict[str, Any]:
    module_id = string(params, "moduleId", "recipe-path-sweep")
    route_id = string(params, "routeId", module_id)
    material = string(params, "material", DEFAULT_MATERIAL)
    grid = integer(params, "grid", 16)
    return build_sweep_ir(
        name="MCP Recipe: Path sweep",
        module_id=module_id,
        route_id=route_id,
        points=points3(params),
        profile_strips=_profile_strips(params),
        material=material,
        part_materials=material_map(params.get("partMaterials")),
        texture_policy=normalize_texture_policy(params.get("texturePolicy")),
        grid=grid,
        corner_mode=string(params, "cornerMode", "miter"),
        miter_limit=number(params, "miterLimit", 4),
        cap_ends=boolean(params, "capEnds", True),
        cap_thickness=number(params, "wallThickness", 16),
        recipe="path_sweep",
    )


def main() -> None:
    run_recipe_cli("Generate generic path sweep TrenchBroom MCP IR", MANIFEST, build)


if __name__ == "__main__":
    main()
