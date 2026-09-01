import trenchbroom as tb


PANEL = None
COUNTER = 0


def on_table_selected(row, column):
    if PANEL is not None:
        PANEL.set_label_text("status", f"Table selection: row={row}, column={column}")


def on_tree_selected(row):
    if PANEL is not None:
        PANEL.set_label_text("status", f"Tree selection: row={row}")


def on_update():
    global COUNTER
    if PANEL is None:
        return
    COUNTER += 1

    PANEL.set_text_field("name", f"Run {COUNTER}")
    PANEL.set_text_area("message", f"Updated\nCounter={COUNTER}")

    rows = [[str(index), f"Item {index} (run {COUNTER})"] for index in range(1, 6)]
    PANEL.set_table_widget_rows("table", rows)

    tree_items = [[f"Node {index} (run {COUNTER})", f"Value {index}"] for index in range(1, 6)]
    PANEL.set_tree_widget_items("tree", tree_items)

    PANEL.set_label_text("status", f"Updated: {COUNTER}")


def build_panel():
    global PANEL
    PANEL = tb.create_plugin_panel("Advanced UI Demo")
    PANEL.clear()

    PANEL.add_label("Advanced UI Demo")
    PANEL.add_label_named("status", "Ready")

    group = PANEL.add_group("input_group", "Inputs")
    group.add_text_field("name", "Name", "", "Type something...")
    group.add_text_area("message", "Message", "Hello\nWorld", 120, "Multi-line text")

    PANEL.add_button_callback("Update", on_update)

    PANEL.add_label("Table")
    PANEL.add_table_widget(
        "table",
        ["ID", "Text"],
        [["1", "Item 1"], ["2", "Item 2"], ["3", "Item 3"]],
        160,
        on_table_selected,
    )

    PANEL.add_label("Tree")
    PANEL.add_tree_widget(
        "tree",
        ["Name", "Value"],
        [["Node 1", "Value 1"], ["Node 2", "Value 2"], ["Node 3", "Value 3"]],
        160,
        on_tree_selected,
    )


build_panel()
