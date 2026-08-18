# Python Scripting and Plugins {#python_scripting_and_plugins}

## Python Console {#python_console}

Open the **Python** tab in the bottom info panel to run Python v2 commands interactively against the active TrenchBroom session.

### Interactive Execution and Multiline Input {#console_execution}

The console provides an interactive REPL with dual-mode evaluation:

- **Expression Evaluation**: Single expressions (such as `tb2.current_document().selection.brushes` or `tb2.Vec3(128, 64, 0).length()`) are evaluated immediately, and their formatted string representation (`repr`) is printed to the console output.
- **Statement Blocks**: Complex multiline blocks containing `for` loops, function definitions, or `with doc.transaction("Action"):` contexts can be entered or pasted directly into the input field. The input field automatically expands to fit up to 4 lines of code.

### Keyboard Shortcuts and History {#console_shortcuts_and_history}

- **Execute**: Press #key(Ctrl)+#key(Return) or click **Run** to execute the command.
- **History Navigation**: Use #key(Up) and #key(Down) at the first or last input line to navigate through up to 100 previous commands. If you have unsubmitted text in the input box, it is automatically preserved as a draft while browsing history and restored when you return to the bottom with #key(Down).
- **Clear Output**: Click **Clear** to remove all logged console output.
- **Font Customization**: Configure the family and size under **Preferences > View > Fonts > Python Console**; only installed monospace families are listed, and **System Monospace** uses the platform default.

### Output and Error Reporting {#console_output_and_errors}

All standard output from Python's built-in `print(...)` function is captured and streamed directly into the console. When an unhandled exception or syntax error occurs, a formatted traceback with source line numbers is printed in red.

```python
doc = tb2.current_document()
with doc.transaction("Batch Align Entities"):
    for ent in doc.entities:
        if ent.classname == "light" and not ent.get("light"):
            ent.set("light", "300")
```

## Python Plugins {#python_plugins}

TrenchBroom features an embedded Python v2 runtime and extension system. Open **Preferences > Misc > Python Plugin Manager** to manage manifest-based UI plugins. The manager lists configured plugin directories, detected plugins, load status, metadata, and errors. Use search or **Only show issues** to diagnose larger plugin sets, then refresh after changing files.

#### Plugin Types and Lifecycle {#plugin_types_and_lifecycle}

TrenchBroom distinguishes between two plugin types:

- **UI Plugins (`pluginType: "ui"`)**: Persistent plugins that declare a `trenchbroom-plugin.json` manifest. They are loaded at startup or when refreshing the plugin manager, and can register custom panels in the **Plugins** inspector tab, global actions, event listeners, and timers.
- **Script plugins (`pluginType: "script"`)**: Standalone scripts executed on demand through the [Python Console](#python_console) or custom actions. Legacy `tb` compatibility is no longer part of the active plugin path; existing scripts should use `tb2` or `import tb2 as tb`.

#### Manifest File Format {#plugin_manifest_format}

Each UI plugin directory must contain a `trenchbroom-plugin.json` manifest file at its root:

```json
{
  "id": "com.example.my_plugin",
  "name": "My Custom Plugin",
  "version": "1.0.0",
  "apiVersion": 2,
  "pluginType": "ui",
  "entry": "main.py",
  "description": "A sample plugin with a custom inspector panel.",
  "author": "Author Name"
}
```

The manifest fields are defined as follows:

| Field | Type | Description |
| :--- | :--- | :--- |
| `id` | string | Unique plugin identifier |
| `name` | string | Display name shown in the UI |
| `version` | string | Semantic version string |
| `apiVersion` | integer | Must be `2` for Python v2 |
| `pluginType` | string | `"ui"` for persistent UI plugins, or `"script"` for scripts |
| `entry` | string | Relative path to the Python entry script |
| `description` | string | Optional description of the plugin |
| `author` | string | Optional author name or contact |

#### The tb2 Python API {#the_tb2_python_api}

All Python scripts and plugins access TrenchBroom through the embedded `tb2` module (`import tb2`). Key components include:

- `tb2.current_document()`: Returns the active `Document` handle representing the open map.
- `doc.transaction(name)`: A context manager (`with doc.transaction("Action Name"):`) that groups modifications into a single undo/redo step and automatically rolls back on Python exceptions.
- `doc.selection`: The `Selection` handle for querying selected objects (`brushes`, `entities`, `brush_faces`) and applying transformations (`translate`, `rotate`, `scale`, `duplicate`, `chamfer_vertices`, `chamfer_edges`).
- `doc.entities`: List of all `Entity` objects in the map. Access properties using `.get(key, default)` and `.set(key, value)`.
- `brush.faces()`: Returns the polygon `Face` objects comprising a brush, providing access to `.material`, `.offset`, `.scale`, `.rotation`, and `.vertices`.
- `tb2.Vec3(x, y, z)` and `tb2.Plane(normal, dist)`: 3D vector and plane math primitives.

#### Building Custom UI Panels {#building_custom_ui_panels}

UI plugins create interactive panels on the **Plugins** inspector tab using `tb2.create_plugin_panel(panel_id, title)`. The returned `PluginPanel` provides declarative controls:

- **Labels & Text**: `.add_label(text)`, `.add_label_named(key, text)`, `.set_label_text(key, text)`, and `.add_html_view(key, html, height, callback)`.
- **Form Inputs**: `.add_text_field(key, label, value)`, `.add_text_area(key, label, value)`, `.add_int_field(key, label, value, min, max)`, `.add_float_field(key, label, value, min, max, decimals, step)`, `.add_checkbox(key, text, checked)`, `.add_combo_box(key, label, items, callback, current)`, and `.add_color_field(key, label, color)`.
- **Buttons**: `.add_button(text, callback)`.
- **Data Views**: `.add_table_widget(key, columns, rows, height, callback)` and `.add_tree_widget(key, columns, rows, height, callback)`.
- **Layout Containers**: `.add_group(key, title)`, `.add_row(key)`, and `.add_column(key)`.

#### Plugin Example {#plugin_example}

Below is a complete, runnable UI plugin script that duplicates the current selection and translates it along an offset vector:

```python
import tb2

panel = None

def on_generate():
    doc = tb2.current_document()
    if not doc.selection.brushes and not doc.selection.entities:
        panel.set_label_text("status", "Please select at least one brush or entity.")
        return

    count = panel.get_int_field("count")
    dx = panel.get_float_field("dx")
    dy = panel.get_float_field("dy")
    dz = panel.get_float_field("dz")

    with doc.transaction(f"Linear Array ({count} copies)"):
        for _ in range(count):
            doc.selection.duplicate()
            doc.selection.translate(dx, dy, dz)

    panel.set_label_text("status", f"Successfully created {count} copies.")

def init_plugin():
    global panel
    panel = tb2.create_plugin_panel("com.example.array_tool", "Array Generator")
    panel.add_label("Duplicate the active selection along an offset vector:")

    group = panel.add_group("config", "Parameters")
    group.add_int_field("count", "Copies", value=3, min=1, max=50)
    group.add_float_field("dx", "Step X", value=64.0, min=-2048.0, max=2048.0, step=8.0)
    group.add_float_field("dy", "Step Y", value=0.0, min=-2048.0, max=2048.0, step=8.0)
    group.add_float_field("dz", "Step Z", value=0.0, min=-2048.0, max=2048.0, step=8.0)

    panel.add_button("Generate Array", on_generate)
    panel.add_label_named("status", "Ready")

init_plugin()
```
