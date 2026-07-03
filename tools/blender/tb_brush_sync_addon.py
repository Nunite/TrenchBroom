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
import struct
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
_texture_cache = {}


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
    material["tb_material_name"] = name
    return material


def _read_wad_texture(wad_path, texture_name):
    key = (str(wad_path).lower(), texture_name.lower())
    if key in _texture_cache:
        return _texture_cache[key]

    with open(wad_path, "rb") as f:
        if f.read(4) != b"WAD3":
            return None
        lump_count = struct.unpack("<I", f.read(4))[0]
        lump_offset = struct.unpack("<I", f.read(4))[0]
        f.seek(lump_offset)
        lumps = []
        for _ in range(lump_count):
            offset, compressed_length, full_length = struct.unpack("<III", f.read(12))
            lump_type = struct.unpack("<B", f.read(1))[0]
            compression = struct.unpack("<B", f.read(1))[0]
            f.seek(2, 1)
            name = f.read(16).split(b"\x00")[0].decode("ascii", errors="ignore")
            lumps.append((name, offset, lump_type, compression, compressed_length, full_length))

        target = texture_name.split("/")[-1].lower()
        for name, offset, lump_type, compression, _compressed_length, _full_length in lumps:
            if name.lower() != target or lump_type not in {0x40, 0x42, 0x43, 0x46} or compression:
                continue

            f.seek(offset)
            if lump_type in {0x40, 0x43}:
                f.seek(16, 1)
            width = struct.unpack("<I", f.read(4))[0]
            height = struct.unpack("<I", f.read(4))[0]
            if width <= 0 or height <= 0 or width > 4096 or height > 4096:
                return None
            if lump_type in {0x40, 0x43}:
                f.seek(16, 1)
            pixels = f.read(width * height)
            if lump_type in {0x40, 0x43}:
                f.seek((width // 2) * (height // 2), 1)
                f.seek((width // 4) * (height // 4), 1)
                f.seek((width // 8) * (height // 8), 1)
            f.seek(2, 1)
            palette_bytes = f.read(256 * 3)
            palette = [
                (
                    palette_bytes[i] / 255.0,
                    palette_bytes[i + 1] / 255.0,
                    palette_bytes[i + 2] / 255.0,
                    0.0 if name.startswith("{") and index == 255 else 1.0,
                )
                for index, i in enumerate(range(0, min(len(palette_bytes), 768), 3))
                if i + 2 < len(palette_bytes)
            ]
            texture = {"name": name, "width": width, "height": height, "pixels": pixels, "palette": palette}
            _texture_cache[key] = texture
            return texture
    return None


def _image_from_wad_texture(texture):
    image = bpy.data.images.get(texture["name"])
    if image is not None:
        return image

    image = bpy.data.images.new(texture["name"], texture["width"], texture["height"])
    rgba = []
    for pixel in texture["pixels"]:
        rgba.extend(texture["palette"][pixel] if pixel < len(texture["palette"]) else (0.0, 0.0, 0.0, 1.0))
    image.pixels = rgba
    image.pack()
    return image


def _material_from_wad(name, wad_paths):
    for wad_path in wad_paths:
        texture = _read_wad_texture(wad_path, name)
        if texture is None:
            continue

        image = _image_from_wad_texture(texture)
        material = bpy.data.materials.get(name)
        if material is None:
            material = bpy.data.materials.new(name)
        material["tb_material_name"] = name
        material.use_nodes = True
        nodes = material.node_tree.nodes
        nodes.clear()
        output = nodes.new("ShaderNodeOutputMaterial")
        bsdf = nodes.new("ShaderNodeBsdfPrincipled")
        tex = nodes.new("ShaderNodeTexImage")
        tex.image = image
        material.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        material.node_tree.links.new(bsdf.outputs["BSDF"], output.inputs["Surface"])
        if texture["name"].startswith("{"):
            material.node_tree.links.new(tex.outputs["Alpha"], bsdf.inputs["Alpha"])
            material.blend_method = "BLEND"
        return material
    return _material(name)


def _material_texture_size(material):
    if material is None or material.node_tree is None:
        return None
    for node in material.node_tree.nodes:
        if node.bl_idname != "ShaderNodeTexImage" or node.image is None:
            continue
        width, height = node.image.size[:]
        if width > 0 and height > 0:
            return float(width), float(height)
    return None


def _to_blender_uv(uv, texture_size):
    u, v = float(uv[0]), float(uv[1])
    if texture_size is None:
        return u, v
    width, height = texture_size
    return u / width, v / height


def _to_tb_uv(uv, texture_size):
    u, v = float(uv.x), float(uv.y)
    if texture_size is not None:
        width, height = texture_size
        u *= width
        v *= height
    return [round(u, 6), round(v, 6)]


def _export_material_name(material):
    name = material.name
    original = material.get("tb_material_name")
    if not isinstance(original, str):
        return name
    if not original and name == "__TB_EMPTY__":
        return original
    suffix = name.removeprefix(original + ".")
    if name == original or (suffix != name and suffix.isdigit()):
        return original
    return name


def _object_name(brush_id):
    return f"TB Brush {brush_id}"


def import_request(payload):
    if payload.get("schema") != SCHEMA:
        raise ValueError("Unsupported TB brush sync schema")

    collection = _sync_collection()
    session_id = payload.get("sessionId", "")
    wad_paths = [str(path) for path in payload.get("wadPaths", []) if Path(str(path)).exists()]

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
                obj.data.materials.append(_material_from_wad(material_name, wad_paths))

        material_index = {name: index for index, name in enumerate(material_names)}
        uv_layer = mesh.uv_layers.new(name=UV_LAYER_NAME)
        for face_payload, polygon in zip(faces, mesh.polygons):
            material_name = str(face_payload.get("material") or "")
            polygon.material_index = material_index.get(material_name, 0)
            material = obj.data.materials[polygon.material_index]
            texture_size = _material_texture_size(material)
            uv_by_vertex = {
                int(loop["vertex"]): loop.get("uv", [0.0, 0.0])
                for loop in face_payload.get("loops", [])
            }
            for loop_index in polygon.loop_indices:
                vertex_index = int(mesh.loops[loop_index].vertex_index)
                uv = uv_by_vertex.get(vertex_index, [0.0, 0.0])
                uv_layer.data[loop_index].uv = _to_blender_uv(uv, texture_size)

    bpy.context.scene["tb_sync_session_id"] = session_id
    return len(payload.get("brushes", []))


def import_request_file(path=REQUEST_PATH):
    return import_request(_read_json(path))


def _loop_uv(uv_layer, loop_index, texture_size):
    uv = uv_layer.data[loop_index].uv
    return _to_tb_uv(uv, texture_size)


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
            material_slot = None
            if 0 <= polygon.material_index < len(obj.material_slots):
                material_slot = obj.material_slots[polygon.material_index].material
                if material_slot:
                    material = _export_material_name(material_slot)
            texture_size = _material_texture_size(material_slot)

            loops = []
            for loop_index in polygon.loop_indices:
                loops.append(
                    {
                        "vertex": int(mesh.loops[loop_index].vertex_index),
                        "uv": _loop_uv(uv_layer, loop_index, texture_size),
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
