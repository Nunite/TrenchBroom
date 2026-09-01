# Linear Array Generator Example Plugin

An example UI plugin demonstrating TrenchBroom's `trenchbroom` Python API plugin system.

## Features Demonstrated
1. **Manifest Configuration**: Defined in `trenchbroom-plugin.json` (`pluginType: "ui"`).
2. **Custom UI Panel**: Registered on the **Plugins** inspector tab via `trenchbroom.create_plugin_panel`.
3. **Form Controls**: Integer input (`add_int_field`), float input (`add_float_field`), buttons, and status labels.
4. **Transaction Safety**: Batched geometric operations inside `with doc.transaction(...):` for clean single-step Undo/Redo.

## How to Test
1. Open TrenchBroom.
2. Go to **Preferences > Misc > Tools > Python Plugins**.
3. Add the path to `docs/examples/plugins` to your plugin directories.
4. Open a map, select one or more brushes/entities, and switch to the **Plugins** inspector tab to run the tool.
