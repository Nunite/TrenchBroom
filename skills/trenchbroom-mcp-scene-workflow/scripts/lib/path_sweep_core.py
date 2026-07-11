#!/usr/bin/env python3
"""Shared path sweep helpers for TrenchBroom MCP recipes."""

from __future__ import annotations

import math
from typing import Any

from ir_builder import DEFAULT_MATERIAL, make_ir, op_polyhedron


Point3 = list[float]
Point2 = tuple[float, float]
ProfileVertex = tuple[float, float]


def points3(params: dict[str, Any]) -> list[Point3]:
    value = params.get("points")
    if not isinstance(value, list) or len(value) < 2:
        raise ValueError("points must contain at least two [x, y, z] entries")
    result: list[Point3] = []
    for index, point in enumerate(value):
        if (
            not isinstance(point, list)
            or len(point) != 3
            or not all(isinstance(component, (int, float)) and not isinstance(component, bool) for component in point)
        ):
            raise ValueError(f"points[{index}] must be [x, y, z]")
        result.append([float(point[0]), float(point[1]), float(point[2])])
    return result


def normalize_texture_policy(value: Any) -> dict[str, Any]:
    default = {"mode": "part_materials", "align": "none", "stretchAlongPath": False}
    if value is None:
        return default
    if not isinstance(value, dict):
        raise ValueError("texturePolicy must be an object")
    result = {**default, **value}
    mode = result.get("mode")
    if mode not in ("part_materials", "metadata_hints", "post_apply"):
        raise ValueError("texturePolicy.mode must be part_materials, metadata_hints, or post_apply")
    align = result.get("align")
    if align not in ("none", "paraxial", "parallel", "reset"):
        raise ValueError("texturePolicy.align must be none, paraxial, parallel, or reset")
    if not isinstance(result.get("stretchAlongPath"), bool):
        raise ValueError("texturePolicy.stretchAlongPath must be a boolean")
    source_face = result.get("sourceFace")
    if source_face is not None and not isinstance(source_face, dict):
        raise ValueError("texturePolicy.sourceFace must be an object when provided")
    return result


def material_map(value: Any) -> dict[str, str]:
    if value is None:
        return {}
    if not isinstance(value, dict):
        raise ValueError("partMaterials must be an object")
    result: dict[str, str] = {}
    for key, material in value.items():
        if not isinstance(key, str) or not key:
            raise ValueError("partMaterials keys must be non-empty strings")
        if not isinstance(material, str):
            raise ValueError(f"partMaterials[{key}] must be a string")
        result[key] = material
    return result


def rect_tunnel_profile(
    *,
    width: float,
    height: float,
    wall_thickness: float,
    floor_thickness: float,
    ceiling_thickness: float,
) -> list[dict[str, Any]]:
    half = width / 2.0
    left_inner = half
    right_inner = -half
    left_outer = half + wall_thickness
    right_outer = -(half + wall_thickness)
    return [
        {
            "part": "floor",
            "role": "walkable",
            "textureRole": "floor",
            "quad": [(left_inner, 0.0), (right_inner, 0.0), (right_inner, -floor_thickness), (left_inner, -floor_thickness)],
        },
        {
            "part": "ceiling",
            "role": "shell",
            "textureRole": "ceiling",
            "quad": [
                (left_inner, height),
                (right_inner, height),
                (right_inner, height + ceiling_thickness),
                (left_inner, height + ceiling_thickness),
            ],
        },
        {
            "part": "wall_left",
            "role": "shell",
            "textureRole": "wall",
            "quad": [(left_inner, 0.0), (left_outer, 0.0), (left_outer, height), (left_inner, height)],
        },
        {
            "part": "wall_right",
            "role": "shell",
            "textureRole": "wall",
            "quad": [(right_inner, 0.0), (right_outer, 0.0), (right_outer, height), (right_inner, height)],
        },
    ]


