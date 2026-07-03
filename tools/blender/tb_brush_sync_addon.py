bl_info = {
    "name": "TB Brush Sync",
    "author": "TrenchBroom",
    "version": (1, 0, 0),
    "blender": (4, 0, 0),
    "location": "View3D > Sidebar > TB Sync",
    "description": "Synchronize selected TrenchBroom brushes for UV/material editing.",
    "category": "Import-Export",
}

import json
import os
import tempfile
from pathlib import Path

import bpy


SCHEMA = "tb.blenderBrushSync.v1"
COLLECTION_NAME = "TB Sync"
UV_LAYER_NAME = "TB_UV"
SYNC_DIR = Path(os.environ.get("TEMP") or tempfile.gettempdir()) / "trenchbroom-blender-sync"
REQUEST_PATH = SYNC_DIR / "request.json"
RESPONSE_PATH = SYNC_DIR / "response.json"

_last_request_mtime = None


def _read_json(path):
    with open(path, "r", encoding="utf-8-sig") as f:
        return json.load(f)


def _write_json(path, payload):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = path.with_suffix(path.suffix + ".tmp")
    with open(temp_path, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2)
    os.replace(temp_path, path)


def _sync_collection():
    collection = bpy.data.collections.get(COLLECTION_NAME)
    if collection is None:
        collection = bpy.data.collections.new(COLLECTION_NAME)
        bpy.context.scene.collection.children.link(collection)
    return collection


def _material(name):
    material_name = name or "__TB_EMPTY__"
    material = bpy.data.materials.get(material_name)
    if material is None:
        material = bpy.data.materials.new(material_name)
    return material


def _object_name(brush_id):
    return f"TB Brush {brush_id}"


def import_request(payload):
    if payload.get("schema") != SCHEMA:
        raise ValueError("Unsupported TB brush sync schema")

    collection = _sync_collection()
    session_id = payload.get("sessionId", "")

    for brush in payload.get("brushes", []):
        brush_id = str(brush["id"])
        vertices = [tuple(vertex) for vertex in brush.get("vertices", [])]
        faces = brush.get("faces", [])
        polygons = [face["vertices"] for face in faces]

        mesh = bpy.data.meshes.new(f"TB Brush Mesh {brush_id}")
        mesh.from_pydata(vertices, [], polygons)
        mesh.update()

        obj_name = _object_name(brush_id)
        old_obj = bpy.data.objects.get(obj_name)
        if old_obj is None:
            obj = bpy.data.objects.new(obj_name, mesh)
            collection.objects.link(obj)
        else:
            old_mesh = old_obj.data
            old_obj.data = mesh
            obj = old_obj
            if old_mesh.users == 0:
                bpy.data.meshes.remove(old_mesh)
            if obj.name not in collection.objects.keys():
                collection.objects.link(obj)

        obj["tb_sync_schema"] = SCHEMA
        obj["tb_session_id"] = session_id
        obj["tb_brush_id"] = brush_id
        obj["tb_face_ids"] = [str(face["id"]) for face in faces]
        obj["tb_face_vertices"] = [list(map(int, face["vertices"])) for face in faces]

        material_names = []
        for face in faces:
            material_name = str(face.get("material") or "")
            if material_name not in material_names:
                material_names.append(material_name)
                obj.data.materials.append(_material(material_name))

        material_index = {name: index for index, name in enumerate(material_names)}
        uv_layer = mesh.uv_layers.new(name=UV_LAYER_NAME)
        for face_payload, polygon in zip(faces, mesh.polygons):
            polygon.material_index = material_index.get(str(face_payload.get("material") or ""), 0)
            uv_by_vertex = {
                int(loop["vertex"]): loop.get("uv", [0.0, 0.0])
                for loop in face_payload.get("loops", [])
            }
            for loop_index in polygon.loop_indices:
                vertex_index = int(mesh.loops[loop_index].vertex_index)
                uv_layer.data[loop_index].uv = uv_by_vertex.get(vertex_index, [0.0, 0.0])

    bpy.context.scene["tb_sync_session_id"] = session_id
    return len(payload.get("brushes", []))


def import_request_file(path=REQUEST_PATH):
    return import_request(_read_json(path))


def _loop_uv(uv_layer, loop_index):
    uv = uv_layer.data[loop_index].uv
    return [round(float(uv.x), 6), round(float(uv.y), 6)]


