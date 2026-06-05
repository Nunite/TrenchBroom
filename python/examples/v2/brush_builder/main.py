import tb2 as tb


half = 64
brush = tb.create_brush(
    [
        tb.Vec3(-half, -half, -half),
        tb.Vec3(half, -half, -half),
        tb.Vec3(half, half, -half),
        tb.Vec3(-half, half, -half),
        tb.Vec3(-half, -half, half),
        tb.Vec3(half, -half, half),
        tb.Vec3(half, half, half),
        tb.Vec3(-half, half, half),
    ],
    "common/caulk",
)

faces = brush.faces()
if faces:
    faces[0].texture_name = "common/caulk"
    faces[0].scale = (0.5, 0.5)

print(f"Created brush with {len(faces)} faces")
