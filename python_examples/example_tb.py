import tb

def main() -> None:
    doc = tb.current_document()
    if doc is None:
        print("no current document")
        return

    sel_attr = doc.selection
    sel = sel_attr() if callable(sel_attr) else sel_attr
    if len(sel.all_entities()) == 0:
        print("selection is empty")
        return

    with tb.transaction("Python: duplicate 10x"):
        for _ in range(10):
            sel.duplicate()
            sel.translate(128, 0, 0)
            sel.rotate(0, 0, 1, 15)

    print("done")


if __name__ == "__main__":
    main()
