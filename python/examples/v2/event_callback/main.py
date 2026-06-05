import tb2 as tb


def on_selection_changed():
    doc = tb.current_document()
    print(f"selection changed: {len(doc.selection.brushes)} selected brushes")


tb.register_callback("selection_changed", on_selection_changed)
print("V2 selection callback registered")
