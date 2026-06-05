"""
Event System Demo
=================

This script demonstrates how to use the event system in TrenchBroom's Python API.
It registers a callback that prints the number of selected entities whenever the selection changes.
"""

import tb

def on_selection_changed():
    """Callback function for selection change event."""
    try:
        doc = tb.Document.current()
        if not doc:
            return
            
        selection = doc.selection
        # Get selected entities count
        entities_count = len(selection.entities)
        # Get selected brushes count
        brushes_count = len(selection.brushes)
        
        print(f"Selection changed: {entities_count} entities, {brushes_count} brushes selected.")
        
        # Example: Print classname of first selected entity
        if entities_count > 0:
            first_ent = selection.entities[0]
            print(f"  First entity classname: {first_ent.classname}")

    except Exception as e:
        print(f"Error in callback: {e}")

# Register the callback
print("Registering selection_changed callback...")
tb.register_callback("selection_changed", on_selection_changed)
print("Callback registered. Try selecting objects in the map view.")

# Note: The callback will persist until TrenchBroom is closed or unregister_callback is called.
# To unregister:
# tb.unregister_callback("selection_changed", on_selection_changed)
