import json
import math
import uuid

import tb2 as tb


COUNT_MODES = ["Fixed Count", "Fit Length", "Fit Path Length"]
MAX_COUNT = 512
HARD_MAX_COUNT = 2048
ARRAY_ID = "_tb_array_id"
ARRAY_ROLE = "_tb_array_role"
ARRAY_SETTINGS = "_tb_array_settings"
ROLE_SOURCE = "source"
ROLE_GENERATED = "generated"


def vec_len(value):
    return math.sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2])


def vec_sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vec_tuple(value):
    if hasattr(value, "x"):
        return (value.x, value.y, value.z)
    return (float(value[0]), float(value[1]), float(value[2]))


def bounds_from_brushes(brushes):
    points = []
    for brush in brushes:
        for face in brush.faces():
            points.extend(vec_tuple(point) for point in face.vertices)
    if not points:
        return None

    mins = [min(point[index] for point in points) for index in range(3)]
    maxs = [max(point[index] for point in points) for index in range(3)]
    size = (maxs[0] - mins[0], maxs[1] - mins[1], maxs[2] - mins[2])
    center = (
        (mins[0] + maxs[0]) * 0.5,
        (mins[1] + maxs[1]) * 0.5,
        (mins[2] + maxs[2]) * 0.5,
    )
    return {"min": tuple(mins), "max": tuple(maxs), "size": size, "center": center}


def path_length(points):
    if len(points) < 2:
        return 0.0
    total = 0.0
    for index in range(len(points) - 1):
        a = points[index]
        b = points[index + 1]
        total += vec_len((b.x - a.x, b.y - a.y, b.z - a.z))
    return total


def clean_json(value):
    if not value:
        return {}
    try:
        return json.loads(value)
    except ValueError:
        try:
            return json.loads(value.replace('\\"', '"'))
        except ValueError:
            return {}


