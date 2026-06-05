import tb2 as tb


def on_selection_changed():
    try:
        doc = tb.current_document()
        selection = doc.selection
        entities = selection.all_entities
        brushes = selection.brushes
        print(
            f"Selection changed: {len(entities)} entities, "
            f"{len(brushes)} explicit brushes selected."
        )
        if entities:
            print(f"  First entity classname: {entities[0].classname}")
    except Exception as e:
        print(f"Error in selection_changed callback: {e}")


tb.register_callback("selection_changed", on_selection_changed)
print("V2 selection_changed callback registered")
