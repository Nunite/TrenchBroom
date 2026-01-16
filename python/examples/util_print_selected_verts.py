import tb

def main() -> None:
    doc = tb.Document.current()
    if doc is None:
        print("no current document")
        return

    verts = doc.vertex_tool_vertices()
    print("selected vertex handles:", len(verts))

    for i, (x, y, z) in enumerate(verts):
        print(i, x, y, z)


if __name__ == "__main__":
    main()
