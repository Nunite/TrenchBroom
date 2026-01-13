import tb

def main() -> None:
    doc = tb.current_document()
    if doc is None:
        raise RuntimeError("没有活动文档")

    distance = 8.0

    with doc.transaction(f"Python: Chamfer Edges ({distance})"):
        ok = doc.selection.chamfer_edges(distance)

    if not ok:
        print("倒角失败：请确认当前是 Edge Tool 且有选中边，且 distance > 0")
    else:
        print("倒角完成")

main()