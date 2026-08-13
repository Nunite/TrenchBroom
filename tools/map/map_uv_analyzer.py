#!/usr/bin/env python3
"""Inspect Valve 220 MAP brush geometry and UV continuity.

Examples:
  python tools/map/map_uv_analyzer.py level.map
  python tools/map/map_uv_analyzer.py level.map --entity 3 --brush-range 0:42
  python tools/map/map_uv_analyzer.py level.map --classname func_detail --sweep-segments 6
  python tools/map/map_uv_analyzer.py level.map --entity 3 --json report.json

Sweep mode relies on TrenchBroom's output order: each selected source face produces one
contiguous run of ``segments`` brushes. Faces are tracked through shared station edges,
so the report remains useful when a convex hull triangulates a nominally quad cap.

Brush summaries separate three diagnostics: ``maxRotErr`` is the stored rotation's
distance from a quarter turn, ``maxShear`` is the absolute dot product of the in-plane U
and V directions, and ``maxAxis.N`` measures UV-axis leakage along the face normal.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from dataclasses import asdict, dataclass, field
from itertools import combinations
from pathlib import Path
from typing import Iterable, Sequence


Vec3 = tuple[float, float, float]


NUMBER = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"
FACE_RE = re.compile(
    rf"^\s*\(\s*({NUMBER})\s+({NUMBER})\s+({NUMBER})\s*\)\s*"
    rf"\(\s*({NUMBER})\s+({NUMBER})\s+({NUMBER})\s*\)\s*"
    rf"\(\s*({NUMBER})\s+({NUMBER})\s+({NUMBER})\s*\)\s*"
    rf"(?:\"([^\"]+)\"|(\S+))\s*"
    rf"\[\s*({NUMBER})\s+({NUMBER})\s+({NUMBER})\s+({NUMBER})\s*\]\s*"
    rf"\[\s*({NUMBER})\s+({NUMBER})\s+({NUMBER})\s+({NUMBER})\s*\]\s*"
    rf"({NUMBER})\s+({NUMBER})\s+({NUMBER})(?:\s+.*)?$"
)
PROPERTY_RE = re.compile(r'^\s*"([^"]+)"\s+"([^"]*)"\s*$')
ENTITY_RE = re.compile(r"^\s*//\s*entity\s+(\d+)\s*$")
BRUSH_RE = re.compile(r"^\s*//\s*brush\s+(\d+)\s*$")


def add(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def mul(a: Vec3, scalar: float) -> Vec3:
    return (a[0] * scalar, a[1] * scalar, a[2] * scalar)


def dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a: Vec3, b: Vec3) -> Vec3:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def length(a: Vec3) -> float:
    return math.sqrt(dot(a, a))


def normalize(a: Vec3) -> Vec3:
    magnitude = length(a)
    return mul(a, 1.0 / magnitude) if magnitude > 1e-12 else (0.0, 0.0, 0.0)


def average(points: Sequence[Vec3]) -> Vec3:
    if not points:
        return (0.0, 0.0, 0.0)
    total = (0.0, 0.0, 0.0)
    for point in points:
        total = add(total, point)
    return mul(total, 1.0 / len(points))


def distance(a: Vec3, b: Vec3) -> float:
    return length(sub(a, b))


def nearly_equal(a: Vec3, b: Vec3, epsilon: float) -> bool:
    return distance(a, b) <= epsilon


def unique_points(points: Iterable[Vec3], epsilon: float) -> list[Vec3]:
    result: list[Vec3] = []
    for point in points:
        if not any(nearly_equal(point, existing, epsilon) for existing in result):
            result.append(point)
    return result


def solve_plane_intersection(
    plane_a: tuple[Vec3, float],
    plane_b: tuple[Vec3, float],
    plane_c: tuple[Vec3, float],
) -> Vec3 | None:
    a, da = plane_a
    b, db = plane_b
    c, dc = plane_c
    determinant = dot(a, cross(b, c))
    if abs(determinant) < 1e-10:
        return None
    numerator = add(add(mul(cross(b, c), da), mul(cross(c, a), db)), mul(cross(a, b), dc))
    return mul(numerator, 1.0 / determinant)


def rotate_between(vector: Vec3, old_normal: Vec3, new_normal: Vec3) -> Vec3:
    old_n = normalize(old_normal)
    new_n = normalize(new_normal)
    cosine = max(-1.0, min(1.0, dot(old_n, new_n)))
    axis_raw = cross(old_n, new_n)
    sine = length(axis_raw)
    if sine < 1e-10:
        if cosine > 0.0:
            return vector
        fallback = normalize(cross(old_n, (1.0, 0.0, 0.0)))
        if length(fallback) < 1e-10:
            fallback = normalize(cross(old_n, (0.0, 1.0, 0.0)))
        return sub(mul(fallback, 2.0 * dot(fallback, vector)), vector)
    axis = mul(axis_raw, 1.0 / sine)
    return add(
        add(mul(vector, cosine), mul(cross(axis, vector), sine)),
        mul(axis, dot(axis, vector) * (1.0 - cosine)),
    )


def signed_angle_degrees(a: Vec3, b: Vec3, normal: Vec3) -> float:
    a_n = normalize(a)
    b_n = normalize(b)
    n_n = normalize(normal)
    return math.degrees(math.atan2(dot(n_n, cross(a_n, b_n)), dot(a_n, b_n)))


def fmt(value: float, width: int = 8) -> str:
    if abs(value) < 0.00005:
        value = 0.0
    return f"{value:{width}.3f}"


@dataclass
class Face:
    line: int
    points: tuple[Vec3, Vec3, Vec3]
    material: str
    u_axis: Vec3
    u_offset: float
    v_axis: Vec3
    v_offset: float
    rotation: float
    scale_u: float
    scale_v: float
    normal: Vec3 = field(init=False)
    distance: float = field(init=False)

    def __post_init__(self) -> None:
        # Valve MAP point order is clockwise when viewed from outside.
        self.normal = normalize(
            cross(sub(self.points[2], self.points[0]), sub(self.points[1], self.points[0]))
        )
        self.distance = dot(self.normal, self.points[0])

    def contains_vertex(self, point: Vec3, epsilon: float) -> bool:
        return abs(dot(self.normal, point) - self.distance) <= epsilon

    def uv_metrics(self) -> dict[str, float]:
        u = normalize(self.u_axis)
        v = normalize(self.v_axis)
        projected_u = normalize(sub(u, mul(self.normal, dot(u, self.normal))))
        projected_v = normalize(sub(v, mul(self.normal, dot(v, self.normal))))
        handedness = dot(normalize(cross(projected_u, projected_v)), self.normal)
        return {
            "u_length": length(self.u_axis),
            "v_length": length(self.v_axis),
            "u_normal_dot": dot(u, self.normal),
            "v_normal_dot": dot(v, self.normal),
            "uv_dot": dot(projected_u, projected_v),
            "handedness": handedness,
        }


@dataclass
class Brush:
    index: int
    line: int
    faces: list[Face] = field(default_factory=list)
    vertices: list[Vec3] = field(default_factory=list)
    center: Vec3 = (0.0, 0.0, 0.0)

    def rebuild_vertices(self, epsilon: float) -> None:
        planes = [(face.normal, face.distance) for face in self.faces]
        candidates: list[Vec3] = []
        for plane_a, plane_b, plane_c in combinations(planes, 3):
            point = solve_plane_intersection(plane_a, plane_b, plane_c)
            if point is None:
                continue
            if all(dot(normal, point) <= plane_distance + epsilon for normal, plane_distance in planes):
                candidates.append(point)
        self.vertices = unique_points(candidates, epsilon)
        self.center = average(self.vertices)

    def face_vertices(self, face: Face, epsilon: float) -> list[Vec3]:
        return [point for point in self.vertices if face.contains_vertex(point, epsilon)]


@dataclass
class Entity:
    index: int
    line: int
    properties: dict[str, str] = field(default_factory=dict)
    brushes: list[Brush] = field(default_factory=list)


def parse_face(line: str, line_number: int) -> Face | None:
    match = FACE_RE.match(line)
    if not match:
        return None
    groups = match.groups()
    numbers = [float(value) for value in groups[:9]]
    material = groups[9] or groups[10]
    uv = [float(value) for value in groups[11:]]
    return Face(
        line=line_number,
        points=(tuple(numbers[0:3]), tuple(numbers[3:6]), tuple(numbers[6:9])),  # type: ignore[arg-type]
        material=material,
        u_axis=tuple(uv[0:3]),  # type: ignore[arg-type]
        u_offset=uv[3],
        v_axis=tuple(uv[4:7]),  # type: ignore[arg-type]
        v_offset=uv[7],
        rotation=uv[8],
        scale_u=uv[9],
        scale_v=uv[10],
    )


def parse_map(path: Path, epsilon: float) -> list[Entity]:
    entities: list[Entity] = []
    current_entity: Entity | None = None
    current_brush: Brush | None = None
    for line_number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        if match := ENTITY_RE.match(line):
            current_entity = Entity(int(match.group(1)), line_number)
            entities.append(current_entity)
            current_brush = None
            continue
        if match := BRUSH_RE.match(line):
            if current_entity is None:
                continue
            current_brush = Brush(int(match.group(1)), line_number)
            current_entity.brushes.append(current_brush)
            continue
        if current_brush is not None:
            face = parse_face(line, line_number)
            if face is not None:
                current_brush.faces.append(face)
                continue
        if current_entity is not None and current_brush is None:
            if match := PROPERTY_RE.match(line):
                current_entity.properties[match.group(1)] = match.group(2)

    for entity in entities:
        for brush in entity.brushes:
            brush.rebuild_vertices(epsilon)
    return entities


def shared_points(left: Brush, right: Brush, epsilon: float) -> list[Vec3]:
    return unique_points(
        (point for point in left.vertices if any(nearly_equal(point, other, epsilon) for other in right.vertices)),
        epsilon,
    )


def face_shared_edge(face: Face, station: Sequence[Vec3], epsilon: float) -> tuple[Vec3, Vec3] | None:
    points = [point for point in station if face.contains_vertex(point, epsilon)]
    points = unique_points(points, epsilon)
    if len(points) < 2:
        return None
    return max(combinations(points, 2), key=lambda pair: distance(pair[0], pair[1]))


def station_sets(run: Sequence[Brush], epsilon: float) -> list[list[Vec3]]:
    stations: list[list[Vec3]] = [[] for _ in range(len(run) + 1)]
    for index in range(1, len(run)):
        stations[index] = shared_points(run[index - 1], run[index], epsilon)
    if len(run) == 1:
        return stations
    stations[0] = [
        point
        for point in run[0].vertices
        if not any(nearly_equal(point, shared, epsilon) for shared in stations[1])
    ]
    stations[-1] = [
        point
        for point in run[-1].vertices
        if not any(nearly_equal(point, shared, epsilon) for shared in stations[-2])
    ]
    return stations


def side_faces(
    brush: Brush, start: Sequence[Vec3], end: Sequence[Vec3], epsilon: float
) -> list[Face]:
    result: list[Face] = []
    for face in brush.faces:
        start_count = sum(face.contains_vertex(point, epsilon) for point in start)
        end_count = sum(face.contains_vertex(point, epsilon) for point in end)
        if start_count >= 2 and end_count >= 2:
            result.append(face)
    return result


def match_face_by_edge(
    faces: Sequence[Face], edge: tuple[Vec3, Vec3], epsilon: float
) -> Face | None:
    return next(
        (
            face
            for face in faces
            if face.contains_vertex(edge[0], epsilon) and face.contains_vertex(edge[1], epsilon)
        ),
        None,
    )


def analyze_track(faces: Sequence[Face | None]) -> list[dict[str, object]]:
    result: list[dict[str, object]] = []
    previous: Face | None = None
    for segment, face in enumerate(faces):
        if face is None:
            result.append({"segment": segment, "missing": True})
            previous = None
            continue
        metrics = face.uv_metrics()
        item: dict[str, object] = {
            "segment": segment,
            "line": face.line,
            "material": face.material,
            "rotation": face.rotation,
            "normal": face.normal,
            "u_axis": face.u_axis,
            "v_axis": face.v_axis,
            "scale": (face.scale_u, face.scale_v),
            **metrics,
        }
        if previous is not None:
            transported_u = rotate_between(previous.u_axis, previous.normal, face.normal)
            transported_v = rotate_between(previous.v_axis, previous.normal, face.normal)
            item["u_transport_delta"] = signed_angle_degrees(transported_u, face.u_axis, face.normal)
            item["v_transport_delta"] = signed_angle_degrees(transported_v, face.v_axis, face.normal)
            item["stored_rotation_delta"] = (
                (face.rotation - previous.rotation + 180.0) % 360.0 - 180.0
            )
        result.append(item)
        previous = face
    return result


def quarter_turn_residual(rotation: float) -> float:
    normalized = rotation % 360.0
    return min(abs(normalized - quarter_turn) for quarter_turn in (0.0, 90.0, 180.0, 270.0, 360.0))


def analyze_brushes(
    entity: Entity,
    epsilon: float,
    brush_range: tuple[int, int] | None,
) -> dict[str, object]:
    brushes = entity.brushes
    if brush_range is not None:
        start, end = brush_range
        brushes = [brush for brush in brushes if start <= brush.index < end]

    brush_reports: list[dict[str, object]] = []
    for brush in brushes:
        face_reports: list[dict[str, object]] = []
        for face in brush.faces:
            vertices = brush.face_vertices(face, epsilon)
            metrics = face.uv_metrics()
            face_reports.append(
                {
                    "line": face.line,
                    "material": face.material,
                    "vertex_count": len(vertices),
                    "center": average(vertices),
                    "normal": face.normal,
                    "rotation": face.rotation,
                    "quarter_turn_residual": quarter_turn_residual(face.rotation),
                    "u_axis": face.u_axis,
                    "u_offset": face.u_offset,
                    "v_axis": face.v_axis,
                    "v_offset": face.v_offset,
                    "scale": (face.scale_u, face.scale_v),
                    **metrics,
                }
            )

        brush_reports.append(
            {
                "brush": brush.index,
                "center": brush.center,
                "face_count": len(brush.faces),
                "vertex_count": len(brush.vertices),
                "non_quarter_turn_faces": sum(
                    report["quarter_turn_residual"] > 0.01 for report in face_reports
                ),
                "max_quarter_turn_residual": max(
                    (report["quarter_turn_residual"] for report in face_reports), default=0.0
                ),
                "max_abs_uv_dot": max(
                    (abs(report["uv_dot"]) for report in face_reports), default=0.0
                ),
                "max_axis_normal_dot": max(
                    (
                        max(abs(report["u_normal_dot"]), abs(report["v_normal_dot"]))
                        for report in face_reports
                    ),
                    default=0.0,
                ),
                "faces": face_reports,
            }
        )

    return {
        "entity": entity.index,
        "classname": entity.properties.get("classname", ""),
        "entity_brush_count": len(entity.brushes),
        "brush_count": len(brushes),
        "brush_range": brush_range,
        "brushes": brush_reports,
    }


def analyze_sweep(
    entity: Entity,
    segments: int,
    epsilon: float,
    brush_range: tuple[int, int] | None,
) -> dict[str, object]:
    brushes = entity.brushes
    if brush_range is not None:
        start, end = brush_range
        brushes = [brush for brush in brushes if start <= brush.index < end]
    run_count, remainder = divmod(len(brushes), segments)
    report: dict[str, object] = {
        "entity": entity.index,
        "classname": entity.properties.get("classname", ""),
        "entity_brush_count": len(entity.brushes),
        "brush_count": len(brushes),
        "brush_range": brush_range,
        "segments": segments,
        "source_runs": run_count,
        "remainder": remainder,
        "runs": [],
    }
    runs: list[dict[str, object]] = []
    for run_index in range(run_count):
        run = brushes[run_index * segments : (run_index + 1) * segments]
        stations = station_sets(run, epsilon)
        first_sides = side_faces(run[0], stations[0], stations[1], epsilon)
        tracks: list[list[Face | None]] = []
        for first_face in first_sides:
            track: list[Face | None] = [first_face]
            current = first_face
            for segment_index in range(1, len(run)):
                edge = face_shared_edge(current, stations[segment_index], epsilon)
                candidates = side_faces(
                    run[segment_index],
                    stations[segment_index],
                    stations[segment_index + 1],
                    epsilon,
                )
                current = match_face_by_edge(candidates, edge, epsilon) if edge else None
                track.append(current)
                if current is None:
                    break
            track.extend([None] * (segments - len(track)))
            tracks.append(track)
        runs.append(
            {
                "run": run_index,
                "brushes": [brush.index for brush in run],
                "centers": [brush.center for brush in run],
                "station_vertex_counts": [len(station) for station in stations],
                "brush_face_counts": [len(brush.faces) for brush in run],
                "brush_vertex_counts": [len(brush.vertices) for brush in run],
                "tracks": [analyze_track(track) for track in tracks],
            }
        )
    report["runs"] = runs
    return report


def print_overview(entities: Sequence[Entity]) -> None:
    print("entity  classname                 brushes  faces  vertices")
    for entity in entities:
        print(
            f"{entity.index:6d}  {entity.properties.get('classname', ''):24.24s}"
            f"{len(entity.brushes):9d}{sum(len(brush.faces) for brush in entity.brushes):7d}"
            f"{sum(len(brush.vertices) for brush in entity.brushes):10d}"
        )


def print_sweep(report: dict[str, object]) -> None:
    print(
        f"entity {report['entity']} {report['classname']}: {report['brush_count']} brushes, "
        f"{report['source_runs']} source runs x {report['segments']} segments"
    )
    if report["remainder"]:
        print(f"warning: {report['remainder']} trailing brushes do not fit the segment count")
    for run in report["runs"]:  # type: ignore[assignment]
        print(
            f"\nrun {run['run']} brushes {run['brushes']} "
            f"faces={run['brush_face_counts']} vertices={run['brush_vertex_counts']} "
            f"stations={run['station_vertex_counts']}"
        )
        for track_index, track in enumerate(run["tracks"]):
            print(f"  track {track_index}")
            print("    seg  line  rotation   dRot    dUwrap  dVwrap  uvDot    U.N      V.N")
            for item in track:
                if item.get("missing"):
                    print(f"    {item['segment']:3d}  missing")
                    continue
                print(
                    f"    {item['segment']:3d} {item['line']:5d}"
                    f" {fmt(item['rotation'])} {fmt(item.get('stored_rotation_delta', 0.0), 7)}"
                    f" {fmt(item.get('u_transport_delta', 0.0), 8)}"
                    f" {fmt(item.get('v_transport_delta', 0.0), 8)}"
                    f" {fmt(item['uv_dot'], 7)} {fmt(item['u_normal_dot'], 8)}"
                    f" {fmt(item['v_normal_dot'], 8)}"
                )


def print_brushes(report: dict[str, object], face_details: bool) -> None:
    print(
        f"entity {report['entity']} {report['classname']}: {report['brush_count']} brushes"
    )
    print("brush  faces verts       center (x, y, z)       non90  maxRotErr  maxShear  maxAxis.N")
    for brush in report["brushes"]:  # type: ignore[assignment]
        center = brush["center"]
        print(
            f"{brush['brush']:5d} {brush['face_count']:6d} {brush['vertex_count']:5d}"
            f" ({fmt(center[0], 8)},{fmt(center[1], 8)},{fmt(center[2], 8)})"
            f" {brush['non_quarter_turn_faces']:6d}"
            f" {fmt(brush['max_quarter_turn_residual'], 10)}"
            f" {fmt(brush['max_abs_uv_dot'], 9)}"
            f" {fmt(brush['max_axis_normal_dot'], 10)}"
        )
        if not face_details:
            continue
        print("       line verts rotation  rotErr   uvDot    U.N      V.N       normal")
        for face in brush["faces"]:
            normal = face["normal"]
            print(
                f"      {face['line']:5d} {face['vertex_count']:5d}"
                f" {fmt(face['rotation'])} {fmt(face['quarter_turn_residual'], 7)}"
                f" {fmt(face['uv_dot'], 7)} {fmt(face['u_normal_dot'], 8)}"
                f" {fmt(face['v_normal_dot'], 8)}"
                f" ({fmt(normal[0], 6)},{fmt(normal[1], 6)},{fmt(normal[2], 6)})"
            )


def select_entities(
    entities: Sequence[Entity], entity_index: int | None, classname: str | None
) -> list[Entity]:
    return [
        entity
        for entity in entities
        if (entity_index is None or entity.index == entity_index)
        and (classname is None or entity.properties.get("classname") == classname)
    ]


def parse_brush_range(value: str) -> tuple[int, int]:
    try:
        start_text, end_text = value.split(":", 1)
        start, end = int(start_text), int(end_text)
    except ValueError as error:
        raise argparse.ArgumentTypeError("brush range must use START:END") from error
    if start < 0 or end <= start:
        raise argparse.ArgumentTypeError("brush range must satisfy 0 <= START < END")
    return start, end


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("map", type=Path, help="Valve 220 MAP file")
    parser.add_argument("--entity", type=int, help="Only analyze this entity index")
    parser.add_argument("--classname", help="Only analyze entities with this classname")
    parser.add_argument(
        "--sweep-segments",
        type=int,
        metavar="N",
        help="Group brushes as TrenchBroom Sweep output with N segments per source face",
    )
    parser.add_argument(
        "--brush-range",
        type=parse_brush_range,
        metavar="START:END",
        help="Only analyze brush indices in the half-open range START:END",
    )
    parser.add_argument(
        "--face-details",
        action="store_true",
        help="Print one diagnostic row per face in brush summary mode",
    )
    parser.add_argument("--epsilon", type=float, default=0.002, help="Geometry matching tolerance")
    parser.add_argument("--json", type=Path, help="Write the complete report as JSON")
    args = parser.parse_args(argv)

    if not args.map.is_file():
        parser.error(f"MAP file does not exist: {args.map}")
    if args.sweep_segments is not None and args.sweep_segments <= 0:
        parser.error("--sweep-segments must be positive")

    entities = parse_map(args.map, args.epsilon)
    selected = select_entities(entities, args.entity, args.classname)
    if not selected:
        print("No matching entities", file=sys.stderr)
        return 2

    if args.sweep_segments is None and args.brush_range is None:
        report: object = {
            "map": str(args.map),
            "entities": [asdict(entity) for entity in selected],
        }
        print_overview(selected)
    elif args.sweep_segments is None:
        brush_reports = [
            analyze_brushes(entity, args.epsilon, args.brush_range) for entity in selected
        ]
        report = {"map": str(args.map), "brush_reports": brush_reports}
        for brush_report in brush_reports:
            print_brushes(brush_report, args.face_details)
    else:
        reports = [
            analyze_sweep(entity, args.sweep_segments, args.epsilon, args.brush_range)
            for entity in selected
        ]
        report = {"map": str(args.map), "sweeps": reports}
        for sweep_report in reports:
            print_sweep(sweep_report)

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"\nwrote {args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
