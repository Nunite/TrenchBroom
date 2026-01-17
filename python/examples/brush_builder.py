import tb
from tb import Vec3

def main() -> None:
    doc = tb.Document.current()
    if doc is None:
        print("No active document")
        return

    # Create a simple cube brush centered at origin
    size = 64.0
    half = size / 2.0
    
    # Define points for a cube
    points = [
        Vec3(-half, -half, -half),
        Vec3( half, -half, -half),
        Vec3( half,  half, -half),
        Vec3(-half,  half, -half),
        Vec3(-half, -half,  half),
        Vec3( half, -half,  half),
        Vec3( half,  half,  half),
        Vec3(-half,  half,  half)
    ]
    
    with tb.transaction("Create Python Brush"):
        brush = tb.create_brush(points)
        if brush:
            print(f"Created brush with bounds: {brush.bounds}")
            
            # Modify faces
            for i, face in enumerate(brush.faces()):
                print(f"Face {i}: texture={face.texture_name}, normal={face.normal}")
                # Set texture for demonstration (ensure this texture exists or it will be default)
                face.texture_name = "common/caulk" 
                face.scale = (0.5, 0.5)
        else:
            print("Failed to create brush")

if __name__ == "__main__":
    main()
