import tb2 as tb


doc = tb.current_document()
selection = doc.selection
entities = [entity for entity in selection.all_entities if entity.classname != "worldspawn"]

if len(entities) != 1:
    print(f"Select exactly one non-worldspawn entity. Selected: {len(entities)}")
else:
    entity = entities[0]
    total_angle = float(entity.get("_angle", "90.0"))
    count = int(entity.get("_count", "5"))
    pivot = [float(value) for value in entity.get("_pivot", "0 0 0").split()]
    axis = [float(value) for value in entity.get("_axis", "0 0 1").split()]

    if count < 1:
        raise RuntimeError("_count must be greater than 0")
    if len(pivot) != 3:
        raise RuntimeError("_pivot must contain 3 numbers")
    if len(axis) != 3:
        raise RuntimeError("_axis must contain 3 numbers")

    step_angle = total_angle / count
    with doc.transaction("Python v2: Spin Entity"):
        for _ in range(count):
            selection.duplicate()
            selection.rotate(axis[0], axis[1], axis[2], step_angle, pivot[0], pivot[1], pivot[2])

    print(f"Generated {count} copies around {pivot}")
