import tb

def main() -> None:
    doc = tb.Document.current()
    if doc is None:
        print("No active document")
        return
    
    sel = doc.selection
    count = 0
    
    with tb.transaction("Modify Entity Brushes"):
        for entity in sel.entities:
            brushes = entity.brushes
            if not brushes:
                continue
                
            print(f"Entity '{entity.classname}' has {len(brushes)} brushes")
            
            for brush in brushes:
                # Example: shift texture offset on all faces
                for face in brush.faces():
                    ox, oy = face.offset
                    face.offset = (ox + 16.0, oy)
                    count += 1
                    
    print(f"Modified {count} faces")

if __name__ == "__main__":
    main()
