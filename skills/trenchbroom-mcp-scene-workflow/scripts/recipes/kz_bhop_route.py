#!/usr/bin/env python3
"""Generate a small KZ-style bhop / slide route IR.

This recipe emits geometry facts and metadata only. KZ difficulty and movement
viability remain skill/agent judgments plus in-game testing.
"""

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
    op_ramp_between,
    run_recipe_cli,
    string,
)


MANIFEST = {
    "id": "kz_bhop_route",
    "name": "KZ bhop route",
    "version": "1.0.0",
    "summary": "Generate a KZ-style bhop/slide route prototype with ordered platforms, direction markers, optional slide ramp, spawn, and light.",
    "defaultParams": {
        "moduleId": "recipe-kz-bhop-route",
        "routeId": "recipe-kz-bhop-route",
        "platformCount": 7,
        "spacing": 224,
        "platformWidth": 112,
        "platformDepth": 96,
        "platformHeight": 32,
        "zStep": 32,
        "slideWidth": 112,
        "grid": 16,
        "includeSlide": True,
        "material": "__TB_empty",
        "markerMaterial": "__TB_empty",
        "includeEntities": True,
    },
    "parameters": [
        {"name": "moduleId", "type": "string", "required": True},
        {"name": "routeId", "type": "string", "required": True},
        {"name": "platformCount", "type": "integer", "min": 2},
        {"name": "spacing", "type": "number", "min": 64},
        {"name": "platformWidth", "type": "number", "min": 32},
        {"name": "platformDepth", "type": "number", "min": 32},
        {"name": "platformHeight", "type": "number", "min": 1},
        {"name": "zStep", "type": "number"},
        {"name": "slideWidth", "type": "number", "min": 32},
        {"name": "grid", "type": "integer", "min": 1},
        {"name": "includeSlide", "type": "boolean"},
        {"name": "material", "type": "string"},
        {"name": "markerMaterial", "type": "string"},
        {"name": "includeEntities", "type": "boolean"},
    ],
    "output": {
        "moduleIdParam": "moduleId",
        "routeIdParam": "routeId",
        "requiredParts": ["platform", "direction_marker"],
        "parts": ["platform", "direction_marker", "slide"],
        "routeLike": True,
        "continuityMode": "stepped_or_jump",
    },
    "expectedWarnings": [
        "Bhop gaps can be intentional; strict geometric continuity may be false even when the route intent is valid."
    ],
    "recommendedValidation": [
        "ir_compile_preview_from_file",
        "ir_apply_from_file",
        "module_inspect",
        "selector_preview(selector={moduleId, routeId})",
        "geometry_analyze_slopes(selector={moduleId, part:'slide'}) when includeSlide is true",
        "geometry_analyze_route_continuity(orderBy:'metadataOrder') with stepped/jump interpretation",
        "map_validate(groupByType:true)",
        "module_render_review",
    ],
}


def build(params: dict) -> dict:
    module_id = string(params, "moduleId", "recipe-kz-bhop-route")
    route_id = string(params, "routeId", module_id)
    material = string(params, "material", DEFAULT_MATERIAL)
    marker_material = string(params, "markerMaterial", material)
    grid = integer(params, "grid", 16)
    platform_count = integer(params, "platformCount", 7)
    spacing = number(params, "spacing", 224)
    platform_w = number(params, "platformWidth", 112)
    platform_d = number(params, "platformDepth", 96)
    platform_h = number(params, "platformHeight", 32)
    z_step = number(params, "zStep", 32)
    slide_width = number(params, "slideWidth", 112)
    include_slide = bool(params.get("includeSlide", True))
    include_entities = bool(params.get("includeEntities", True))

    operations: list[dict] = []
    order = 1
    for i in range(platform_count):
        x = i * spacing
        y_offset = 48 if i % 2 else -48
        z = (i % 3) * z_step
        operations.append(
            op_box(
                [x - platform_w / 2, y_offset - platform_d / 2, z],
                [x + platform_w / 2, y_offset + platform_d / 2, z + platform_h],
                part="platform",
                role="walkable",
                order=order,
                material=material,
                extra_metadata={"routeId": route_id},
            )
        )
        # Small direction marker on the landing side.
        operations.append(
            op_box(
                [x + platform_w / 2 - 16, y_offset - 16, z + platform_h],
                [x + platform_w / 2 + 32, y_offset + 16, z + platform_h + 16],
                part="direction_marker",
                role="guidance",
                order=1000 + order,
                material=marker_material,
                extra_metadata={"routeId": route_id},
            )
        )
        order += 1

    if include_slide and platform_count >= 2:
        start_x = (platform_count - 2) * spacing
        end_x = (platform_count - 1) * spacing
        start_z = ((platform_count - 2) % 3) * z_step + platform_h
        end_z = ((platform_count - 1) % 3) * z_step + platform_h + 128
        operations.append(
            op_ramp_between(
                [start_x, 176, start_z],
                [end_x, 176, end_z],
                width=slide_width,
                thickness=16,
                part="slide",
                role="walkable",
                order=order,
                material=material,
                extra_metadata={"routeId": route_id},
            )
        )

    entities = []
    if include_entities:
        entities = [
            entity("info_player_start", [0, -48, 96]),
            entity("light", [spacing * max(1, platform_count // 2), 0, 384], {"_light": "220 240 255 400"}),
        ]

    return make_ir(
        name="MCP Recipe: KZ bhop route",
        module_id=module_id,
        route_id=route_id,
        role="walkable",
        material=material,
        grid=grid,
        operations=operations,
        entities=entities,
        extra_metadata={"gameplay": "kz-bhop-prototype"},
    )


def main() -> None:
    run_recipe_cli("Generate KZ bhop route TrenchBroom MCP IR", MANIFEST, build)


if __name__ == "__main__":
    main()
