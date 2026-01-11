import tb


def main() -> None:
    doc = tb.Document.current()
    if doc is None:
        print("no current document")
        return

    sel = doc.selection
    verts_by_brush = sel.brush_vertices()

    if len(verts_by_brush) == 0:
        print("no selected brushes (or brush faces)")
        return

    print(f"selected brushes: {len(verts_by_brush)}")

    for brush_index, verts in enumerate(verts_by_brush):
        print(f"brush[{brush_index}] vertices: {len(verts)}")
        for v_index, (x, y, z) in enumerate(verts):
            print(f"  v[{v_index}]: ({x}, {y}, {z})")


if __name__ == "__main__":
    main()
