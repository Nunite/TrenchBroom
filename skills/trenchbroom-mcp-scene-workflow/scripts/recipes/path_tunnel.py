#!/usr/bin/env python3
"""Generate a continuous tunnel shell along path_corner-style points."""

from __future__ import annotations

import math
import sys
from pathlib import Path
from typing import Any

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR.parent / "lib"))

from ir_builder import (  # noqa: E402
    DEFAULT_MATERIAL,
    boolean,
    integer,
    make_ir,
    number,
    op_polyhedron,
    run_recipe_cli,
    string,
)


MANIFEST = {
    "id": "path_tunnel",
    "name": "Path tunnel",
    "version": "1.0.0",
    "summary": "Generate a continuous annotated tunnel shell along a path_corner chain.",
    "defaultParams": {
        "moduleId": "recipe-path-tunnel",
        "routeId": "recipe-path-tunnel",
        "points": [[0, 0, 0], [256, 0, 0]],
        "width": 128,
        "height": 128,
        "wallThickness": 16,
        "floorThickness": 16,
        "ceilingThickness": 16,
        "grid": 16,
        "material": DEFAULT_MATERIAL,
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
        {"name": "grid", "type": "integer", "min": 1},
        {"name": "material", "type": "string"},
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
    },
    "qualityPolicy": {"intent": "balanced"},
    "reviewPolicy": {"recommended": True, "required": False},
    "recommendedValidation": [
        "ir_compile_preview_from_file",
        "ir_apply_from_file(previewId)",
        "geometry_analyze_shell_seams(selector={moduleId}, detail:'summary')",
        "module_render_review(edgeMode:'all')",
        "module_render_review(edgeMode:'silhouette')",
        "map_validate(groupByType:true)",
        "problems_check",
    ],
}


def _points(params: dict[str, Any]) -> list[list[float]]:
    value = params.get("points")
    if not isinstance(value, list) or len(value) < 2:
        raise ValueError("points must contain at least two [x, y, z] entries")
    result: list[list[float]] = []
    for index, point in enumerate(value):
        if (
            not isinstance(point, list)
            or len(point) != 3
            or not all(isinstance(component, (int, float)) and not isinstance(component, bool) for component in point)
        ):
            raise ValueError(f"points[{index}] must be [x, y, z]")
        result.append([float(point[0]), float(point[1]), float(point[2])])
    return result


def _sub(a: list[float], b: list[float]) -> tuple[float, float]:
    return a[0] - b[0], a[1] - b[1]


def _normalize_xy(delta: tuple[float, float], label: str) -> tuple[float, float]:
    length = math.hypot(delta[0], delta[1])
    if length <= 1e-6:
        raise ValueError(f"{label} has zero horizontal length")
    return delta[0] / length, delta[1] / length


def _left_normal(direction: tuple[float, float]) -> tuple[float, float]:
    return -direction[1], direction[0]


def _line_intersection(
    p1: tuple[float, float],
    d1: tuple[float, float],
    p2: tuple[float, float],
    d2: tuple[float, float],
) -> tuple[float, float] | None:
    cross = d1[0] * d2[1] - d1[1] * d2[0]
    if abs(cross) <= 1e-8:
        return None
    delta = (p2[0] - p1[0], p2[1] - p1[1])
    t = (delta[0] * d2[1] - delta[1] * d2[0]) / cross
    return p1[0] + d1[0] * t, p1[1] + d1[1] * t


def _offset_point(point: list[float], normal: tuple[float, float], distance: float) -> tuple[float, float]:
    return point[0] + normal[0] * distance, point[1] + normal[1] * distance


def _section_point(section: dict[str, Any], key: str, z_offset: float) -> list[float]:
    xy = section[key]
    return [xy[0], xy[1], section["z"] + z_offset]


def _seam(key: str, vertices: list[list[float]]) -> dict[str, Any]:
    return {"key": key, "vertices": vertices}


def _section_seam(section: dict[str, Any], part: str, key: str, floor_thickness: float, height: float, ceiling_thickness: float) -> dict[str, Any]:
    if part == "floor":
        vertices = [
            _section_point(section, "inner_left", 0),
            _section_point(section, "inner_right", 0),
            _section_point(section, "inner_right", -floor_thickness),
            _section_point(section, "inner_left", -floor_thickness),
        ]
    elif part == "ceiling":
        vertices = [
            _section_point(section, "inner_left", height),
            _section_point(section, "inner_right", height),
            _section_point(section, "inner_right", height + ceiling_thickness),
            _section_point(section, "inner_left", height + ceiling_thickness),
        ]
    elif part == "wall_left":
        vertices = [
            _section_point(section, "inner_left", 0),
            _section_point(section, "outer_left", 0),
            _section_point(section, "outer_left", height),
            _section_point(section, "inner_left", height),
        ]
    elif part == "wall_right":
        vertices = [
            _section_point(section, "inner_right", 0),
            _section_point(section, "outer_right", 0),
            _section_point(section, "outer_right", height),
            _section_point(section, "inner_right", height),
        ]
    else:
        raise ValueError(f"unknown tunnel part: {part}")
    return _seam(key, vertices)


