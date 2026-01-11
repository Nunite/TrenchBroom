import tb


def main() -> None:
    doc = tb.document()
    sel = doc.selection()

    entities = sel.entities()
    print(f"selected explicit entity count: {len(entities)}")
    for e in entities:
        print("classname:", e.classname())
        print("targetname:", e.get("targetname", ""))

    all_entities = sel.all_entities()
    print(f"selected entity targets (all_entities): {len(all_entities)}")
    for e in all_entities:
        print("classname:", e.classname())

    if all_entities:
        sel.set_property("message", "hello from python")


if __name__ == "__main__":
    main()
