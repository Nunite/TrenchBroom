#!/usr/bin/env python3
"""Generate a curved slide surface IR."""

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
    op_banked_arc_segments,
    run_recipe_cli,
    string,
    vec3,
)


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

    operations = op_banked_arc_segments(
        center=center,
        radius=radius,
        width=width,
        start_angle=start_angle,
        turn_degrees=turn_degrees,
        cross_slope_degrees=cross_slope_degrees,
        segments=segments,
        thickness=thickness,
        part="slide",
        role="walkable",
        route_id=route_id,
        material=material,
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