def _sections(
    points: list[list[float]],
    distances: dict[str, float],
    miter_limit: float,
) -> list[dict[str, Any]]:
    directions = [
        _normalize_xy(_sub(points[index + 1], points[index]), f"segment {index}")
        for index in range(len(points) - 1)
    ]
    normals = [_left_normal(direction) for direction in directions]
    sections: list[dict[str, Any]] = []
    for index, point in enumerate(points):
        if index == 0:
            normal = normals[0]
            values = {key: _offset_point(point, normal, distance) for key, distance in distances.items()}
        elif index == len(points) - 1:
            normal = normals[-1]
            values = {key: _offset_point(point, normal, distance) for key, distance in distances.items()}
        else:
            prev_dir = directions[index - 1]
            next_dir = directions[index]
            prev_normal = normals[index - 1]
            next_normal = normals[index]
            values = {}
            for key, distance in distances.items():
                prev_origin = _offset_point(point, prev_normal, distance)
                next_origin = _offset_point(point, next_normal, distance)
                intersection = _line_intersection(prev_origin, prev_dir, next_origin, next_dir)
                if intersection is None:
                    intersection = prev_origin
                if abs(distance) > 1e-6:
                    ratio = math.hypot(intersection[0] - point[0], intersection[1] - point[1]) / abs(distance)
                    if ratio > miter_limit:
                        raise ValueError(
                            f"miter at points[{index}] exceeds miterLimit: {ratio:.3f} > {miter_limit:.3f}"
                        )
                values[key] = intersection
        values["z"] = point[2]
        sections.append(values)
    return sections


def _part_points(
    a: dict[str, Any],
    b: dict[str, Any],
    part: str,
    floor_thickness: float,
    height: float,
    ceiling_thickness: float,
) -> list[list[float]]:
    if part == "floor":
        return [
            _section_point(a, "inner_left", 0),
            _section_point(a, "inner_right", 0),
            _section_point(b, "inner_right", 0),
            _section_point(b, "inner_left", 0),
            _section_point(a, "inner_left", -floor_thickness),
            _section_point(a, "inner_right", -floor_thickness),
            _section_point(b, "inner_right", -floor_thickness),
            _section_point(b, "inner_left", -floor_thickness),
        ]
    if part == "ceiling":
        return [
            _section_point(a, "inner_left", height),
            _section_point(a, "inner_right", height),
            _section_point(b, "inner_right", height),
            _section_point(b, "inner_left", height),
            _section_point(a, "inner_left", height + ceiling_thickness),
            _section_point(a, "inner_right", height + ceiling_thickness),
            _section_point(b, "inner_right", height + ceiling_thickness),
            _section_point(b, "inner_left", height + ceiling_thickness),
        ]
    if part == "wall_left":
        return [
            _section_point(a, "inner_left", 0),
            _section_point(b, "inner_left", 0),
            _section_point(b, "inner_left", height),
            _section_point(a, "inner_left", height),
            _section_point(a, "outer_left", 0),
            _section_point(b, "outer_left", 0),
            _section_point(b, "outer_left", height),
            _section_point(a, "outer_left", height),
        ]
    if part == "wall_right":
        return [
            _section_point(a, "inner_right", 0),
            _section_point(b, "inner_right", 0),
            _section_point(b, "inner_right", height),
            _section_point(a, "inner_right", height),
            _section_point(a, "outer_right", 0),
            _section_point(b, "outer_right", 0),
            _section_point(b, "outer_right", height),
            _section_point(a, "outer_right", height),
        ]
    raise ValueError(f"unknown tunnel part: {part}")


def _cap_points(section: dict[str, Any], direction: tuple[float, float], sign: float, thickness: float, floor_thickness: float, height: float, ceiling_thickness: float) -> list[list[float]]:
    def shifted(key: str, z_offset: float) -> list[float]:
        p = _section_point(section, key, z_offset)
        return [p[0] + direction[0] * sign * thickness, p[1] + direction[1] * sign * thickness, p[2]]

    return [
        _section_point(section, "outer_left", -floor_thickness),
        _section_point(section, "outer_right", -floor_thickness),
        _section_point(section, "outer_right", height + ceiling_thickness),
        _section_point(section, "outer_left", height + ceiling_thickness),
        shifted("outer_left", -floor_thickness),
        shifted("outer_right", -floor_thickness),
        shifted("outer_right", height + ceiling_thickness),
        shifted("outer_left", height + ceiling_thickness),
    ]


