# Python Scripting (Phase 1 Experiment)

## Goal

Embed Python into TrenchBroom and expose a small API that can execute existing TrenchBroom actions by their action path (the same paths used for menus/shortcuts).

This first experiment focuses on:

- Running a `.py` file from the UI
- Calling `tb.execute_action("Menu/...")` from Python
- Discovering available actions via `tb.list_actions()`
- Exposing a minimal map object model for selection + entity properties

## Build Requirements

### vcpkg dependency

This experiment adds `python3` to [vcpkg.json](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/vcpkg.json).

After re-configuring the project, vcpkg will install Python development/runtime libraries automatically.

### CMake

The top-level CMake now requires Python3:

- `find_package(Python3 COMPONENTS Interpreter Development REQUIRED)`

If CMake cannot find Python3, ensure your vcpkg toolchain is active (this repo already sets `CMAKE_TOOLCHAIN_FILE` to vcpkg).

## How To Use

### UI entry point

In TrenchBroom:

- Open the map window
- Use menu: `Run` → `Run Python Script...`
- Select a `.py` file

If the script fails, TrenchBroom will show a warning dialog and print details to the TrenchBroom console/log.

## Python API

The embedded module name is `tb`.

### `tb.current_document() -> Document | None`

Returns the active document, or `None` if there is no active map window.

### `tb.document() -> Document`

Returns the active document.

Raises `RuntimeError` if there is no active map window.

### `Document.current() -> Document | None`

Returns the active document, or `None` if there is no active map window.

### `Document.selection -> Selection` / `Document.selection() -> Selection`

Returns the current selection.

### `Selection.entities -> list[Entity]` / `Selection.entities() -> list[Entity]`

Returns the currently selected entities.

### `Selection.all_entities -> list[Entity]` / `Selection.all_entities() -> list[Entity]`

Returns the entities that commands act on based on the current selection.

This includes:
- selected entities
- parent entities of selected brushes/patches
- entities contained in selected groups

### `Selection.set_property(key: str, value: str, default_to_protected: bool = False) -> bool`

Sets a key/value property on the current selection (undoable).

### `Selection.remove_property(key: str) -> bool`

Removes a key from the current selection (undoable).

### `Selection.rename_property(old_key: str, new_key: str) -> bool`

Renames a key on the current selection (undoable).

### `Selection.clear() -> None`

Clears the current selection (undoable).

### `Entity.classname -> str` / `Entity.classname() -> str`

Returns the entity classname.

### `Entity.keys() -> list[str]`

Returns the entity's property keys.

### `Entity.get(key: str, default: Any = None) -> Any`

Returns the property value (string) or `default` if not set.

### `tb.execute_action(path: str) -> None`

Executes an existing TrenchBroom action by its action path, for example:

- `"Menu/Edit/Undo"`
- `"Menu/Run/Compile..."`
- `"Menu/View/Grid/Toggle Grid"`

If the action does not exist, a `KeyError` is raised.

If the action exists but is currently disabled, a `RuntimeError` is raised.

### `tb.list_actions() -> list[str]`

Returns a list of all registered action paths.

## Example Script

Save as `example.py` and run via `Run → Run Python Script...`:

```python
import tb

doc = tb.Document.current()
if doc is None:
    print("No active document")
    raise SystemExit(1)

sel = doc.selection
entities = sel.entities
print("selected entity count:", len(entities))
for e in entities:
    print("classname:", e.classname)

tb.execute_action("Menu/File/Preferences...")
```

## Notes / Constraints

- Scripts run in-process with full access to the machine and user files.
- Actions are executed on the UI thread and use the normal TrenchBroom action dispatch.
- The object model is intentionally minimal and currently focused on selection + entity properties.