def export_response(path=RESPONSE_PATH):
    collection = bpy.data.collections.get(COLLECTION_NAME)
    session_id = bpy.context.scene.get("tb_sync_session_id", "")
    response = {"schema": SCHEMA, "sessionId": session_id, "faces": [], "warnings": []}
    if collection is None:
        response["warnings"].append("Missing TB Sync collection")
        _write_json(path, response)
        return response

    for obj in collection.objects:
        if obj.type != "MESH" or obj.get("tb_sync_schema") != SCHEMA:
            continue

        mesh = obj.data
        brush_id = str(obj.get("tb_brush_id", ""))
        face_ids = list(obj.get("tb_face_ids", []))
        expected_vertices = list(obj.get("tb_face_vertices", []))
        uv_layer = mesh.uv_layers.get(UV_LAYER_NAME) or mesh.uv_layers.active
        if uv_layer is None:
            response["warnings"].append(f"{brush_id}: missing UV layer")
            continue
        if len(mesh.polygons) != len(face_ids):
            response["warnings"].append(f"{brush_id}: polygon count changed")

        for index, polygon in enumerate(mesh.polygons):
            if index >= len(face_ids) or index >= len(expected_vertices):
                response["warnings"].append(f"{brush_id}: extra polygon {index}")
                continue

            loop_vertices = [
                int(mesh.loops[loop_index].vertex_index) for loop_index in polygon.loop_indices
            ]
            if loop_vertices != list(map(int, expected_vertices[index])):
                response["warnings"].append(f"{brush_id}/{face_ids[index]}: topology changed")
                continue

            material = ""
            if 0 <= polygon.material_index < len(obj.material_slots):
                slot = obj.material_slots[polygon.material_index]
                if slot.material:
                    material = slot.material.name

            loops = []
            for loop_index in polygon.loop_indices:
                loops.append(
                    {
                        "vertex": int(mesh.loops[loop_index].vertex_index),
                        "uv": _loop_uv(uv_layer, loop_index),
                    }
                )

            response["faces"].append(
                {
                    "brushId": brush_id,
                    "faceId": str(face_ids[index]),
                    "material": material,
                    "loops": loops,
                }
            )

    _write_json(path, response)
    return response


def poll_request():
    global _last_request_mtime
    if not REQUEST_PATH.exists():
        return 1.0

    mtime = REQUEST_PATH.stat().st_mtime
    if mtime != _last_request_mtime:
        _last_request_mtime = mtime
        try:
            count = import_request_file(REQUEST_PATH)
            print(f"TB Brush Sync imported {count} brushes")
        except Exception as exc:
            print(f"TB Brush Sync import failed: {exc}")
    return 1.0


class TB_BRUSH_SYNC_OT_import(bpy.types.Operator):
    bl_idname = "tb_brush_sync.import_request"
    bl_label = "Import From TrenchBroom"

    def execute(self, context):
        try:
            count = import_request_file()
            self.report({"INFO"}, f"Imported {count} TB brushes")
            return {"FINISHED"}
        except Exception as exc:
            self.report({"ERROR"}, str(exc))
            return {"CANCELLED"}


class TB_BRUSH_SYNC_OT_export(bpy.types.Operator):
    bl_idname = "tb_brush_sync.export_response"
    bl_label = "Sync Back to TrenchBroom"

    def execute(self, context):
        try:
            response = export_response()
            self.report({"INFO"}, f"Exported {len(response['faces'])} faces")
            return {"FINISHED"}
        except Exception as exc:
            self.report({"ERROR"}, str(exc))
            return {"CANCELLED"}


class TB_BRUSH_SYNC_PT_panel(bpy.types.Panel):
    bl_label = "TB Brush Sync"
    bl_idname = "TB_BRUSH_SYNC_PT_panel"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "TB Sync"

    def draw(self, context):
        layout = self.layout
        layout.operator(TB_BRUSH_SYNC_OT_import.bl_idname)
        layout.operator(TB_BRUSH_SYNC_OT_export.bl_idname)
        layout.label(text=f"Folder: {SYNC_DIR}")


classes = (
    TB_BRUSH_SYNC_OT_import,
    TB_BRUSH_SYNC_OT_export,
    TB_BRUSH_SYNC_PT_panel,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    if not bpy.app.timers.is_registered(poll_request):
        bpy.app.timers.register(poll_request, first_interval=1.0, persistent=True)


def unregister():
    if bpy.app.timers.is_registered(poll_request):
        bpy.app.timers.unregister(poll_request)
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)


if __name__ == "__main__":
    register()