def build(params: dict[str, Any]) -> dict[str, Any]:
    module_id = string(params, "moduleId", "recipe-path-tunnel")
    route_id = string(params, "routeId", module_id)
    material = string(params, "material", DEFAULT_MATERIAL)
    grid = integer(params, "grid", 16)
    width = number(params, "width", 128)
    height = number(params, "height", 128)
    wall_thickness = number(params, "wallThickness", 16)
    floor_thickness = number(params, "floorThickness", 16)
    ceiling_thickness = number(params, "ceilingThickness", 16)
    corner_mode = string(params, "cornerMode", "miter")
    miter_limit = number(params, "miterLimit", 4)
    cap_ends = boolean(params, "capEnds", True)
    points = _points(params)
    if corner_mode != "miter":
        raise ValueError("cornerMode must be miter")

    half_width = width / 2.0
    distances = {
        "inner_left": half_width,
        "inner_right": -half_width,
        "outer_left": half_width + wall_thickness,
        "outer_right": -(half_width + wall_thickness),
    }
    sections = _sections(points, distances, miter_limit)
    directions = [
        _normalize_xy(_sub(points[index + 1], points[index]), f"segment {index}")
        for index in range(len(points) - 1)
    ]

    operations: list[dict[str, Any]] = []
    parts = ("floor", "ceiling", "wall_left", "wall_right")
    for segment_index in range(len(sections) - 1):
        start = sections[segment_index]
        end = sections[segment_index + 1]
        for part_index, part in enumerate(parts):
            shell_seams = []
            if segment_index > 0:
                shell_seams.append(_section_seam(
                    start,
                    part,
                    f"internal:{segment_index}:{part}",
                    floor_thickness,
                    height,
                    ceiling_thickness,
                ))
            if segment_index < len(sections) - 2:
                shell_seams.append(_section_seam(
                    end,
                    part,
                    f"internal:{segment_index + 1}:{part}",
                    floor_thickness,
                    height,
                    ceiling_thickness,
                ))
            if cap_ends and segment_index == 0:
                shell_seams.append(_section_seam(
                    start,
                    part,
                    f"cap:start:{part}",
                    floor_thickness,
                    height,
                    ceiling_thickness,
                ))
            if cap_ends and segment_index == len(sections) - 2:
                shell_seams.append(_section_seam(
                    end,
                    part,
                    f"cap:end:{part}",
                    floor_thickness,
                    height,
                    ceiling_thickness,
                ))
            operations.append(
                op_polyhedron(
                    _part_points(start, end, part, floor_thickness, height, ceiling_thickness),
                    part=part,
                    role="shell" if part != "floor" else "walkable",
                    order=segment_index * 10 + part_index + 1,
                    material=material,
                    extra_metadata={
                        "moduleId": module_id,
                        "routeId": route_id,
                        "recipe": "path_tunnel",
                        "shellSeams": shell_seams,
                    },
                )
            )

    if cap_ends:
        for cap_name, section, direction, sign, order in (
            ("cap_start", sections[0], directions[0], -1.0, 9001),
            ("cap_end", sections[-1], directions[-1], 1.0, 9002),
        ):
            cap_side = "start" if cap_name == "cap_start" else "end"
            operations.append(
                op_polyhedron(
                    _cap_points(
                        section,
                        direction,
                        sign,
                        wall_thickness,
                        floor_thickness,
                        height,
                        ceiling_thickness,
                    ),
                    part=cap_name,
                    role="shell",
                    order=order,
                    material=material,
                    extra_metadata={
                        "moduleId": module_id,
                        "routeId": route_id,
                        "recipe": "path_tunnel",
                        "shellSeams": [
                            _section_seam(
                                section,
                                part,
                                f"cap:{cap_side}:{part}",
                                floor_thickness,
                                height,
                                ceiling_thickness,
                            )
                            for part in parts
                        ],
                    },
                )
            )

    return make_ir(
        name="MCP Recipe: Path tunnel",
        module_id=module_id,
        route_id=route_id,
        role="shell",
        material=material,
        grid=grid,
        operations=operations,
        extra_metadata={"recipe": "path_tunnel"},
    )


def main() -> None:
    run_recipe_cli("Generate path tunnel TrenchBroom MCP IR", MANIFEST, build)


if __name__ == "__main__":
    main()
