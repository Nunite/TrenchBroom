import json
import sys

import bmesh
import bpy


def fail(output_path, message):
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump({"ok": False, "error": message}, f)
    raise SystemExit(1)


def parse_args():
    try:
        marker = sys.argv.index("--")
    except ValueError:
        raise SystemExit("usage: blender --background --python tb_uv_bridge.py -- input.json output.json")

    args = sys.argv[marker + 1 :]
    if len(args) != 2:
        raise SystemExit("usage: blender --background --python tb_uv_bridge.py -- input.json output.json")
    return args[0], args[1]


def main():
    input_path, output_path = parse_args()
    with open(input_path, "r", encoding="utf-8-sig") as f:
        payload = json.load(f)

    vertices = payload["vertices"]
    triangles = payload["triangles"]
    active_quad = payload["activeQuad"]

    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()

    mesh = bpy.data.meshes.new("tb_uv_bridge")
    mesh.from_pydata(vertices, [], [tri["vertices"] for tri in triangles])
    mesh.update()

    obj = bpy.data.objects.new("tb_uv_bridge", mesh)
    bpy.context.collection.objects.link(obj)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)

    uv_layer = mesh.uv_layers.new(name="TB_UV")
    seed_uvs = {
        int(vertex_index): uv
        for vertex_index, uv in zip(active_quad["corners"], active_quad["uvs"])
    }
    for poly in mesh.polygons:
        for loop_index in poly.loop_indices:
            vertex_index = mesh.loops[loop_index].vertex_index
            uv_layer.data[loop_index].uv = seed_uvs.get(vertex_index, (0.0, 0.0))

    bpy.ops.object.mode_set(mode="EDIT")
    bm = bmesh.from_edit_mesh(mesh)
    for face in bm.faces:
        face.select = True
    bm.faces.ensure_lookup_table()
    if bm.faces:
        bm.faces.active = bm.faces[0]
    bmesh.update_edit_mesh(mesh)

    bpy.ops.mesh.tris_convert_to_quads(face_threshold=3.14159, shape_threshold=3.14159)
    bm = bmesh.from_edit_mesh(mesh)
    for face in bm.faces:
        face.select = True
    bm.faces.ensure_lookup_table()
    if bm.faces:
        bm.faces.active = bm.faces[0]
    bmesh.update_edit_mesh(mesh)

    if not bm.faces:
        fail(output_path, "no faces to unwrap")

    bpy.ops.uv.follow_active_quads(mode="LENGTH_AVERAGE")
    bpy.ops.mesh.quads_convert_to_tris(quad_method="FIXED", ngon_method="BEAUTY")
    bpy.ops.object.mode_set(mode="OBJECT")
    mesh.update()
    uv_layer = mesh.uv_layers["TB_UV"]

    def loop_uv(loop_index):
        uv = uv_layer.data[loop_index].uv
        return tuple(round(float(v), 6) for v in uv)

    output_triangles = []
    for index, poly in enumerate(mesh.polygons):
        loops = []
        for loop_index in poly.loop_indices:
            vertex_index = mesh.loops[loop_index].vertex_index
            loops.append(
                {
                    "vertex": int(vertex_index),
                    "uv": list(loop_uv(loop_index)),
                }
            )
        output_triangles.append({"id": f"tri{index}", "loops": loops})

    edge_uvs = {}
    mismatches = []
    for poly in mesh.polygons:
        loop_indices = list(poly.loop_indices)
        for i, loop_index in enumerate(loop_indices):
            next_loop_index = loop_indices[(i + 1) % len(loop_indices)]
            a = mesh.loops[loop_index].vertex_index
            b = mesh.loops[next_loop_index].vertex_index
            key = tuple(sorted((a, b)))
            uv_pair = [loop_uv(loop_index), loop_uv(next_loop_index)]
            if key in edge_uvs:
                if set(edge_uvs[key]) != set(uv_pair):
                    mismatches.append({"edge": key, "a": edge_uvs[key], "b": uv_pair})
            else:
                edge_uvs[key] = uv_pair

    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(
            {
                "ok": True,
                "triangles": output_triangles,
                "mismatchCount": len(mismatches),
                "mismatches": mismatches[:12],
            },
            f,
            indent=2,
        )


if __name__ == "__main__":
    main()
