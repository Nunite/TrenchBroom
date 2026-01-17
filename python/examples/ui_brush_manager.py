import tb
from tb import Vec3

_PANEL_TITLE = "Brush Manager"
_TEXTURE_PRESETS = ["common/caulk", "common/nodraw", "common/weapclip", "common/trigger"]

class BrushManagerAddon:
    def __init__(self) -> None:
        self._panel = tb.create_plugin_panel(_PANEL_TITLE)
        self._build_ui()

    def _build_ui(self) -> None:
        self._panel.clear()
        
        self._panel.add_label("<b>Generator</b>")
        self._panel.add_int_field("cube_size", "Size", 64, 16, 512)
        self._panel.add_float_field("offset_x", "X", 0, -4096, 4096, 0, 16)
        self._panel.add_float_field("offset_y", "Y", 0, -4096, 4096, 0, 16)
        self._panel.add_float_field("offset_z", "Z", 0, -4096, 4096, 0, 16)
        self._panel.add_button_callback("Create Cube", self._on_create_cube)
        
        self._panel.add_label("<b>Modifier</b>")
        self._panel.add_combo_box("texture", "Texture", _TEXTURE_PRESETS)
        self._panel.add_float_field("tex_scale", "Scale", 1.0, 0.1, 10.0, 2, 0.1)
        self._panel.add_button_callback("Apply to Selection", self._on_apply_texture)
        
        self._panel.add_label("<b>Info</b>")
        self._panel.add_label_named("status", "Ready")
        self._panel.add_button_callback("Analyze Selection", self._on_analyze)

    def _log(self, msg: str) -> None:
        print(f"[{_PANEL_TITLE}] {msg}")
        self._panel.set_label_text("status", msg)

    def _on_create_cube(self) -> None:
        try:
            size = self._panel.get_int_field("cube_size")
            ox = self._panel.get_float_field("offset_x")
            oy = self._panel.get_float_field("offset_y")
            oz = self._panel.get_float_field("offset_z")
            center = Vec3(ox, oy, oz)
            
            half = size / 2.0
            points = [
                center + Vec3(-half, -half, -half), center + Vec3( half, -half, -half),
                center + Vec3( half,  half, -half), center + Vec3(-half,  half, -half),
                center + Vec3(-half, -half,  half), center + Vec3( half, -half,  half),
                center + Vec3( half,  half,  half), center + Vec3(-half,  half,  half)
            ]
            
            with tb.transaction("Create Cube"):
                brush = tb.create_brush(points)
                if brush:
                    self._log(f"Created cube at {center}")
                else:
                    self._log("Failed to create brush")
        except Exception as e:
            self._log(f"Error: {e}")

    def _on_apply_texture(self) -> None:
        doc = tb.Document.current()
        if not doc:
            self._log("No active document")
            return
            
        try:
            texture = self._panel.get_combo_box_text("texture")
            scale_val = self._panel.get_float_field("tex_scale")
            
            count = 0
            with tb.transaction("Apply Texture"):
                # Collect all brushes: direct selection + inside selected entities
                all_brushes = list(doc.selection.brushes)
                for entity in doc.selection.entities:
                    all_brushes.extend(entity.brushes)
                
                # Use a set to avoid duplicates (using object ID/hash if supported, or manual)
                # Currently Brush equality might not be fully supported, but modifying same brush twice is safe but redundant.
                # However, Python wrappers are new objects each time.
                # Let's just iterate.
                
                for brush in all_brushes:
                    for face in brush.faces():
                        face.texture_name = texture
                        face.scale = (scale_val, scale_val)
                        count += 1
            
            self._log(f"Updated {count} faces")
        except Exception as e:
            self._log(f"Error: {e}")

    def _on_analyze(self) -> None:
        doc = tb.Document.current()
        if not doc:
            return
            
        sel = doc.selection
        
        # Explicit brushes
        brushes = sel.brushes
        brush_count = len(brushes)
        
        # Entity brushes
        for entity in sel.entities:
            brushes.extend(entity.brushes)
        
        total_faces = 0
        for b in brushes:
            total_faces += len(b.faces())
                
        self._log(f"Selected: {len(sel.entities)} entities, {brush_count} explicit brushes, {total_faces} faces")

if __name__ == "__main__":
    # Keep instance alive
    _ADDON = BrushManagerAddon()
