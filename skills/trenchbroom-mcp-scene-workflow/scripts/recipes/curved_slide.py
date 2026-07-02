#!/usr/bin/env python3
"""Generate a curved slide surface IR."""

from __future__ import annotations

import math
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR.parent / "lib"))

from ir_builder import DEFAULT_MATERIAL, integer, make_ir, number, run_recipe_cli, string, vec3  # noqa: E402


MANIFEST = {
    "id": "curved_slide",
    "name": "Curved slide",
    "version": "1.0.0",
    "summary": "Generate a segmented curved slide surface with a cross-slope across its width.",
    "defaultParams": {
        "moduleId": "recipe-curved-slide",
        "routeId": "recipe-curved-slide",
        "center": [0, 0, 256],
        "radius": 320,
        "width": 128,
        "turnDegrees": 180,
        "startAngle": 0,
        "crossSlopeDegrees": 52,
        "segments": 24,
        "thickness": 16,
        "grid": 1,
        "material": DEFAULT_MATERIAL,
    },
    "parameters": [
        {"name": "moduleId", "type": "string", "required": True},
        {"name": "routeId", "type": "string"},
        {"name": "center", "type": "vec3"},
        {"name": "radius", "type": "number", "min": 64},
        {"name": "width", "type": "number", "min": 32},
        {"name": "turnDegrees", "type": "number", "min": -360, "max": 360},
        {"name": "startAngle", "type": "number"},
        {"name": "crossSlopeDegrees", "type": "number", "min": -80, "max": 80},
        {"name": "segments", "type": "integer", "min": 4, "max": 96},
        {"name": "thickness", "type": "number", "min": 1},
        {"name": "grid", "type": "integer", "min": 1},
        {"name": "material", "type": "string"},
    ],
    "output": {
        "moduleIdParam": "moduleId",
        "routeIdParam": "routeId",
        "requiredParts": ["slide"],
        "parts": ["slide"],
        "routeLike": True,
        "closedLoopRecommended": False,
    },
    "expectedWarnings": [
        "Curved slide geometry uses polyhedron point clouds; validate slope/readability with MCP review before accepting.",
        "Banked/cross-slope slides may fail centerline route continuity; inspect fullWidthContinuous/maxEdgeGap plus slopeCount.",
    ],
    "recommendedValidation": [
        "ir_compile_preview_from_file",
        "ir_apply_from_file",
        "module_inspect",
        "geometry_analyze_slopes(selector={moduleId, part:'slide'})",
        "geometry_analyze_route_continuity(orderBy:'metadataOrder', routeMode:'spiral'; inspect fullWidthContinuous/maxEdgeGap)",
        "map_validate(groupByType:true)",
        "module_render_review",
    ],
}


def _polar(center: list[float], radius: float, angle_degrees: float, z: float) -> list[float]:
    angle = math.radians(angle_degrees)
    return [
        center[0] + math.cos(angle) * radius,
        center[1] + math.sin(angle) * radius,
        center[2] + z,
    ]


def _slide_segment_points(
    center: list[float],
    inner_radius: float,
    outer_radius: float,
    start_angle: float,
    end_angle: float,
    z_inner: float,
    z_outer: float,
    thickness: float,
) -> list[list[float]]:
    top = [
        _polar(center, inner_radius, start_angle, z_inner),
        _polar(center, outer_radius, start_angle, z_outer),
        _polar(center, outer_radius, end_angle, z_outer),
        _polar(center, inner_radius, end_angle, z_inner),
    ]
    bottom = [[point[0], point[1], point[2] - thickness] for point in top]
    return top + bottom


def build(params: dict) -> dict:
    module_id = string(params, "moduleId", "recipe-curved-slide")
    route_id = string(params, "routeId", module_id)
    center = vec3(params, "center", [0, 0, 256])
    radius = number(params, "radius", 320)
    width = number(params, "width", 128)
    turn_degrees = number(params, "turnDegrees", 180)
    start_angle = number(params, "startAngle", 0)
    cross_slope_degrees = number(params, "crossSlopeDegrees", 52)
    segments = integer(params, "segments", 24)
    thickness = number(params, "thickness", 16)
    grid = integer(params, "grid", 1)
    material = string(params, "material", DEFAULT_MATERIAL)

    if abs(turn_degrees) <= 0.001:
        raise ValueError("turnDegrees must not be zero")
    if width >= radius * 2:
        raise ValueError("width must be smaller than diameter")

    inner_radius = radius - width / 2
    outer_radius = radius + width / 2
    cross_rise = math.tan(math.radians(cross_slope_degrees)) * width
    z_inner = 0.0
    z_outer = cross_rise
    angle_step = turn_degrees / segments

    operations = []
    for index in range(segments):
        operations.append(
            {
                "type": "polyhedron",
                "points": _slide_segment_points(
                    center,
                    inner_radius,
                    outer_radius,
                    start_angle + angle_step * index,
                    start_angle + angle_step * (index + 1),
                    z_inner,
                    z_outer,
                    thickness,
                ),
                "material": material,
                "metadata": {
                    "part": "slide",
                    "role": "walkable",
                    "routeId": route_id,
                    "order": index + 1,
                },
            }
        )

    return make_ir(
        name="MCP Recipe: Curved slide",
        module_id=module_id,
        route_id=route_id,
        role="walkable",
        material=material,
        grid=grid,
        operations=operations,
        extra_metadata={"recipe": "curved_slide"},
    )


def main() -> None:
    run_recipe_cli("Generate curved slide TrenchBroom MCP IR", MANIFEST, build)


if __name__ == "__main__":
    main()
