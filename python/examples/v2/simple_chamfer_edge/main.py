import tb2 as tb


distance = 8.0
doc = tb.current_document()

with doc.transaction(f"Python v2: Chamfer Edges ({distance})"):
    ok = doc.selection.chamfer_edges(distance)

if ok:
    print("Chamfer complete")
else:
    print("Chamfer failed: select edge handles in the edge tool and try again")
