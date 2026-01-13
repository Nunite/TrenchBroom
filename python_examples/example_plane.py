import tb

def main() -> None:
    doc = tb.Document.current()
    panel = tb.create_plugin_panel("Plane Tools")
    if doc is None:
        panel.set_text("No active document")
        panel.add_button("Run Python Script...", "Menu/Run/Run Python Script...")
        return

    panel.set_text(
        "Plane Tools\n"
        "- Duplicate selection along X to form a row\n"
        "- Rotate around first Vertex Tool handle if present"
    )
    panel.add_button("Preferences", "Menu/File/Preferences...")
    panel.add_button("Run Python Script...", "Menu/Run/Run Python Script...")
    panel.add_button("Compile Map...", "Menu/Run/Compile...")

    sel = doc.selection
    if len(sel.all_entities()) > 0:
        with tb.transaction("Python: build plane"):
            for _ in range(9):
                sel.duplicate()
                sel.translate(128, 0, 0)

    verts = doc.vertex_tool_vertices()
    if len(verts) > 0:
        pivot_x, pivot_y, pivot_z = verts[0]
        with tb.transaction("Python: rotate around vertex"):
            sel.rotate(0, 0, 1, 15, pivot_x, pivot_y, pivot_z)

if __name__ == "__main__":
    main()
