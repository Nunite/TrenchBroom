import tb

def main() -> None:
    doc = tb.Document.current()
    if doc is None:
        print("no current document")
        return

    verts = doc.vertex_tool_vertices()
    if len(verts) == 0:
        print("no vertex tool selection")
        return

    pivot_x, pivot_y, pivot_z = verts[0]
    print(f"pivot: ({pivot_x}, {pivot_y}, {pivot_z})")

    sel_attr = doc.selection
    sel = sel_attr() if callable(sel_attr) else sel_attr
    if len(sel.all_entities()) == 0:
        print("selection is empty")
        return

    with tb.transaction("Python: duplicate 10x around vertex"):
        for _ in range(10):
            sel.duplicate()
            sel.translate(128, 0, 0)
            sel.rotate(0, 0, 1, 15, pivot_x, pivot_y, pivot_z)

    print("done")


if __name__ == "__main__":
    main()
