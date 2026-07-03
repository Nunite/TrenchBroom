import json
import sys

import bpy


def fail(output_path, message):
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump({"ok": False, "error": message}, f)
    raise SystemExit(1)


def parse_args():
    try:
        marker = sys.argv.index("--")
    except ValueError:
        raise SystemExit(
            "usage: blender --python tb_uv_bridge.py -- export input.json scene.blend "
            "| read scene.blend output.json"
        )

    args = sys.argv[marker + 1 :]
    if len(args) != 3 or args[0] not in {"export", "read"}:
        raise SystemExit(
            "usage: blender --python tb_uv_bridge.py -- export input.json scene.blend "
            "| read scene.blend output.json"
        )
    return args


def export_scene(input_path, blend_path):
    with open(input_path, "r", encoding="utf-8-sig") as f:
        payload = json.load(f)

    vertices = payload["vertices"]
    triangles = payload["triangles"]

    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()

    mesh = bpy.data.meshes.new("tb_uv_bridge")
    mesh.from_pydata(vertices, [], [tri["vertices"] for tri in triangles])
    mesh.update()

    obj = bpy.data.objects.new("tb_uv_bridge", mesh)
    obj["tb_uv_bridge"] = True
    obj["tb_ids"] = [tri["id"] for tri in triangles]
    bpy.context.collection.objects.link(obj)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)

    uv_layer = mesh.uv_layers.new(name="TB_UV")
    for tri, poly in zip(triangles, mesh.polygons):
        uv_by_vertex = {int(loop["vertex"]): loop["uv"] for loop in tri["loops"]}
        for loop_index in poly.loop_indices:
            vertex_index = mesh.loops[loop_index].vertex_index
            uv_layer.data[loop_index].uv = uv_by_vertex.get(vertex_index, (0.0, 0.0))

    bpy.ops.wm.save_as_mainfile(filepath=blend_path)


def read_scene(blend_path, output_path):
    bpy.ops.wm.open_mainfile(filepath=blend_path)
    obj = next((o for o in bpy.data.objects if o.get("tb_uv_bridge")), None)
    if obj is None or obj.type != "MESH":
        fail(output_path, "missing tb_uv_bridge mesh")

    mesh = obj.data
    tb_ids = list(obj.get("tb_ids", []))
    uv_layer = mesh.uv_layers.get("TB_UV") or mesh.uv_layers.active
    if uv_layer is None:
        fail(output_path, "missing UV layer")

    def loop_uv(loop_index):
        uv = uv_layer.data[loop_index].uv
        return [round(float(uv.x), 6), round(float(uv.y), 6)]

    output_triangles = []
    for index, poly in enumerate(mesh.polygons):
        loops = []
        for loop_index in poly.loop_indices:
            loops.append(
                {
                    "vertex": int(mesh.loops[loop_index].vertex_index),
                    "uv": loop_uv(loop_index),
                }
            )
        output_triangles.append(
            {"id": tb_ids[index] if index < len(tb_ids) else f"tri{index}", "loops": loops}
        )

    with open(output_path, "w", encoding="utf-8") as f:
        json.dump({"ok": True, "triangles": output_triangles}, f, indent=2)


def main():
    mode, src, dst = parse_args()
    if mode == "export":
        export_scene(src, dst)
    else:
        read_scene(src, dst)


if __name__ == "__main__":
    main()