def arch_tunnel_profile(
    *,
    width: float,
    height: float,
    wall_thickness: float,
    floor_thickness: float,
    ceiling_thickness: float,
    arch_segments: int,
) -> list[dict[str, Any]]:
    if arch_segments < 2:
        raise ValueError("archSegments must be >= 2")
    half = width / 2.0
    spring_z = height - half
    if spring_z < 16.0:
        raise ValueError("height must be at least width / 2 + 16 for arch_tunnel")
    strips = rect_tunnel_profile(
        width=width,
        height=spring_z,
        wall_thickness=wall_thickness,
        floor_thickness=floor_thickness,
        ceiling_thickness=0.0,
    )
    strips = [strip for strip in strips if strip["part"] != "ceiling"]
    inner_radius = half
    outer_radius = half + ceiling_thickness
    for index in range(arch_segments):
        a0 = math.pi - math.pi * index / arch_segments
        a1 = math.pi - math.pi * (index + 1) / arch_segments
        inner0 = (math.cos(a0) * inner_radius, spring_z + math.sin(a0) * inner_radius)
        inner1 = (math.cos(a1) * inner_radius, spring_z + math.sin(a1) * inner_radius)
        outer1 = (math.cos(a1) * outer_radius, spring_z + math.sin(a1) * outer_radius)
        outer0 = (math.cos(a0) * outer_radius, spring_z + math.sin(a0) * outer_radius)
        strips.append(
            {
                "part": f"ceiling_{index + 1:02d}",
                "role": "shell",
                "textureRole": "ceiling",
                "quad": [inner0, inner1, outer1, outer0],
            }
        )
    return strips


def pipe_profile(
    *,
    width: float,
    height: float,
    wall_thickness: float,
    pipe_segments: int,
) -> list[dict[str, Any]]:
    if pipe_segments < 6:
        raise ValueError("pipeSegments must be >= 6")
    x_radius = width / 2.0
    z_radius = height / 2.0
    center_z = z_radius
    strips: list[dict[str, Any]] = []
    for index in range(pipe_segments):
        a0 = -math.pi / 2.0 + math.tau * index / pipe_segments
        a1 = -math.pi / 2.0 + math.tau * (index + 1) / pipe_segments
        inner0 = (math.cos(a0) * x_radius, center_z + math.sin(a0) * z_radius)
        inner1 = (math.cos(a1) * x_radius, center_z + math.sin(a1) * z_radius)
        outer1 = (
            math.cos(a1) * (x_radius + wall_thickness),
            center_z + math.sin(a1) * (z_radius + wall_thickness),
        )
        outer0 = (
            math.cos(a0) * (x_radius + wall_thickness),
            center_z + math.sin(a0) * (z_radius + wall_thickness),
        )
        texture_role = "floor" if index == 0 else "pipe"
        strips.append(
            {
                "part": f"pipe_{index + 1:02d}",
                "role": "shell",
                "textureRole": texture_role,
                "quad": [inner0, inner1, outer1, outer0],
            }
        )
    return strips


def custom_profile(profile: Any) -> list[dict[str, Any]]:
    if not isinstance(profile, dict):
        raise ValueError("profile must be an object for profileType custom")
    strips_value = profile.get("strips")
    if not isinstance(strips_value, list) or not strips_value:
        raise ValueError("profile.strips must be a non-empty array")
    strips: list[dict[str, Any]] = []
    for index, strip in enumerate(strips_value):
        if not isinstance(strip, dict):
            raise ValueError(f"profile.strips[{index}] must be an object")
        part = strip.get("part")
        if not isinstance(part, str) or not part:
            raise ValueError(f"profile.strips[{index}].part must be a non-empty string")
        role = strip.get("role", "shell")
        if not isinstance(role, str) or not role:
            raise ValueError(f"profile.strips[{index}].role must be a non-empty string")
        texture_role = strip.get("textureRole", part)
        if not isinstance(texture_role, str) or not texture_role:
            raise ValueError(f"profile.strips[{index}].textureRole must be a non-empty string")
        quad_value = strip.get("quad")
        if not isinstance(quad_value, list) or len(quad_value) != 4:
            raise ValueError(f"profile.strips[{index}].quad must contain four [offset, z] vertices")
        quad: list[ProfileVertex] = []
        for vertex_index, vertex in enumerate(quad_value):
            if (
                not isinstance(vertex, list)
                or len(vertex) != 2
                or not all(isinstance(component, (int, float)) and not isinstance(component, bool) for component in vertex)
            ):
                raise ValueError(f"profile.strips[{index}].quad[{vertex_index}] must be [offset, z]")
            quad.append((float(vertex[0]), float(vertex[1])))
        strips.append({"part": part, "role": role, "textureRole": texture_role, "quad": quad})
    return strips


