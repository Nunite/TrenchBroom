import tb

def create_panel():
    try:
        panel = tb.create_plugin_panel("Vec3 & Color Demo")
    except RuntimeError as e:
        print(f"Error creating panel: {e}")
        return

    panel.clear()

    panel.add_label("<b>Vector Operations</b>")
    
    # Vector A
    panel.add_label("Vector A:")
    panel.add_float_field("ax", "X", 1.0)
    panel.add_float_field("ay", "Y", 0.0)
    panel.add_float_field("az", "Z", 0.0)
    
    # Vector B
    panel.add_label("Vector B:")
    panel.add_float_field("bx", "X", 0.0)
    panel.add_float_field("by", "Y", 1.0)
    panel.add_float_field("bz", "Z", 0.0)
    
    def on_calc_dot():
        v1 = tb.Vec3(panel.get_float_field("ax"), panel.get_float_field("ay"), panel.get_float_field("az"))
        v2 = tb.Vec3(panel.get_float_field("bx"), panel.get_float_field("by"), panel.get_float_field("bz"))
        result = v1.dot(v2)
        panel.set_label_text("result", f"Dot Product: {result}")
        
    def on_calc_cross():
        v1 = tb.Vec3(panel.get_float_field("ax"), panel.get_float_field("ay"), panel.get_float_field("az"))
        v2 = tb.Vec3(panel.get_float_field("bx"), panel.get_float_field("by"), panel.get_float_field("bz"))
        result = v1.cross(v2)
        panel.set_label_text("result", f"Cross Product: {result}")
        
    def on_calc_len_a():
        v1 = tb.Vec3(panel.get_float_field("ax"), panel.get_float_field("ay"), panel.get_float_field("az"))
        result = v1.length()
        panel.set_label_text("result", f"Length A: {result}")

    def on_calc_norm_a():
        v1 = tb.Vec3(panel.get_float_field("ax"), panel.get_float_field("ay"), panel.get_float_field("az"))
        if v1.length() == 0:
             panel.set_label_text("result", "Result: Zero vector")
             return
        result = v1.normalize()
        panel.set_label_text("result", f"Normalized A: {result}")

    panel.add_button_callback("Calculate Dot (A . B)", on_calc_dot)
    panel.add_button_callback("Calculate Cross (A x B)", on_calc_cross)
    panel.add_button_callback("Calculate Length (A)", on_calc_len_a)
    panel.add_button_callback("Calculate Normalize (A)", on_calc_norm_a)
    
    panel.add_label_named("result", "Result: ")
    
    panel.add_label("<b>Color Picker</b>")
    # Initial color red
    panel.add_color_field("color", "Select Color", (255, 0, 0))
    
    def on_apply_color():
        c = panel.get_color_field("color")
        # Format as string "r g b" (0-1 for TrenchBroom standard usually, but _color is often 0-1 or 0-255 depending on engine)
        # Quake engines usually use "r g b" 0-255?
        # Standard Quake _color is "R G B" (0-255) or "R G B S"
        # Let's assume 0-255.
        
        # But wait, QColorDialog returns 0-255.
        # If we want 0-1, we divide.
        # Let's write "r g b" (0-1) which is safer for modern engines, or just "r g b" (0-255).
        # TB documentation says _color is vector.
        
        color_str = f"{c[0]/255.0} {c[1]/255.0} {c[2]/255.0}"
        
        try:
            doc = tb.document()
            sel = doc.selection
            if not sel.entities:
                panel.set_label_text("status", "Status: No entities selected")
                return
                
            with doc.transaction("Apply Color"):
                # Apply to all selected entities
                sel.set_property("_color", color_str)
            
            panel.set_label_text("status", f"Status: Applied color {color_str}")
        except Exception as e:
            panel.set_label_text("status", f"Error: {e}")

    panel.add_button_callback("Apply Color to Selection (_color)", on_apply_color)
    panel.add_label_named("status", "Status: Ready")

if __name__ == "__main__":
    create_panel()
