# TrenchBroom Python API Reference {#python_api_reference}

Welcome to the TrenchBroom Python API reference documentation. TrenchBroom embeds a high-performance Python v2 (`tb2`) runtime that allows developers and level designers to automate geometry generation, manipulate entities, inspect maps, and build declarative native UI panels.

::: {.api-grid}
[**1. Quickstart & Concepts**\
Transaction context managers, atomic undo/redo, and coordinate systems.](#quickstart){.api-card}

[**2. tb2 Root Module**\
Document handles, panel factories, and 3D vector/plane mathematics.](#the_tb2_root_module){.api-card}

[**3. Document Access**\
Map queries, selections, materials, transactions, and UV updates.](#tb2_document){.api-card}

[**4. Selection & Transforms**\
Spatial manipulation (translate/rotate/scale), cloning, and chamfering.](#tb2_selection){.api-card}

[**5. Geometry & Elements**\
Polyhedral Brush, polygon Face UV alignments, and Entity properties.](#geometry_and_elements){.api-card}

[**6. PluginPanel UI**\
Declarative forms, spinboxes, color pickers, tables, and tree views.](#tb2_pluginpanel){.api-card}
:::

## Quickstart & Architecture {#quickstart}

All scripting in TrenchBroom interacts with the editor through the built-in `tb2` module. You can execute commands interactively in the **Python Console** or author persistent manifest-based plugins.

```python
import tb2

# Access the active map document
doc = tb2.current_document()

# Wrap modifications in a named transaction for atomic undo/redo
with doc.transaction("Normalize Selected Lights"):
    for ent in doc.selection.all_entities:
        if ent.classname == "light":
            ent["light"] = "200"

print(f"Map contains {len(doc.entities)} entities.")
```

### Key Concepts {#key_concepts}

- **Transactional Integrity**: All modifications to documents should be wrapped in `with doc.transaction("Action Name"):`. If an unhandled Python exception occurs within the block, all changes are automatically rolled back.
- **Immediate Selection Reactivity**: Operations on `doc.selection` immediately update 2D/3D viewports and inspectors.
- **Coordinate System**: TrenchBroom uses standard Quake/GoldSrc world coordinates: X is right (East), Y is forward (North), and Z is up.

---

## Core Module: tb2 {#the_tb2_root_module}

The root `tb2` module provides top-level access to the active document, plugin UI factory functions, and vector mathematics primitives.

### Top-Level Functions {#tb2_functions}

#### `tb2.current_document()` {#tb2_current_document}

Returns a handle to the active map document currently open in the editor.

- **Returns**: <span class="type-badge">Document</span> The active map document.
- **Return Type**: `tb2.Document`
- **Raises**: `RuntimeError` if no map document is active.

```python
doc = tb2.current_document()
print(doc.path)
```

#### `tb2.create_plugin_panel(title)` {#tb2_create_plugin_panel}

Creates and registers a declarative interactive panel in the **Plugins** inspector tab.

- **Parameters**:
  - `title` (*str*) – Display title shown in the inspector tab header.
- **Returns**: <span class="type-badge">PluginPanel</span> The created panel instance.
- **Return Type**: `tb2.PluginPanel`

```python
panel = tb2.create_plugin_panel("Surface Aligner")
panel.add_label("Align selected faces to the world grid.")
```

#### `tb2.selected_brushes()` / `tb2.selectedBrushes()` {#tb2_selected_brushes}

Returns a list of all `Brush` handles in the active selection.

- **Returns**: `list[tb2.Brush]`

#### `tb2.selected_entities(include_brushes=False)` / `tb2.selectedEntities(include_brushes=False)` {#tb2_selected_entities}

Returns directly selected `Entity` handles. Pass `include_brushes=True` to also include the parent entities of selected brushes and individually selected faces.

- **Returns**: `list[tb2.Entity]`

#### `tb2.selected_faces()` / `tb2.selectedFaces()` {#tb2_selected_faces}

Returns the individually selected `Face` handles. Selecting a whole brush does not expand it into all of its faces.

- **Returns**: `list[tb2.Face]`

#### `tb2.translate(...)` {#tb2_translate}

Translates the active selection or a target object along the specified offset vector with automatic undo transaction.

#### `tb2.rotate(...)` {#tb2_rotate}

Rotates the active selection or a target object. Supports Euler angles `rotate(rx, ry, rz)` or axis-angle `rotate(ax, ay, az, angle)`.

#### `tb2.scale(...)` {#tb2_scale}

Scales the active selection or a target object uniformly or non-uniformly with automatic undo transaction.

#### `tb2.duplicate(target=None)` {#tb2_duplicate}

Duplicates the active selection (or target object) and updates the active selection to the cloned copies.

#### `tb2.delete_selection()` / `tb2.deleteSelection()` {#tb2_delete_selection}

Deletes all currently selected geometry and entities from the active map.

#### `tb2.deselect_all()` / `tb2.deselectAll()` {#tb2_deselect_all}

Clears the active map selection.

#### Selection and Event Helpers {#tb2_selection_and_event_helpers}

- `tb2.selection() -> Selection`: Returns the current selection handle.
- `tb2.selected_all_entities()` / `tb2.selectedAllEntities()`: Returns directly selected entities plus parent entities of selected brushes and faces.
- `tb2.register_callback(event, callback) -> int`: Registers a no-argument callback for `selection_changed`, `document_loaded`, or `document_saved`.
- `tb2.unregister_callback(token)`: Unregisters an event callback.
- `tb2.set_timeout(callback, milliseconds)` / `tb2.set_interval(callback, milliseconds)`: Creates a plugin-session timer. Timers require a persistent UI plugin session.
- `tb2.clear_interval(timer_id)`: Cancels either type of timer.

### Math & Geometry Primitives {#math_primitives}

#### `tb2.Vec3(x, y, z)` {#tb2_vec3}

Three-dimensional Cartesian vector representing coordinates, offsets, and directions.

- **Attributes**:
  - `x` (*float*): X-axis coordinate.
  - `y` (*float*): Y-axis coordinate.
  - `z` (*float*): Z-axis coordinate.
- **Methods**:
  - `length() -> float`: Euclidean vector magnitude.
  - `normalized() -> Vec3`: Unit vector in the same direction.
  - `dot(other: Vec3) -> float`: Dot product.
  - `cross(other: Vec3) -> Vec3`: Cross product.

```python
pos = tb2.Vec3(128.0, 64.0, 32.0)
offset = tb2.Vec3(0.0, 0.0, 16.0)
target = pos + offset
```

#### `tb2.Plane(normal, dist)` {#tb2_plane}

Hessian normal form plane definition ($N \cdot P - D = 0$).

- **Parameters**:
  - `normal` (*tb2.Vec3*) – Normalized plane normal vector.
  - `dist` (*float*) – Distance from coordinate origin along the normal.

---

## Document Access: Document {#tb2_document}

The `Document` class represents an open map file and provides map queries, transactions, selection control, and UV updates.

### Properties {#document_properties}

| Property | Type | Description |
| :--- | :--- | :--- |
| `selection` | <span class="type-badge">Selection</span> | Active selection container for querying and transforming objects. |
| `entities` | <span class="type-badge">list[Entity]</span> | All point and brush entities in the document. |
| `path` | <span class="type-badge">str \| None</span> | Current map path. New unsaved maps normally use `"unnamed.map"`; `None` is returned only when the internal path is empty. |
| `materials` | <span class="type-badge">list[Material]</span> | Materials currently loaded by the map. |
| `material_collections` | <span class="type-badge">list[MaterialCollection]</span> | Loaded material collections. |

### Methods {#document_methods}

#### `doc.transaction(name)` {#doc_transaction}

Context manager that groups all internal document mutations into a single undo/redo action.

- **Parameters**:
  - `name` (*str*) – Human-readable description displayed in the Undo history.

```python
with doc.transaction("Duplicate and Move"):
    doc.selection.duplicate()
    doc.selection.translate(0, 0, 64)
```

Other document methods include `save()`, `reload()`, `select(objects)`, `clear_selection()`, `vertex_tool_vertices()`, `set_triangle_uvs(triangles)`, `set_face_uvs(updates)`, and `set_face_uvs_with_split(updates)`.

### Handle Lifetime {#python_handle_lifetime}

`Document`, `Entity`, `Brush`, and `Face` objects are live handles, not snapshots. Closing or reloading a document and deleting nodes invalidates related handles. Changing brush geometry also invalidates previously acquired `Face` handles. Accessing an invalid handle raises `RuntimeError`; reacquire long-lived objects from `tb2.current_document()`, the current selection, or their parent object after such changes.

---

## Selection & Transforms: Selection {#tb2_selection}

The `Selection` object provides direct access to highlighted geometry and high-level spatial manipulation functions.

### Query Properties {#selection_queries}

- `sel.entity` (*Entity | None*): First relevant entity, including the parent entity of a selected brush or face. Returns `None` for an empty selection.
- `sel.brush` (*Brush | None*): First selected brush.
- `sel.properties` (*dict[str, str] | None*): Property snapshot of the first relevant entity.
- `sel.classname` (*str | None*): Classname of the first relevant entity.
- `sel.entities` (*list[Entity]*): Directly selected entities.
- `sel.all_entities` (*list[Entity]*): Directly selected entities plus parent entities of selected brushes and individually selected faces. Returns an empty list for an empty selection.
- `sel.brushes` (*list[Brush]*): Selected brushes.
- `sel.brush_faces` (*list[Face]*): List of individually selected brush faces.

`sel[key]` and `key in sel` read the first relevant entity. `sel[key] = value` is equivalent to `sel.set_property(key, value)` and writes to every relevant entity. Face-only selections target the parent entity of the face's brush. Use `create_if_missing=False` with `set_property()` to update only entities that already contain the key; it returns `False` when none match.

### Transformation Methods {#selection_transforms}

#### `sel.translate(dx, dy, dz)` {#sel_translate}

Translates all selected objects by the given coordinate delta.

- **Parameters**:
  - `dx`, `dy`, `dz` (*float*) – Displacement offsets along X, Y, and Z axes.

#### `sel.rotate(axis_x, axis_y, axis_z, angle_degrees, center_x=None, center_y=None, center_z=None)` {#sel_rotate}

Rotates selected objects around a pivot point and axis vector.

- **Parameters**: Axis components, angle in degrees, and optional center components.

#### `sel.scale(scale_x, scale_y, scale_z, center_x=None, center_y=None, center_z=None)` {#sel_scale}

Scales selected objects relative to a center point.

- **Parameters**: Per-axis scale factors and optional center components.

#### `sel.duplicate()` {#sel_duplicate}

Clones all selected brushes and entities, leaving the newly created duplicates selected.

#### `sel.chamfer_vertices(distance)` {#sel_chamfer_vertices}

Bevels selected vertices by cutting corners at the specified distance.

- **Parameters**:
  - `distance` (*float*) – Inset cut distance from original vertices.

#### `sel.chamfer_edges(distance, segments=1)` {#sel_chamfer_edges}

Bevels selected brush edges.

---

## Map Elements & Geometry {#geometry_and_elements}

### Brush {#tb2_brush}

Represents a convex 3D polyhedron bounded by half-space planes.

- `brush.entity` (*Entity*): Returns the parent entity owning this brush.
- `brush.faces()` (*list[Face]*): Returns the list of polygon faces comprising the brush.

### Face {#tb2_face}

Represents a single planar boundary polygon of a brush.

- `face.material` (*str*): Material/texture name assigned to the face.
- `face.texture_name` (*str*): Alias for the material/texture name.
- `face.vertices` (*list[Vec3]*): Ordered boundary polygon vertices.
- `face.uv_loops` (*list*): UV loop data.
- `face.offset` (*tuple[float, float]*): UV translation offset (U, V).
- `face.scale` (*tuple[float, float]*): UV scale multipliers.
- `face.rotation` (*float*): UV rotation angle in degrees.
- `face.surface_contents` (*int | None*): Surface contents value.
- `face.surface_flags` (*int | None*): Surface flags value.
- `face.surface_value` (*float | None*): Surface value.
- `face.set_material(name: str)`: Assigns a new material to the face.
- `face.set_uv_loops(loops)`: Writes UV loop data.

### Entity {#tb2_entity}

Represents point entities (monsters, lights, spawn points) and brush entities (`func_door`, `trigger_multiple`, `worldspawn`). Supports standard Python dictionary operations.

- `entity.classname` (*str*): Entity class definition.
- `entity.properties` (*dict[str, str]*): Dictionary containing all entity key-value properties.
- `entity.brushes` (*list[Brush]*): All brush geometry owned by this entity.
- `entity[key]` / `entity[key] = value`: Subscript reading and writing of entity properties.
- `key in entity` (*bool*): Checks if a property key exists on the entity.
- `entity.keys()` (*list[str]*): List of property keys.
- `entity.values()` (*list[str]*): List of property values.
- `entity.items()` (*list[tuple[str, str]]*): List of `(key, value)` pairs.
- `entity.get(key: str, default: str = None) -> str`: Retrieves a property value, or default if missing.
- `entity.set(key: str, value: str)`: Sets or updates a key-value property.
- `entity.remove(key: str)`: Removes a key-value property.
- `len(entity)` (*int*): Number of properties defined on the entity.

---

## UI & Plugin Panels: PluginPanel {#tb2_pluginpanel}

The `PluginPanel` class allows Python plugins to construct rich, responsive interfaces in the **Plugins** inspector tab.

### Form Inputs & Controls {#pluginpanel_controls}

| Method | Parameters | Description |
| :--- | :--- | :--- |
| `add_label(text)` | `text: str` | Adds static informational text. |
| `add_label_named(key, text)` | `key: str, text: str` | Adds a dynamic label whose text can be updated via `set_label_text(key, text)`. |
| `add_html_view(key, html, height, callback)` | `key, html, height, callback` | Adds rich HTML content; update it with `set_html_view(key, html)`. |
| `add_line_edit(text, callback)` | `text: str, callback: callable` | Compatibility text field that invokes a callback as text changes. |
| `add_text_field(key, label, value)` | `key: str, label: str, value: str` | Single-line string input field. |
| `add_text_area(key, label, value, height)` | `key, label, value, height` | Multi-line text input field. |
| `add_int_field(key, label, value, min, max)` | `key, label, value: int, min: int, max: int` | Integer spinbox with bounded limits. |
| `add_float_field(key, label, value, min, max, decimals, step)` | `key, label, value: float, min, max, decimals: int, step: float` | Floating-point numerical input field. |
| `add_checkbox(key, text, checked)` | `key: str, text: str, checked: bool` | Boolean toggle checkbox. |
| `add_combo_box(key, label, items, callback, current)` | `key, label, items: list[str], callback: callable, current: int` | Dropdown selection box. |
| `add_color_field(key, label, color)` | `key: str, label: str, color: tuple[int, int, int]` | RGB color picker input. |
| `add_button(text, callback)` | `text: str, callback: callable` | Push button triggering a Python function. |
| `add_button_callback(text, callback)` | `text: str, callback: callable` | Compatibility alias for `add_button`. |

Named fields expose matching getters such as `get_text_field`, `get_text_area`, `get_int_field`, `get_float_field`, `get_checkbox`, `get_combo_box_text`, and `get_color_field`. Text fields and text areas also provide `set_text_field` and `set_text_area`.

### Data Views & Containers {#pluginpanel_containers}

- `add_table_widget(key, columns, rows, height, callback)`: Displays multi-column tabular data with selectable rows.
- `set_table_widget_rows(key, rows)`: Replaces table rows.
- `add_tree_widget(key, columns, rows, height, callback)`: Displays hierarchical tree data with expandable nodes.
- `set_tree_widget_items(key, rows)`: Replaces tree items.
- `add_group(key, title)`: Creates a collapsible visual section group.
- `add_row(key)` / `add_column(key)`: Horizontal and vertical layout containers.
- `set_widget_visible(key, visible)`: Shows or hides a named control.
- `clear()`: Removes all controls from the current panel container.

---

## Practical Examples {#runnable_examples}

### Example 1: Linear Array Generator {#example_linear_array}

```python
import tb2

panel = None

def on_generate():
    doc = tb2.current_document()
    if not doc or (not doc.selection.brushes and not doc.selection.entities):
        panel.set_label_text("status", "Error: Please select objects to duplicate.")
        return

    count = panel.get_int_field("count")
    dx = panel.get_float_field("dx")
    dy = panel.get_float_field("dy")
    dz = panel.get_float_field("dz")

    with doc.transaction(f"Linear Array ({count} copies)"):
        for _ in range(count):
            doc.selection.duplicate()
            doc.selection.translate(dx, dy, dz)

    panel.set_label_text("status", f"Success: Created {count} copies.")

def init_plugin():
    global panel
    panel = tb2.create_plugin_panel("Array Generator")
    panel.add_label("Duplicate active selection along a vector:")

    group = panel.add_group("params", "Parameters")
    group.add_int_field("count", "Count", value=4, min=1, max=100)
    group.add_float_field("dx", "Step X", value=128.0, min=-4096.0, max=4096.0, decimals=1, step=16.0)
    group.add_float_field("dy", "Step Y", value=0.0, min=-4096.0, max=4096.0, decimals=1, step=16.0)
    group.add_float_field("dz", "Step Z", value=0.0, min=-4096.0, max=4096.0, decimals=1, step=16.0)

    panel.add_button("Generate Array", on_generate)
    panel.add_label_named("status", "Ready")

init_plugin()
```

### Example 2: Batch Light Color Modifier {#example_batch_lights}

```python
import tb2

def randomize_light_colors():
    doc = tb2.current_document()
    if not doc:
        return

    lights = [e for e in doc.entities if e.classname == "light"]
    with doc.transaction("Normalize Light Values"):
        for light in lights:
            # Ensure standard brightness value
            if not light.get("light"):
                light.set("light", "300")

    print(f"Updated {len(lights)} lights.")

randomize_light_colors()
```