class ArrayModifier:
    def __init__(self):
        self.panel = tb.create_plugin_panel("Array Modifier")
        self.array_id = None
        self.preview = None
        self.preview_signature = None
        self.path_points = []
        self.message = "Capture a source selection."
        self.merge_failures = 0
        self.updating = False
        self.defaults = {
            "mode": COUNT_MODES[0],
            "count": 5,
            "fit_length": 512.0,
            "rel_x": 1.0,
            "rel_y": 0.0,
            "rel_z": 0.0,
            "const_x": 0.0,
            "const_y": 0.0,
            "const_z": 0.0,
            "obj_x": 0.0,
            "obj_y": 0.0,
            "obj_z": 0.0,
            "rot_x": 0.0,
            "rot_y": 0.0,
            "rot_z": 1.0,
            "rot_deg": 0.0,
            "scale_x": 1.0,
            "scale_y": 1.0,
            "scale_z": 1.0,
            "pivot_x": 0.0,
            "pivot_y": 0.0,
            "pivot_z": 0.0,
            "merge": False,
            "merge_dist": 1.0,
            "merge_first_last": False,
            "uv_u": 0.0,
            "uv_v": 0.0,
            "start_cap": False,
            "end_cap": False,
        }
        self.timer = tb.set_interval(self.tick, 150)
        self.build_ui()
        self.refresh()

    def build_ui(self):
        self.panel.clear()
        self.panel.add_label_named("status", "")
        self.panel.add_combo_box(
            "mode", "Count Mode", COUNT_MODES, None, self.defaults["mode"]
        )
        self.panel.add_int_field("count", "Fixed Count", self.defaults["count"], 1, HARD_MAX_COUNT)
        self.panel.add_float_field(
            "fit_length", "Fit Length", self.defaults["fit_length"], 0.0, 1000000.0, 2, 16.0
        )

        self.panel.add_label("Relative Offset")
        self.panel.add_float_field("rel_x", "X", self.defaults["rel_x"], -32.0, 32.0, 3, 0.1)
        self.panel.add_float_field("rel_y", "Y", self.defaults["rel_y"], -32.0, 32.0, 3, 0.1)
        self.panel.add_float_field("rel_z", "Z", self.defaults["rel_z"], -32.0, 32.0, 3, 0.1)

        self.panel.add_label("Constant Offset")
        self.panel.add_float_field("const_x", "X", self.defaults["const_x"], -100000.0, 100000.0, 2, 8.0)
        self.panel.add_float_field("const_y", "Y", self.defaults["const_y"], -100000.0, 100000.0, 2, 8.0)
        self.panel.add_float_field("const_z", "Z", self.defaults["const_z"], -100000.0, 100000.0, 2, 8.0)

        self.panel.add_label("Object Offset Per Copy")
        self.panel.add_float_field("obj_x", "Move X", self.defaults["obj_x"], -100000.0, 100000.0, 2, 8.0)
        self.panel.add_float_field("obj_y", "Move Y", self.defaults["obj_y"], -100000.0, 100000.0, 2, 8.0)
        self.panel.add_float_field("obj_z", "Move Z", self.defaults["obj_z"], -100000.0, 100000.0, 2, 8.0)
        self.panel.add_float_field("rot_x", "Axis X", self.defaults["rot_x"], -1.0, 1.0, 3, 0.1)
        self.panel.add_float_field("rot_y", "Axis Y", self.defaults["rot_y"], -1.0, 1.0, 3, 0.1)
        self.panel.add_float_field("rot_z", "Axis Z", self.defaults["rot_z"], -1.0, 1.0, 3, 0.1)
        self.panel.add_float_field("rot_deg", "Rotate", self.defaults["rot_deg"], -360.0, 360.0, 2, 1.0)
        self.panel.add_float_field("scale_x", "Scale X", self.defaults["scale_x"], 0.01, 100.0, 3, 0.05)
        self.panel.add_float_field("scale_y", "Scale Y", self.defaults["scale_y"], 0.01, 100.0, 3, 0.05)
        self.panel.add_float_field("scale_z", "Scale Z", self.defaults["scale_z"], 0.01, 100.0, 3, 0.05)
        self.panel.add_float_field("pivot_x", "Pivot X", self.defaults["pivot_x"], -100000.0, 100000.0, 2, 8.0)
        self.panel.add_float_field("pivot_y", "Pivot Y", self.defaults["pivot_y"], -100000.0, 100000.0, 2, 8.0)
        self.panel.add_float_field("pivot_z", "Pivot Z", self.defaults["pivot_z"], -100000.0, 100000.0, 2, 8.0)

        self.panel.add_checkbox("merge", "Merge / snap adjacent copies", self.defaults["merge"])
        self.panel.add_float_field("merge_dist", "Merge Distance", self.defaults["merge_dist"], 0.0, 1024.0, 2, 1.0)
        self.panel.add_checkbox("merge_first_last", "Merge First/Last", self.defaults["merge_first_last"])
        self.panel.add_float_field("uv_u", "UV Offset U", self.defaults["uv_u"], -100000.0, 100000.0, 2, 1.0)
        self.panel.add_float_field("uv_v", "UV Offset V", self.defaults["uv_v"], -100000.0, 100000.0, 2, 1.0)
        self.panel.add_checkbox("start_cap", "Start Cap", self.defaults["start_cap"])
        self.panel.add_checkbox("end_cap", "End Cap", self.defaults["end_cap"])

        self.panel.add_button_callback("Capture Source", self.capture_source)
        self.panel.add_button_callback("Record Path", self.record_path)
        self.panel.add_button_callback("Preview", self.preview_array)
        self.panel.add_button_callback("Cancel Preview", self.cancel_preview)
        self.panel.add_button_callback("Commit Live", self.commit_live)
        self.panel.add_button_callback("Apply", self.apply)

    def source_group(self):
        if not self.array_id:
            return None
        for group in tb.current_document().groups:
            if group.get(ARRAY_ID) == self.array_id and group.get(ARRAY_ROLE) == ROLE_SOURCE:
                return group
        return None

    def generated_groups(self):
        if not self.array_id:
            return []
        return [
            group
            for group in tb.current_document().groups
            if group.get(ARRAY_ID) == self.array_id and group.get(ARRAY_ROLE) == ROLE_GENERATED
        ]

    def settings(self):
        result = {
            "mode": self.panel.get_combo_box_text("mode"),
            "count": int(self.panel.get_int_field("count")),
            "fit_length": float(self.panel.get_float_field("fit_length")),
            "rel_x": float(self.panel.get_float_field("rel_x")),
            "rel_y": float(self.panel.get_float_field("rel_y")),
            "rel_z": float(self.panel.get_float_field("rel_z")),
            "const_x": float(self.panel.get_float_field("const_x")),
            "const_y": float(self.panel.get_float_field("const_y")),
            "const_z": float(self.panel.get_float_field("const_z")),
            "obj_x": float(self.panel.get_float_field("obj_x")),
            "obj_y": float(self.panel.get_float_field("obj_y")),
            "obj_z": float(self.panel.get_float_field("obj_z")),
            "rot_x": float(self.panel.get_float_field("rot_x")),
            "rot_y": float(self.panel.get_float_field("rot_y")),
            "rot_z": float(self.panel.get_float_field("rot_z")),
            "rot_deg": float(self.panel.get_float_field("rot_deg")),
            "scale_x": float(self.panel.get_float_field("scale_x")),
            "scale_y": float(self.panel.get_float_field("scale_y")),
            "scale_z": float(self.panel.get_float_field("scale_z")),
            "pivot_x": float(self.panel.get_float_field("pivot_x")),
            "pivot_y": float(self.panel.get_float_field("pivot_y")),
            "pivot_z": float(self.panel.get_float_field("pivot_z")),
            "merge": bool(self.panel.get_checkbox("merge")),
            "merge_dist": float(self.panel.get_float_field("merge_dist")),
            "merge_first_last": bool(self.panel.get_checkbox("merge_first_last")),
            "uv_u": float(self.panel.get_float_field("uv_u")),
            "uv_v": float(self.panel.get_float_field("uv_v")),
            "start_cap": bool(self.panel.get_checkbox("start_cap")),
            "end_cap": bool(self.panel.get_checkbox("end_cap")),
        }
        return result

    def settings_text(self):
        return json.dumps(self.settings(), sort_keys=True, separators=(",", ":"))

    def load_settings(self, text):
        loaded = clean_json(text)
        if not loaded:
            return
        for key in self.defaults:
            if key in loaded:
                self.defaults[key] = loaded[key]
        self.build_ui()

    def source_signature(self):
        source = self.source_group()
        if source is None:
            return None
        values = []
        for brush in source.brushes:
            for face in brush.faces():
                values.append(face.texture_name)
                for vertex in face.vertices:
                    values.extend(round(component, 4) for component in vec_tuple(vertex))
        return tuple(values)

    def signature(self):
        return (self.settings_text(), self.source_signature(), len(self.path_points), path_length(self.path_points))

    def refresh(self, message=None):
        if message is not None:
            self.message = message
        source = self.source_group()
        source_text = "none" if source is None else source.name
        generated = len(self.generated_groups())
        path_text = f"{len(self.path_points)} points, {path_length(self.path_points):.1f} units"
        merge_note = ""
        if source is not None and self.panel.get_checkbox("merge"):
            merge_note = "\nMerge note: adjacent convex copies are merged when TB can keep a valid brush."
            if self.merge_failures:
                merge_note += f"\n{self.merge_failures} generated copies could not merge convexly."
        caps_note = ""
        if self.panel.get_checkbox("start_cap") or self.panel.get_checkbox("end_cap"):
            caps_note = "\nCap note: cap toggles are stored for compatibility; separate cap sources are not exposed yet."
        self.panel.set_label_text(
            "status",
            f"Source: {source_text}\nGenerated groups: {generated}\nPath: {path_text}\nStatus: {self.message}{merge_note}{caps_note}",
        )

    def capture_source(self):
        self.cancel_preview()
        doc = tb.current_document()
        selection = doc.selection

        if len(selection.groups) == 1 and selection.groups[0].get(ARRAY_ROLE) == ROLE_SOURCE:
            group = selection.groups[0]
            self.array_id = group.get(ARRAY_ID)
            self.load_settings(group.get(ARRAY_SETTINGS, ""))
            self.refresh("Loaded existing array source.")
            return

        if not selection.objects:
            self.refresh("Select source objects first.")
            return

        group = selection.group("Array Source")
        self.array_id = group.get(ARRAY_ID) or str(uuid.uuid4())
        group.set(ARRAY_ID, self.array_id)
        group.set(ARRAY_ROLE, ROLE_SOURCE)
        group.set(ARRAY_SETTINGS, self.settings_text())
        self.refresh("Source captured.")

    def record_path(self):
        self.path_points = list(tb.current_document().vertex_tool_vertices())
        if len(self.path_points) < 2:
            self.refresh("Need at least 2 selected vertex handles for Fit Path Length.")
            return
        self.refresh("Path recorded.")
        if self.preview is not None:
            self.rebuild_preview()

    def step_vector(self, settings, source_bounds):
        size = source_bounds["size"]
        step = (
            size[0] * settings["rel_x"] + settings["const_x"] + settings["obj_x"],
            size[1] * settings["rel_y"] + settings["const_y"] + settings["obj_y"],
            size[2] * settings["rel_z"] + settings["const_z"] + settings["obj_z"],
        )
        if settings["merge"] and settings["merge_dist"] > 0:
            step = tuple(0.0 if abs(value) <= settings["merge_dist"] else value for value in step)
        return step

    def instance_count(self, settings, step):
        mode = settings["mode"]
        if mode == "Fixed Count":
            count = settings["count"]
        else:
            length = settings["fit_length"]
            if mode == "Fit Path Length":
                length = path_length(self.path_points)
                if length <= 0:
                    self.message = "Record a path before using Fit Path Length."
                    return 0
            step_len = vec_len(step)
            if step_len <= 0.0001:
                self.message = "Offset is zero; cannot compute fit count."
                return 0
            count = int(math.floor(length / step_len)) + 1
        if count > HARD_MAX_COUNT:
            self.message = f"Count {count} exceeds hard limit {HARD_MAX_COUNT}."
            return 0
        if count > MAX_COUNT:
            self.message = f"Count {count} exceeds preview limit {MAX_COUNT}."
            return 0
        return max(1, count)

    def delete_generated(self):
        groups = self.generated_groups()
        if groups:
            tb.current_document().delete(groups)

    def select_source(self):
        source = self.source_group()
        if source is None:
            return None
        tb.current_document().selection.set([source])
        return source

    def apply_uv_offset(self, group, index, u, v):
        if abs(u) < 0.0001 and abs(v) < 0.0001:
            return
        brush_count = len(group.brushes)
        for brush_index in range(brush_count):
            face_count = len(group.brushes[brush_index].faces())
            for face_index in range(face_count):
                brush = group.brushes[brush_index]
                face = brush.faces()[face_index]
                offset = face.offset
                face.offset = (offset[0] + u * index, offset[1] + v * index)

    def transform_copy(self, group, index, settings, step, source_bounds):
        doc = tb.current_document()
        selection = doc.selection
        selection.set([group])

        selection.translate(step[0] * index, step[1] * index, step[2] * index)

        pivot = (
            source_bounds["center"][0] + settings["pivot_x"],
            source_bounds["center"][1] + settings["pivot_y"],
            source_bounds["center"][2] + settings["pivot_z"],
        )
        angle = settings["rot_deg"] * index
        if abs(angle) > 0.0001 and vec_len((settings["rot_x"], settings["rot_y"], settings["rot_z"])) > 0.0001:
            selection.rotate(
                settings["rot_x"],
                settings["rot_y"],
                settings["rot_z"],
                angle,
                pivot[0],
                pivot[1],
                pivot[2],
            )

        sx = math.pow(settings["scale_x"], index)
        sy = math.pow(settings["scale_y"], index)
        sz = math.pow(settings["scale_z"], index)
        if abs(sx - 1.0) > 0.0001 or abs(sy - 1.0) > 0.0001 or abs(sz - 1.0) > 0.0001:
            selection.scale(sx, sy, sz, pivot[0], pivot[1], pivot[2])

        if selection.groups:
            group = selection.groups[0]
        self.apply_uv_offset(group, index, settings["uv_u"], settings["uv_v"])

        if settings["merge"] and len(group.brushes) > 1:
            merged = doc.convex_merge(group.brushes)
            if not merged:
                self.merge_failures += 1
            elif selection.groups:
                group = selection.groups[0]

        return group

    def build_array(self):
        source = self.select_source()
        if source is None:
            self.refresh("Capture a source first.")
            return False

        source_bounds = bounds_from_brushes(source.brushes)
        if source_bounds is None:
            self.refresh("Source has no brush geometry.")
            return False

        settings = self.settings()
        step = self.step_vector(settings, source_bounds)
        count = self.instance_count(settings, step)
        if count <= 0:
            self.refresh()
            return False

        self.merge_failures = 0
        self.delete_generated()
        doc = tb.current_document()
        generated = []
        for index in range(1, count):
            doc.selection.set([source])
            objects = doc.selection.duplicate_objects()
            if not objects:
                continue
            group = objects[0]
            group.set(ARRAY_ID, self.array_id)
            group.set(ARRAY_ROLE, ROLE_GENERATED)
            group = self.transform_copy(group, index, settings, step, source_bounds)
            generated.append(group)

        source.set(ARRAY_SETTINGS, self.settings_text())
        doc.selection.set([source])
        self.preview_signature = self.signature()
        self.refresh(f"Built {len(generated)} generated copies.")
        return True

    def preview_array(self):
        if self.preview is not None:
            self.rebuild_preview()
            return
        if self.source_group() is None:
            self.refresh("Capture a source first.")
            return
        self.preview = tb.current_document().transaction("Python v2: Array Modifier Preview")
        self.preview.__enter__()
        if not self.build_array():
            self.cancel_preview()

    def rebuild_preview(self):
        self.cancel_preview()
        self.preview_array()

    def cancel_preview(self):
        if self.preview is not None:
            self.preview.cancel()
            self.preview = None
        self.preview_signature = None
        self.refresh("Preview cancelled.")

    def commit_live(self):
        self.updating = True
        try:
            if self.preview is None:
                self.preview_array()
            if self.preview is None:
                return
            self.preview.commit()
            self.preview = None
            self.preview_signature = None
            self.refresh("Live array committed.")
        finally:
            self.updating = False

    def apply(self):
        self.updating = True
        try:
            if self.preview is not None:
                self.preview.commit()
                self.preview = None
                self.preview_signature = None

            doc = tb.current_document()
            groups = []
            source = self.source_group()
            if source is not None:
                groups.append(source)
            groups.extend(self.generated_groups())
            if not groups:
                self.refresh("Nothing to apply.")
                return

            with doc.transaction("Python v2: Apply Array Modifier"):
                for group in groups:
                    group.remove(ARRAY_SETTINGS)
                    group.remove(ARRAY_ROLE)
                    group.remove(ARRAY_ID)
            self.array_id = None
            self.refresh("Array applied as ordinary groups/brushes.")
        finally:
            self.updating = False

    def tick(self):
        if self.updating or self.preview is None:
            return
        signature = self.signature()
        if signature != self.preview_signature:
            self.rebuild_preview()


_tool = ArrayModifier()
