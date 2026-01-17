"""
Texture Browser Demo
====================

This script demonstrates how to access and display available textures (materials) in the current project.
It creates a plugin panel that lists all material collections and their materials.
"""

import tb

def create_texture_browser():
    try:
        doc = tb.Document.current()
        if not doc:
            print("No active document.")
            return

        panel = tb.create_plugin_panel("Texture Browser")
        panel.clear()
        
        panel.add_label("<b>Material Collections:</b>")
        
        collections = doc.material_collections
        if not collections:
            panel.add_label("No material collections found.")
            return

        for collection in collections:
            panel.add_label(f"<b>Collection: {collection.name}</b>")
            materials = collection.materials
            panel.add_label(f"  Count: {len(materials)}")
            
            # Show first 10 materials as example
            for i, mat in enumerate(materials[:10]):
                panel.add_label(f"  - {mat.name} ({mat.width}x{mat.height})")
            
            if len(materials) > 10:
                panel.add_label(f"  ... and {len(materials) - 10} more.")
            
            panel.add_label("") # Separator

    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    create_texture_browser()