def _sub(a: Point3, b: Point3) -> tuple[float, float]:
    return a[0] - b[0], a[1] - b[1]


def _normalize_xy(delta: tuple[float, float], label: str) -> tuple[float, float]:
    length = math.hypot(delta[0], delta[1])
    if length <= 1e-6:
        raise ValueError(f"{label} has zero horizontal length")
    return delta[0] / length, delta[1] / length


def _left_normal(direction: tuple[float, float]) -> tuple[float, float]:
    return -direction[1], direction[0]


def _line_intersection(p1: Point2, d1: Point2, p2: Point2, d2: Point2) -> Point2 | None:
    cross = d1[0] * d2[1] - d1[1] * d2[0]
    if abs(cross) <= 1e-8:
        return None
    delta = (p2[0] - p1[0], p2[1] - p1[1])
    t = (delta[0] * d2[1] - delta[1] * d2[0]) / cross
    return p1[0] + d1[0] * t, p1[1] + d1[1] * t


def _offset_point(point: Point3, normal: Point2, distance: float) -> Point2:
    return point[0] + normal[0] * distance, point[1] + normal[1] * distance


def _sections(points: list[Point3], offsets: list[float], miter_limit: float) -> list[dict[str, Any]]:
    directions = [_normalize_xy(_sub(points[index + 1], points[index]), f"segment {index}") for index in range(len(points) - 1)]
    normals = [_left_normal(direction) for direction in directions]
    sections: list[dict[str, Any]] = []
    for index, point in enumerate(points):
        values: dict[float, Point2] = {}
        if index == 0:
            normal = normals[0]
            values = {offset: _offset_point(point, normal, offset) for offset in offsets}
        elif index == len(points) - 1:
            normal = normals[-1]
            values = {offset: _offset_point(point, normal, offset) for offset in offsets}
        else:
            prev_dir = directions[index - 1]
            next_dir = directions[index]
            prev_normal = normals[index - 1]
            next_normal = normals[index]
            for offset in offsets:
                prev_origin = _offset_point(point, prev_normal, offset)
                next_origin = _offset_point(point, next_normal, offset)
                intersection = _line_intersection(prev_origin, prev_dir, next_origin, next_dir)
                if intersection is None:
                    intersection = prev_origin
                if abs(offset) > 1e-6:
                    ratio = math.hypot(intersection[0] - point[0], intersection[1] - point[1]) / abs(offset)
                    if ratio > miter_limit:
                        raise ValueError(f"miter at points[{index}] exceeds miterLimit: {ratio:.3f} > {miter_limit:.3f}")
                values[offset] = intersection
        sections.append({"xy": values, "z": point[2]})
    return sections


def _section_vertex(section: dict[str, Any], vertex: ProfileVertex) -> Point3:
    offset, z = vertex
    xy = section["xy"][offset]
    return [xy[0], xy[1], section["z"] + z]


def _section_quad(section: dict[str, Any], strip: dict[str, Any]) -> list[Point3]:
    return [_section_vertex(section, vertex) for vertex in strip["quad"]]


def _strip_points(start: dict[str, Any], end: dict[str, Any], strip: dict[str, Any]) -> list[Point3]:
    a0, a1, a2, a3 = _section_quad(start, strip)
    b0, b1, b2, b3 = _section_quad(end, strip)
    return [a0, a1, b1, b0, a3, a2, b2, b3]


def _seam(key: str, vertices: list[Point3]) -> dict[str, Any]:
    return {"key": key, "vertices": vertices}


def _cap_points(section: dict[str, Any], strip: dict[str, Any], direction: Point2, sign: float, thickness: float) -> list[Point3]:
    a0, a1, a2, a3 = _section_quad(section, strip)

    def shifted(point: Point3) -> Point3:
        return [point[0] + direction[0] * sign * thickness, point[1] + direction[1] * sign * thickness, point[2]]

    return [a0, a1, a2, a3, shifted(a0), shifted(a1), shifted(a2), shifted(a3)]


def _material_for_part(default_material: str, part_materials: dict[str, str], part: str, texture_role: str) -> str:
    for key in (part, texture_role):
        if key in part_materials:
            return part_materials[key]
    if part.startswith("cap_") and "cap" in part_materials:
        return part_materials["cap"]
    if part.startswith("ceiling_") and "ceiling" in part_materials:
        return part_materials["ceiling"]
    if part.startswith("pipe_") and "pipe" in part_materials:
        return part_materials["pipe"]
    if part.startswith("wall_") and "wall" in part_materials:
        return part_materials["wall"]
    return default_material


