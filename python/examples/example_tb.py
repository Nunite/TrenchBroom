import tb

_ADDON = None

class VertexToolAddon:
    def __init__(self) -> None:
        self._panel = tb.create_plugin_panel("顶点工具面板")
        self._pivot = None  # (x, y, z)
        self._message = "就绪"
        self._pivot_mode = "first"
        self._require_explicit_entities = False
        self._build_ui()
        self._refresh_status()

    def _build_ui(self) -> None:
        self._panel.clear()
        self._panel.add_label_named("status", "")
        self._panel.add_button_callback("记录中心点", self.op_record_pivot)
        self._panel.add_button_callback("切换中心点模式", self.op_toggle_pivot_mode)
        self._panel.add_button_callback("切换目标实体要求", self.op_toggle_target_mode)
        self._panel.add_float_field("chamfer_dist", "倒角距离", 8.0, 0.0, 100000.0, 2, 1.0)
        self._panel.add_button_callback("倒角（顶点工具所选顶点）", self.op_chamfer_vertices)
        self._panel.add_int_field("dup_count", "重复次数", 10, 1, 999)
        self._panel.add_float_field("step_x", "X轴步进", 128.0, -100000.0, 100000.0, 2, 1.0)
        self._panel.add_float_field("step_y", "Y轴步进", 0.0, -100000.0, 100000.0, 2, 1.0)
        self._panel.add_float_field("step_z", "Z轴步进", 0.0, -100000.0, 100000.0, 2, 1.0)
        self._panel.add_float_field("axis_x", "X轴系数", 0.0, -1.0, 1.0, 3, 0.1)
        self._panel.add_float_field("axis_y", "Y轴系数", 0.0, -1.0, 1.0, 3, 0.1)
        self._panel.add_float_field("axis_z", "Z轴系数", 1.0, -1.0, 1.0, 3, 0.1)
        self._panel.add_float_field("rotate_deg", "旋转角度", 15.0, -360.0, 360.0, 2, 1.0)
        self._panel.add_button_callback("应用 重复+旋转", self.op_apply_duplicate_rotate)

    def _status_text(self) -> str:
        lines = ["顶点工具面板"]
        if self._pivot is None:
            lines.append("- 中心点: <未记录>")
        else:
            x, y, z = self._pivot
            lines.append(f"- 中心点: ({x:.2f}, {y:.2f}, {z:.2f})")
        
        mode_name = "第一个" if self._pivot_mode == "first" else "平均"
        lines.append(f"- 中心点模式: {mode_name}")
        
        target_mode = "仅限显式实体" if self._require_explicit_entities else "任意选择"
        lines.append(f"- 目标模式: {target_mode}")
        lines.append(f"- 状态: {self._message}")
        return "\n".join(lines)

    def _refresh_status(self, msg: str | None = None) -> None:
        if msg is not None:
            self._message = msg
        self._panel.set_label_text("status", self._status_text())

    def _set_message(self, msg: str) -> None:
        self._refresh_status(msg)

    def _get_list(self, obj, name: str):
        attr = getattr(obj, name, None)
        if attr is None:
            return None
        return attr() if callable(attr) else attr

    def _current_document(self):
        doc = tb.Document.current()
        if doc is None:
            self._set_message("没有活动的文档")
            return None
        return doc

    def _current_selection(self, doc):
        if hasattr(doc, "get_selection"):
            sel = doc.get_selection()
        else:
            sel_attr = doc.selection
            sel = sel_attr() if callable(sel_attr) else sel_attr
        return sel

    def _selected_entity_count(self, sel) -> int:
        entities = self._get_list(sel, "entities")
        if entities is None:
            return 0
        return len(entities)

    def _has_target_selection(self, sel) -> bool:
        if self._require_explicit_entities:
            return self._selected_entity_count(sel) > 0
        all_entities = self._get_list(sel, "all_entities")
        return all_entities is not None and len(all_entities) > 0

    def _record_pivot_from_vertices(self, verts) -> bool:
        if len(verts) == 0:
            self._set_message("顶点工具未选择任何顶点")
            return False
        if self._pivot_mode == "average":
            sx = 0.0
            sy = 0.0
            sz = 0.0
            for v in verts:
                sx += v[0]
                sy += v[1]
                sz += v[2]
            n = float(len(verts))
            self._pivot = (sx / n, sy / n, sz / n)
        else:
            self._pivot = tuple(verts[0])
        self._set_message("中心点已记录")
        return True

    def op_record_pivot(self) -> None:
        doc = self._current_document()
        if doc is None:
            return
        verts = doc.vertex_tool_vertices()
        self._record_pivot_from_vertices(verts)

    def op_toggle_pivot_mode(self) -> None:
        self._pivot_mode = "average" if self._pivot_mode == "first" else "first"
        self._set_message("中心点模式已更新")

    def op_toggle_target_mode(self) -> None:
        self._require_explicit_entities = not self._require_explicit_entities
        self._set_message("目标模式已更新")

    def op_chamfer_vertices(self) -> None:
        doc = self._current_document()
        if doc is None:
            return
        sel = self._current_selection(doc)
        dist = self._read_float("chamfer_dist")
        ok = sel.chamfer_vertices(dist)
        self._set_message("倒角完成" if ok else "倒角失败（请检查是否选中了顶点工具顶点）")

    def _read_int(self, key: str) -> int:
        return int(self._panel.get_int_field(key))

    def _read_float(self, key: str) -> float:
        return float(self._panel.get_float_field(key))

    def op_apply_duplicate_rotate(self) -> None:
        if self._pivot is None:
            self._set_message("未记录中心点")
            return
        doc = self._current_document()
        if doc is None:
            return
        sel = self._current_selection(doc)
        if not self._has_target_selection(sel):
            if self._require_explicit_entities:
                self._set_message("未选择显式实体")
            else:
                self._set_message("选择为空")
            return

        px, py, pz = self._pivot
        duplicate_count = self._read_int("dup_count")
        step_x = self._read_float("step_x")
        step_y = self._read_float("step_y")
        step_z = self._read_float("step_z")
        axis_x = self._read_float("axis_x")
        axis_y = self._read_float("axis_y")
        axis_z = self._read_float("axis_z")
        rotate_deg = self._read_float("rotate_deg")

        tx_cm = getattr(doc, "transaction", None)
        tx = tx_cm("Python: 围绕中心点重复并旋转") if callable(tx_cm) else tb.transaction(
            "Python: 围绕中心点重复并旋转"
        )
        with tx:
            for _ in range(duplicate_count):
                sel.duplicate()
                sel.translate(step_x, step_y, step_z)
                sel.rotate(axis_x, axis_y, axis_z, rotate_deg, px, py, pz)

        self._set_message("已应用重复+旋转")


def register() -> None:
    global _ADDON
    _ADDON = VertexToolAddon()


def main() -> None:
    register()

if __name__ == "__main__":
    main()
