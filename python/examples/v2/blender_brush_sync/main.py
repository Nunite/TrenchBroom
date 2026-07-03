import json
import os
import tempfile
import time
import uuid
from pathlib import Path

import tb2 as tb


PANEL_TITLE = "Blender Brush Sync"
SCHEMA = "tb.blenderBrushSync.v1"
SYNC_DIR = Path(os.environ.get("TEMP") or tempfile.gettempdir()) / "trenchbroom-blender-sync"
REQUEST_PATH = SYNC_DIR / "request.json"
RESPONSE_PATH = SYNC_DIR / "response.json"


def _as_vec3(value):
    return [float(value[0]), float(value[1]), float(value[2])]


def _as_uv(value):
    return [float(value[0]), float(value[1])]


def _write_json(path, payload):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = path.with_suffix(path.suffix + ".tmp")
    with temp_path.open("w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2)
    os.replace(temp_path, path)


def _existing_wad_path(path, doc):
    if not path:
        return None
    wad_path = Path(path)
    candidates = [wad_path]
    if not wad_path.is_absolute() and doc.path:
        candidates.append(Path(doc.path).parent / wad_path)
    for candidate in candidates:
        if candidate.exists() and candidate.suffix.lower() == ".wad":
            return str(candidate)
    return str(wad_path) if wad_path.suffix.lower() == ".wad" else None


def _wad_paths(doc):
    result = []
    seen = set()

    def add(path):
        resolved = _existing_wad_path(path, doc)
        if resolved and resolved.lower() not in seen:
            seen.add(resolved.lower())
            result.append(resolved)

    for entity in doc.entities:
        if entity.classname == "worldspawn":
            wad_value = entity.get("wad", "")
            for path in wad_value.split(";"):
                add(path.strip())
            break

    for collection in doc.material_collections:
        add(collection.path)
    return result


class BlenderBrushSync:
    def __init__(self):
        self.panel = tb.create_plugin_panel(PANEL_TITLE)
        self.session_id = None
        self.cache = {}
        self.pending_response_mtime = None
        self.timer_token = None
        self.build_ui()
        self.timer_token = tb.set_interval(self.poll_response, 1000)

    def build_ui(self):
        self.panel.clear()
        self.panel.add_label("Brush UV/material exchange with an open Blender project.")
        self.panel.add_button_callback("Send Selection", self.send_selection)
        self.panel.add_button_callback("Apply Pending", self.apply_pending)
        self.panel.add_label_named("status", "Ready.")

    def log(self, message):
        print(f"[{PANEL_TITLE}] {message}")
        self.panel.set_label_text("status", message)

    def selected_brushes(self):
        selection = tb.current_document().selection
        brushes = list(selection.brushes)
        if brushes:
            return brushes

        for entity in selection.all_entities:
            brushes.extend(entity.brushes)
        return brushes

    def export_brush(self, brush, brush_index):
        brush_id = f"brush{brush_index}"
        vertices = []
        vertex_ids = {}
        faces = []
        face_cache = {}

        def intern_vertex(point):
            key = tuple(_as_vec3(point))
            if key not in vertex_ids:
                vertex_ids[key] = len(vertices)
                vertices.append(list(key))
            return vertex_ids[key]

        for face_index, face in enumerate(brush.faces()):
            face_id = f"face{face_index}"
            face_vertices = list(face.vertices)
            local_to_brush = [intern_vertex(point) for point in face_vertices]
            loops = []
            for loop in face.uv_loops:
                local_vertex = int(loop["vertex"])
                loops.append(
                    {
                        "vertex": local_to_brush[local_vertex],
                        "uv": _as_uv(loop["uv"]),
                    }
                )

            faces.append(
                {
                    "id": face_id,
                    "vertices": local_to_brush,
                    "material": face.texture_name,
                    "loops": loops,
                }
            )
            face_cache[face_id] = {
                "handle": face,
                "vertices": local_to_brush,
                "points": [_as_vec3(point) for point in face_vertices],
                "vertex_to_local": {
                    brush_vertex: local_vertex
                    for local_vertex, brush_vertex in enumerate(local_to_brush)
                },
            }

        self.cache[brush_id] = {"faces": face_cache}
        return {"id": brush_id, "vertices": vertices, "faces": faces}

    def send_selection(self):
        try:
            brushes = self.selected_brushes()
            if not brushes:
                self.log("No selected brushes.")
                return

            self.session_id = uuid.uuid4().hex
            self.cache = {}
            payload = {
                "schema": SCHEMA,
                "sessionId": self.session_id,
                "createdAt": time.time(),
                "wadPaths": _wad_paths(tb.current_document()),
                "brushes": [
                    self.export_brush(brush, brush_index)
                    for brush_index, brush in enumerate(brushes)
                ],
            }
            _write_json(REQUEST_PATH, payload)
            if RESPONSE_PATH.exists():
                RESPONSE_PATH.unlink()
            face_count = sum(len(brush["faces"]) for brush in payload["brushes"])
            self.pending_response_mtime = None
            self.log(f"Sent {len(brushes)} brushes, {face_count} faces.")
        except Exception as e:
            self.log(f"Error: {e}")
            raise

    def read_response(self):
        if not RESPONSE_PATH.exists():
            return None
        with RESPONSE_PATH.open("r", encoding="utf-8-sig") as f:
            response = json.load(f)
        if response.get("schema") != SCHEMA:
            raise ValueError("Response schema mismatch.")
        if response.get("sessionId") != self.session_id:
            raise ValueError("Response belongs to a different sync session.")
        return response

    def poll_response(self):
        if not self.session_id or not RESPONSE_PATH.exists():
            return
        mtime = RESPONSE_PATH.stat().st_mtime
        if mtime != self.pending_response_mtime:
            self.pending_response_mtime = mtime
            self.log("Blender response pending. Click Apply Pending.")

    def apply_pending(self):
        try:
            if not self.session_id or not self.cache:
                self.log("Send selection before applying.")
                return
            response = self.read_response()
            if response is None:
                self.log("No pending Blender response.")
                return

            warnings = list(response.get("warnings", []))
            material_count = 0
            uv_count = 0
            skipped = 0
            doc = tb.current_document()

            with doc.transaction("Blender Brush Sync: Apply"):
                for face_payload in response.get("faces", []):
                    brush_id = face_payload.get("brushId")
                    face_id = face_payload.get("faceId")
                    face_cache = self.cache.get(brush_id, {}).get("faces", {}).get(face_id)
                    if face_cache is None:
                        skipped += 1
                        warnings.append(f"Unknown face: {brush_id}/{face_id}")
                        continue

                    loops = face_payload.get("loops", [])
                    expected_vertices = set(face_cache["vertices"])
                    loop_vertices = {int(loop.get("vertex", -1)) for loop in loops}
                    if len(loops) != len(face_cache["vertices"]) or loop_vertices != expected_vertices:
                        skipped += 1
                        warnings.append(f"Topology changed: {brush_id}/{face_id}")
                        continue

                    face = face_cache["handle"]
                    if [_as_vec3(point) for point in face.vertices] != face_cache["points"]:
                        skipped += 1
                        warnings.append(f"Brush changed in TrenchBroom: {brush_id}/{face_id}")
                        continue

                    material = face_payload.get("material")
                    if isinstance(material, str):
                        face.texture_name = material
                        material_count += 1

                    local_loops = []
                    for loop in loops:
                        brush_vertex = int(loop["vertex"])
                        local_loops.append(
                            {
                                "vertex": face_cache["vertex_to_local"][brush_vertex],
                                "uv": _as_uv(loop["uv"]),
                            }
                        )
                    if face.set_uv_loops(local_loops):
                        uv_count += 1
                    else:
                        skipped += 1
                        warnings.append(f"UVs are not affine/parallel: {brush_id}/{face_id}")

            RESPONSE_PATH.unlink(missing_ok=True)
            suffix = f", {len(warnings)} warnings" if warnings else ""
            self.log(
                f"Applied {material_count} materials, {uv_count} UV faces, "
                f"skipped {skipped}{suffix}."
            )
        except Exception as e:
            self.log(f"Error: {e}")
            raise


_sync = BlenderBrushSync()
