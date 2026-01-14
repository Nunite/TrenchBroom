import tb

_ADDON = None


class ChamferEdgesAddon:
    def __init__(self) -> None:
        self._panel = tb.create_plugin_panel("倒角（边工具）")
        self._message = "就绪"
        self._build_ui()
        self._refresh_status()

    def _build_ui(self) -> None:
        self._panel.clear()
        self._panel.add_label_named("status", "")
        self._panel.add_float_field("distance", "倒角距离", 8.0, 0.0, 100000.0, 2, 1.0)
        self._panel.add_int_field("segments", "倒角段数", 1, 1, 64)
        self._panel.add_button_callback("执行倒角", self.op_apply)
        self._panel.add_button_callback("刷新状态", self.op_refresh)

    def _status_text(self) -> str:
        lines = ["倒角（边工具）"]
        lines.append(f"- 状态: {self._message}")
        return "\n".join(lines)

    def _refresh_status(self, msg: str | None = None) -> None:
        if msg is not None:
            self._message = msg
        self._panel.set_label_text("status", self._status_text())

    def _current_document(self):
        doc = tb.Document.current()
        return doc

    def _current_selection(self, doc):
        if hasattr(doc, "get_selection"):
            return doc.get_selection()
        sel_attr = doc.selection
        return sel_attr() if callable(sel_attr) else sel_attr

    def _read_int(self, key: str) -> int:
        return int(self._panel.get_int_field(key))

    def _read_float(self, key: str) -> float:
        return float(self._panel.get_float_field(key))

    def op_refresh(self) -> None:
        self._refresh_status("就绪")

    def op_apply(self) -> None:
        doc = self._current_document()
        if doc is None:
            self._refresh_status("没有活动的文档")
            return

        sel = self._current_selection(doc)
        distance = self._read_float("distance")
        segments = self._read_int("segments")

        tx_cm = getattr(doc, "transaction", None)
        tx = tx_cm("Python: Chamfer Edges") if callable(tx_cm) else tb.transaction(
            "Python: Chamfer Edges"
        )

        with tx:
            ok = sel.chamfer_edges(distance, segments)

        self._refresh_status("倒角完成" if ok else "倒角失败（请检查是否选中了边工具边）")


def register() -> None:
    global _ADDON
    _ADDON = ChamferEdgesAddon()


def main() -> None:
    register()


if __name__ == "__main__":
    main()

