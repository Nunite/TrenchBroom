#!/usr/bin/env python3
"""Small helpers for TrenchBroom MCP skill recipes.

Recipes intentionally emit plain IR JSON only. They do not talk to TrenchBroom,
MCP, or tb2 directly; C++ MCP remains responsible for transactions, guards,
validation, selector/module tracking, and review.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any


DEFAULT_MATERIAL = "__TB_empty"
GENERATED_BY = "trenchbroom-mcp-scene-workflow"
IR_SCHEMA_VERSION = 1


def load_params(path: str | None) -> dict[str, Any]:
    if not path:
        return {}
    with Path(path).open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError("params JSON must be an object")
    return data


def write_json(path: str, data: dict[str, Any]) -> None:
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8", newline="\n") as handle:
        json.dump(data, handle, indent=2)
        handle.write("\n")


def number(params: dict[str, Any], key: str, default: float) -> float:
    value = params.get(key, default)
    if not isinstance(value, (int, float)):
        raise ValueError(f"{key} must be a number")
    return float(value)


def integer(params: dict[str, Any], key: str, default: int) -> int:
    value = params.get(key, default)
    if not isinstance(value, int):
        raise ValueError(f"{key} must be an integer")
    return value


def boolean(params: dict[str, Any], key: str, default: bool) -> bool:
    value = params.get(key, default)
    if not isinstance(value, bool):
        raise ValueError(f"{key} must be a boolean")
    return value


def string(params: dict[str, Any], key: str, default: str) -> str:
    value = params.get(key, default)
    if not isinstance(value, str):
        raise ValueError(f"{key} must be a string")
    return value


def vec3(params: dict[str, Any], key: str, default: list[float]) -> list[float]:
    value = params.get(key, default)
    if (
        not isinstance(value, list)
        or len(value) != 3
        or not all(isinstance(component, (int, float)) for component in value)
    ):
        raise ValueError(f"{key} must be [x, y, z]")
    return [float(value[0]), float(value[1]), float(value[2])]


def make_ir(
    *,
    name: str,
    module_id: str,
    material: str,
    grid: int,
    operations: list[dict[str, Any]],
    entities: list[dict[str, Any]] | None = None,
    route_id: str | None = None,
    role: str | None = None,
    extra_metadata: dict[str, Any] | None = None,
) -> dict[str, Any]:
    metadata: dict[str, Any] = {
        "moduleId": module_id,
        "generatedBy": GENERATED_BY,
    }
    if route_id:
        metadata["routeId"] = route_id
    if role:
        metadata["role"] = role
    if extra_metadata:
        metadata.update(extra_metadata)
    result: dict[str, Any] = {
        "schemaVersion": IR_SCHEMA_VERSION,
        "name": name,
        "moduleId": module_id,
        "defaultMetadata": metadata,
        "grid": grid,
        "material": material,
        "operations": operations,
    }
    if entities:
        result["entities"] = entities
    return result


def op_box(
    minimum: list[float],
    maximum: list[float],
    *,
    part: str,
    role: str,
    order: int | None = None,
    material: str | None = None,
    extra_metadata: dict[str, Any] | None = None,
) -> dict[str, Any]:
    metadata: dict[str, Any] = {"part": part, "role": role}
    if order is not None:
        metadata["order"] = order
    if extra_metadata:
        metadata.update(extra_metadata)
    op: dict[str, Any] = {
        "type": "box",
        "min": minimum,
        "max": maximum,
        "metadata": metadata,
    }
    if material:
        op["material"] = material
    return op


def op_ramp_between(
    start: list[float],
    end: list[float],
    *,
    width: float,
    thickness: float,
    part: str,
    role: str,
    order: int,
    material: str | None = None,
    extra_metadata: dict[str, Any] | None = None,
) -> dict[str, Any]:
    metadata: dict[str, Any] = {"part": part, "role": role, "order": order}
    if extra_metadata:
        metadata.update(extra_metadata)
    op: dict[str, Any] = {
        "type": "ramp_between",
        "start": start,
        "end": end,
        "width": width,
        "thickness": thickness,
        "metadata": metadata,
    }
    if material:
        op["material"] = material
    return op


def entity(
    classname: str,
    origin: list[float],
    properties: dict[str, Any] | None = None,
) -> dict[str, Any]:
    result: dict[str, Any] = {"classname": classname, "origin": origin}
    clean_properties = {
        key: str(value)
        for key, value in (properties or {}).items()
        if value is not None and str(value) != ""
    }
    if clean_properties:
        result["properties"] = clean_properties
    return result


def polar_point(
    center: list[float], radius: float, angle_degrees: float, z: float
) -> list[float]:
    angle = math.radians(angle_degrees)
    return [
        center[0] + math.cos(angle) * radius,
        center[1] + math.sin(angle) * radius,
        center[2] + z,
    ]


def _type_matches(value: Any, type_name: str) -> bool:
    if type_name == "string":
        return isinstance(value, str)
    if type_name == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if type_name == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if type_name == "boolean":
        return isinstance(value, bool)
    if type_name == "vec3":
        return (
            isinstance(value, list)
            and len(value) == 3
            and all(isinstance(component, (int, float)) and not isinstance(component, bool) for component in value)
        )
    return True


def merge_defaults(manifest: dict[str, Any], params: dict[str, Any]) -> dict[str, Any]:
    defaults = manifest.get("defaultParams", {})
    if not isinstance(defaults, dict):
        raise ValueError("recipe manifest defaultParams must be an object")
    return {**defaults, **params}


def validate_params(manifest: dict[str, Any], params: dict[str, Any]) -> list[str]:
    parameters = manifest.get("parameters", [])
    if not isinstance(parameters, list):
        raise ValueError("recipe manifest parameters must be a list")
    specs = {spec["name"]: spec for spec in parameters if isinstance(spec, dict) and "name" in spec}
    unknown = sorted(set(params) - set(specs))
    if unknown:
        raise ValueError(f"unknown recipe parameter(s): {', '.join(unknown)}")

    warnings: list[str] = []
    for name, spec in specs.items():
        required = bool(spec.get("required", False))
        if required and name not in params:
            raise ValueError(f"missing required recipe parameter: {name}")
        if name not in params:
            continue
        value = params[name]
        type_name = str(spec.get("type", "any"))
        if not _type_matches(value, type_name):
            raise ValueError(f"{name} must be {type_name}")
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            if "min" in spec and value < spec["min"]:
                raise ValueError(f"{name} must be >= {spec['min']}")
            if "max" in spec and value > spec["max"]:
                raise ValueError(f"{name} must be <= {spec['max']}")
        if spec.get("warning"):
            warnings.append(str(spec["warning"]))
    return warnings


def _extend_bounds(
    bounds: list[list[float]] | None, minimum: list[float], maximum: list[float]
) -> list[list[float]]:
    if bounds is None:
        return [minimum[:], maximum[:]]
    for i in range(3):
        bounds[0][i] = min(bounds[0][i], minimum[i])
        bounds[1][i] = max(bounds[1][i], maximum[i])
    return bounds


def _operation_bounds(operation: dict[str, Any]) -> tuple[list[float], list[float]] | None:
    op_type = operation.get("type")
    if op_type == "box" and "min" in operation and "max" in operation:
        return [float(v) for v in operation["min"]], [float(v) for v in operation["max"]]
    if op_type == "ramp_between" and "start" in operation and "end" in operation:
        start = [float(v) for v in operation["start"]]
        end = [float(v) for v in operation["end"]]
        half_width = float(operation.get("width", 0)) / 2
        thickness = float(operation.get("thickness", 0))
        minimum = [min(start[i], end[i]) for i in range(3)]
        maximum = [max(start[i], end[i]) for i in range(3)]
        return (
            [minimum[0] - half_width, minimum[1] - half_width, minimum[2] - thickness],
            [maximum[0] + half_width, maximum[1] + half_width, maximum[2]],
        )
    if op_type in ("arc_ramp", "helical_ramp") and "center" in operation:
        center = [float(v) for v in operation["center"]]
        radius = float(operation.get("radius", 0))
        width = float(operation.get("width", 0))
        rise = float(operation.get("rise", 0))
        thickness = float(operation.get("thickness", 0))
        start_angle = float(operation.get("startAngle", 0))
        turn_degrees = float(operation.get("turnDegrees", 360))
        segments = max(4, int(operation.get("segments", 16)))
        bounds: list[list[float]] | None = None
        for index in range(segments + 1):
            angle = start_angle + turn_degrees * index / segments
            z = rise * index / segments
            for side_radius in (radius - width / 2, radius + width / 2):
                p = polar_point(center, side_radius, angle, z)
                bounds = _extend_bounds(bounds, [p[0], p[1], p[2] - thickness], p)
        return None if bounds is None else (bounds[0], bounds[1])
    return None


def summarize_ir(ir: dict[str, Any]) -> dict[str, Any]:
    operations = ir.get("operations", [])
    entities = ir.get("entities", [])
    bounds: list[list[float]] | None = None
    parts: dict[str, int] = {}
    roles: dict[str, int] = {}
    operations_with_part_role = 0
    operations_with_order = 0
    for operation in operations if isinstance(operations, list) else []:
        if not isinstance(operation, dict):
            continue
        op_bounds = _operation_bounds(operation)
        if op_bounds:
            bounds = _extend_bounds(bounds, op_bounds[0], op_bounds[1])
        metadata = operation.get("metadata", {})
        if isinstance(metadata, dict):
            part = metadata.get("part")
            role = metadata.get("role")
            if part:
                parts[str(part)] = parts.get(str(part), 0) + 1
            if role:
                roles[str(role)] = roles.get(str(role), 0) + 1
            if part and role:
                operations_with_part_role += 1
            if "order" in metadata:
                operations_with_order += 1
    return {
        "moduleId": ir.get("moduleId"),
        "operationCount": len(operations) if isinstance(operations, list) else 0,
        "entityCount": len(entities) if isinstance(entities, list) else 0,
        "bounds": bounds,
        "parts": dict(sorted(parts.items())),
        "roles": dict(sorted(roles.items())),
        "metadataCoverage": {
            "operationsWithPartRole": operations_with_part_role,
            "operationsWithOrder": operations_with_order,
        },
    }


def validate_ir(ir: dict[str, Any], manifest: dict[str, Any]) -> dict[str, Any]:
    errors: list[str] = []
    warnings: list[str] = []
    if not isinstance(ir, dict):
        errors.append("IR root must be an object")
        return {"valid": False, "errors": errors, "warnings": warnings, "summary": {}}
    if ir.get("schemaVersion") != IR_SCHEMA_VERSION:
        errors.append(f"IR schemaVersion must be {IR_SCHEMA_VERSION}")
    if not ir.get("moduleId"):
        errors.append("IR must include moduleId")
    default_metadata = ir.get("defaultMetadata")
    if not isinstance(default_metadata, dict):
        errors.append("IR must include defaultMetadata")
    else:
        for key in ("moduleId", "generatedBy"):
            if not default_metadata.get(key):
                errors.append(f"defaultMetadata must include {key}")
    operations = ir.get("operations")
    if not isinstance(operations, list) or not operations:
        errors.append("IR must include at least one operation")
    else:
        for index, operation in enumerate(operations):
            if not isinstance(operation, dict):
                errors.append(f"operation[{index}] must be an object")
                continue
            metadata = operation.get("metadata")
            if not isinstance(metadata, dict):
                errors.append(f"operation[{index}] must include metadata")
                continue
            for key in ("part", "role", "order"):
                if key not in metadata:
                    errors.append(f"operation[{index}] metadata must include {key}")
    entities = ir.get("entities", [])
    if entities is not None and not isinstance(entities, list):
        errors.append("entities must be a list when present")

    summary = summarize_ir(ir)
    output = manifest.get("output", {})
    expected_parts = set(output.get("requiredParts", output.get("parts", [])))
    actual_parts = set(summary.get("parts", {}).keys())
    missing_parts = sorted(expected_parts - actual_parts)
    if missing_parts:
        warnings.append(f"expected part(s) not emitted: {', '.join(missing_parts)}")
    warnings.extend(str(item) for item in manifest.get("expectedWarnings", []))
    return {"valid": not errors, "errors": errors, "warnings": warnings, "summary": summary}


def common_arg_parser(description: str) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--params", help="Input recipe params JSON file")
    parser.add_argument("--out", help="Output IR JSON file")
    parser.add_argument("--describe", action="store_true", help="Print recipe manifest and exit")
    parser.add_argument("--validate-only", action="store_true", help="Validate params and generated IR without writing --out")
    return parser


def run_recipe_cli(description: str, manifest: dict[str, Any], build_fn: Any) -> None:
    parser = common_arg_parser(description)
    args = parser.parse_args()
    if args.describe:
        print(json.dumps(manifest, indent=2))
        return

    raw_params = load_params(args.params)
    merged_params = merge_defaults(manifest, raw_params)
    param_warnings = validate_params(manifest, merged_params)
    ir = build_fn(merged_params)
    validation = validate_ir(ir, manifest)
    validation["recipe"] = manifest.get("id")
    validation["params"] = merged_params
    validation["warnings"] = param_warnings + validation["warnings"]

    if args.validate_only:
        print(json.dumps(validation, indent=2))
        if not validation["valid"]:
            raise SystemExit(1)
        return

    if not args.out:
        parser.error("--out is required unless --describe or --validate-only is used")
    if not validation["valid"]:
        raise ValueError("; ".join(validation["errors"]))
    write_json(args.out, ir)