def build_sweep_ir(
    *,
    name: str,
    module_id: str,
    route_id: str,
    points: list[Point3],
    profile_strips: list[dict[str, Any]],
    material: str = DEFAULT_MATERIAL,
    part_materials: dict[str, str] | None = None,
    texture_policy: dict[str, Any] | None = None,
    grid: int = 16,
    corner_mode: str = "miter",
    miter_limit: float = 4.0,
    cap_ends: bool = True,
    cap_thickness: float = 16.0,
    recipe: str = "path_sweep",
) -> dict[str, Any]:
    if corner_mode != "miter":
        raise ValueError("cornerMode must be miter")
    if not profile_strips:
        raise ValueError("profile must include at least one strip")
    if not cap_ends and len(points) < 3:
        raise ValueError("capEnds false requires at least three points so internal shell seams can be annotated")
    texture_policy = normalize_texture_policy(texture_policy)
    part_materials = part_materials or {}
    offsets = sorted({float(vertex[0]) for strip in profile_strips for vertex in strip["quad"]})
    sections = _sections(points, offsets, miter_limit)
    directions = [_normalize_xy(_sub(points[index + 1], points[index]), f"segment {index}") for index in range(len(points) - 1)]
    operations: list[dict[str, Any]] = []

    for segment_index in range(len(sections) - 1):
        start = sections[segment_index]
        end = sections[segment_index + 1]
        for strip_index, strip in enumerate(profile_strips):
            part = str(strip["part"])
            texture_role = str(strip.get("textureRole", part))
            shell_seams = []
            if segment_index > 0:
                shell_seams.append(_seam(f"internal:{segment_index}:{part}", _section_quad(start, strip)))
            if segment_index < len(sections) - 2:
                shell_seams.append(_seam(f"internal:{segment_index + 1}:{part}", _section_quad(end, strip)))
            if cap_ends and segment_index == 0:
                shell_seams.append(_seam(f"cap:start:{part}", _section_quad(start, strip)))
            if cap_ends and segment_index == len(sections) - 2:
                shell_seams.append(_seam(f"cap:end:{part}", _section_quad(end, strip)))
            operations.append(
                op_polyhedron(
                    _strip_points(start, end, strip),
                    part=part,
                    role=str(strip.get("role", "shell")),
                    order=segment_index * 100 + strip_index + 1,
                    material=_material_for_part(material, part_materials, part, texture_role),
                    extra_metadata={
                        "moduleId": module_id,
                        "routeId": route_id,
                        "recipe": recipe,
                        "profileType": str(strip.get("profileType", "custom")),
                        "textureRole": texture_role,
                        "texturePolicy": texture_policy,
                        "shellSeams": shell_seams,
                    },
                )
            )

    if cap_ends:
        for cap_side, section, direction, sign, order_base in (
            ("start", sections[0], directions[0], -1.0, 9000),
            ("end", sections[-1], directions[-1], 1.0, 9500),
        ):
            for strip_index, strip in enumerate(profile_strips):
                source_part = str(strip["part"])
                cap_part = f"cap_{cap_side}"
                operations.append(
                    op_polyhedron(
                        _cap_points(section, strip, direction, sign, cap_thickness),
                        part=cap_part,
                        role="shell",
                        order=order_base + strip_index + 1,
                        material=_material_for_part(material, part_materials, cap_part, "cap"),
                        extra_metadata={
                            "moduleId": module_id,
                            "routeId": route_id,
                            "recipe": recipe,
                            "profileType": str(strip.get("profileType", "custom")),
                            "sourcePart": source_part,
                            "textureRole": "cap",
                            "texturePolicy": texture_policy,
                            "shellSeams": [_seam(f"cap:{cap_side}:{source_part}", _section_quad(section, strip))],
                        },
                    )
                )

    return make_ir(
        name=name,
        module_id=module_id,
        route_id=route_id,
        role="shell",
        material=material,
        grid=grid,
        operations=operations,
        extra_metadata={"recipe": recipe, "texturePolicy": texture_policy},
    )
