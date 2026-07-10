#!/usr/bin/env python3
"""Generate a generic ascending loop / helical route IR."""

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
    polar_point,
    run_recipe_cli,
    string,
    vec3,
)


MANIFEST = {
    "id": "ascending_loop",
    "name": "Ascending loop",
    "version": "1.1.0",
    "summary": "Generate a smooth rising loop route with optional rails, support posts, start/finish markers, spawn, and light.",
    "defaultParams": {
        "moduleId": "recipe-ascending-loop",
        "routeId": "recipe-ascending-loop",
        "center": [0, 0, 0],
        "radius": 384,
        "width": 128,
        "rise": 256,
        "thickness": 16,
        "startAngle": 0,
        "turnDegrees": 360,
        "segments": 36,
        "qualityIntent": "balanced",
        "railHeight": 48,
        "railWidth": 16,
        "supportSize": 24,
        "supportEvery": 8,
        "grid": 16,
        "material": "__TB_empty",
        "railMaterial": "__TB_empty",
        "supportMaterial": "__TB_empty",
        "includeEntities": True,
    },
    "parameters": [
        {"name": "moduleId", "type": "string", "required": True},
        {"name": "routeId", "type": "string", "required": True},
        {"name": "center", "type": "vec3"},
        {"name": "radius", "type": "number", "min": 64},
        {"name": "width", "type": "number", "min": 32},
        {"name": "rise", "type": "number"},
        {"name": "thickness", "type": "number", "min": 1},
        {"name": "startAngle", "type": "number"},
        {"name": "turnDegrees", "type": "number", "min": 1, "max": 360},
        {"name": "segments", "type": "integer", "min": 4},
        {"name": "qualityIntent", "type": "string", "enum": ["draft", "balanced", "smooth"]},
        {"name": "railHeight", "type": "number", "min": 0},
        {"name": "railWidth", "type": "number", "min": 1},
        {"name": "supportSize", "type": "number", "min": 1},
        {"name": "supportEvery", "type": "integer", "min": 1},
        {"name": "grid", "type": "integer", "min": 1},
        {"name": "material", "type": "string"},
        {"name": "railMaterial", "type": "string"},
        {"name": "supportMaterial", "type": "string"},
        {"name": "includeEntities", "type": "boolean"},
    ],
    "output": {
        "moduleIdParam": "moduleId",
        "routeIdParam": "routeId",
        "requiredParts": ["ramp", "start_marker", "finish_marker"],
        "parts": ["ramp", "support", "inner_rail", "outer_rail", "start_marker", "finish_marker"],
        "routeLike": True,
        "closedLoopRecommended": False,
    },
    "qualityPolicy": {
        "intentParam": "qualityIntent",
        "defaultIntent": "balanced",
    },
    "reviewPolicy": {"recommended": True, "required": False},
    "expectedWarnings": [
        "Circular arc geometry can produce non-integer vertex warnings; accept them only when slope and continuity acceptance pass."
    ],
    "recommendedValidation": [
        "ir_compile_preview_from_file",
        "ir_apply_from_file",
        "module_inspect",
        "geometry_analyze_slopes(selector={moduleId, part:'ramp'})",
        "geometry_analyze_route_continuity(orderBy:'metadataOrder')",
        "map_validate(groupByType:true)",
        "module_render_review",
    ],
}


def build(params: dict) -> dict:
    module_id = string(params, "moduleId", "recipe-ascending-loop")
    route_id = string(params, "routeId", module_id)
    material = string(params, "material", DEFAULT_MATERIAL)
    rail_material = string(params, "railMaterial", material)
    support_material = string(params, "supportMaterial", material)
    grid = integer(params, "grid", 16)
    center = vec3(params, "center", [0, 0, 0])
    radius = number(params, "radius", 384)
    width = number(params, "width", 128)
    rise = number(params, "rise", 256)
    thickness = number(params, "thickness", 16)
    start_angle = number(params, "startAngle", 0)
    turn_degrees = number(params, "turnDegrees", 360)
    segments = integer(params, "segments", 36)
    quality_intent = string(params, "qualityIntent", "balanced")
    rail_height = number(params, "railHeight", 48)
    rail_width = number(params, "railWidth", 16)
    support_size = number(params, "supportSize", 24)
    support_every = max(1, integer(params, "supportEvery", 8))
    include_entities = bool(params.get("includeEntities", True))

    operations: list[dict] = [
        {
            "type": "arc_ramp",
            "center": center,
            "radius": radius,
            "width": width,
            "startAngle": start_angle,
            "turnDegrees": turn_degrees,
            "rise": rise,
            "segments": segments,
            "thickness": thickness,
            "material": material,
            "metadata": {
                "part": "ramp",
                "role": "walkable",
                "routeId": route_id,
                "order": 1,
            },
        }
    ]

    for index in range(segments + 1):
        angle = start_angle + turn_degrees * index / segments
        z = rise * index / segments
        if index % support_every == 0:
            p = polar_point(center, radius, angle, z)
            operations.append(
                op_box(
                    [
                        p[0] - support_size / 2,
                        p[1] - support_size / 2,
                        center[2] - thickness,
                    ],
                    [
                        p[0] + support_size / 2,
                        p[1] + support_size / 2,
                        center[2] + z,
                    ],
                    part="support",
                    role="support",
                    order=1000 + index,
                    material=support_material,
                )
            )

        if index % 2 == 0:
            for side, side_radius in (("inner_rail", radius - width / 2), ("outer_rail", radius + width / 2)):
                p = polar_point(center, side_radius, angle, z)
                operations.append(
                    op_box(
                        [
                            p[0] - rail_width / 2,
                            p[1] - rail_width / 2,
                            center[2] + z,
                        ],
                        [
                            p[0] + rail_width / 2,
                            p[1] + rail_width / 2,
                            center[2] + z + rail_height,
                        ],
                        part=side,
                        role="boundary",
                        order=2000 + index,
                        material=rail_material,
                    )
                )

    start = polar_point(center, radius, start_angle, 0)
    end = polar_point(center, radius, start_angle + turn_degrees, rise)
    operations.append(
        op_box(
            [start[0] - width / 2, start[1] - width / 2, center[2] - 8],
            [start[0] + width / 2, start[1] + width / 2, center[2] + 8],
            part="start_marker",
            role="guidance",
            order=3001,
            material=rail_material,
        )
    )
    operations.append(
        op_box(
            [end[0] - width / 2, end[1] - width / 2, center[2] + rise - 8],
            [end[0] + width / 2, end[1] + width / 2, center[2] + rise + 8],
            part="finish_marker",
            role="guidance",
            order=3002,
            material=rail_material,
        )
    )

    entities = []
    if include_entities:
        entities = [
            entity("info_player_start", [start[0], start[1], center[2] + 64]),
            entity("light", [center[0], center[1], center[2] + rise + 384], {"_light": "255 240 220 500"}),
        ]

    return make_ir(
        name="MCP Recipe: Ascending loop",
        module_id=module_id,
        route_id=route_id,
        role="walkable",
        material=material,
        grid=grid,
        operations=operations,
        entities=entities,
        quality_policy={"intent": quality_intent},
    )


def main() -> None:
    run_recipe_cli("Generate ascending loop TrenchBroom MCP IR", MANIFEST, build)


if __name__ == "__main__":
    main()
