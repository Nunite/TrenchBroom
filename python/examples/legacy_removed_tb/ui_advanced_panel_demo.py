import tb

_PANEL: tb.PluginPanel | None = None
_COUNTER = 0


def _on_table_selected(row: int, column: int) -> None:
    if _PANEL is None:
        return
    _PANEL.set_label_text("status", f"Table selection: row={row}, column={column}")


def _on_tree_selected(row: int) -> None:
    if _PANEL is None:
        return
    _PANEL.set_label_text("status", f"Tree selection: row={row}")


def _on_update() -> None:
    global _COUNTER
    if _PANEL is None:
        return
    _COUNTER += 1

    _PANEL.set_text_field("name", f"Run {_COUNTER}")
    _PANEL.set_text_area("message", f"Updated\nCounter={_COUNTER}")

    rows = [[str(i), f"Item {i} (run {_COUNTER})"] for i in range(1, 6)]
    _PANEL.set_table_widget_rows("table", rows)

    tree_items = [[f"Node {i} (run {_COUNTER})", f"Value {i}"] for i in range(1, 6)]
    _PANEL.set_tree_widget_items("tree", tree_items)

    _PANEL.set_label_text("status", f"Updated: {_COUNTER}")


def main() -> None:
    global _PANEL
    _PANEL = tb.create_plugin_panel("Advanced UI Demo")
    _PANEL.clear()

    _PANEL.add_label("<b>Advanced UI Demo</b>")
    _PANEL.add_label_named("status", "Ready")

    group = _PANEL.add_group("input_group", "Inputs")
    group.add_text_field("name", "Name", "", "Type something...")
    group.add_text_area("message", "Message", "Hello\nWorld", 120, "Multi-line text")

    _PANEL.add_button_callback("Update", _on_update)

    _PANEL.add_label("<b>Table</b>")
    _PANEL.add_table_widget(
        "table",
        ["ID", "Text"],
        [["1", "Item 1"], ["2", "Item 2"], ["3", "Item 3"]],
        160,
        _on_table_selected,
    )

    _PANEL.add_label("<b>Tree</b>")
    _PANEL.add_tree_widget(
        "tree",
        ["Name", "Value"],
        [["Node 1", "Value 1"], ["Node 2", "Value 2"], ["Node 3", "Value 3"]],
        160,
        _on_tree_selected,
    )


if __name__ == "__main__":
    main()

