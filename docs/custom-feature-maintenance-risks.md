# Custom Feature Maintenance Risks

This document tracks custom features that are useful, but currently more invasive or less maintainable than the upstream TrenchBroom style. Keep this file updated as the issues are fixed.

For a commit-oriented review of the current custom feature stack and suggested
optimization order, see `docs/custom-feature-architecture-review.md`.

## High Priority

### Python v2 Runtime and Plugin API

- Risk: `PythonV2Module.cpp` is a large mixed module containing bindings, UI panel construction, callbacks, transactions, and geometry helpers.
- Risk: process-global callback, event, handle, and transaction state makes document/window/plugin unload behavior harder to reason about.
- Suggested fix: split the v2 bindings by domain (`document`, `selection`, `geometry`, `panel`, `events`, `actions`) and move callback/timer ownership fully into `PythonPluginSession`.

### Outliner Tree and Property Editor

- Risk: `OutlinerTreeWidget` stores raw `mdl::Node*` in tree items and contains comments about avoiding `dynamic_cast` on deleted objects.
- Risk: `OutlinerEntityPropertyEditor` rebuilds its whole QWidget tree for many updates, which can reset scroll position and cause UI latency.
- Risk: embedded smart editors such as `SmartSkyboxEditor` can update map properties through a separate path from the native entity inspector; if they force a full Outliner rebuild, undo/redo and selection refresh can become visibly inconsistent.
- Risk: embedded smart editors that scan filesystem resources or load preview icons can make ordinary selection changes slow if they are rebuilt repeatedly.
- Suggested fix: move the tree to a model/view design with stable node ids or explicit invalidation; split property row creation into a row factory and update individual row values when possible; cache smart-editor resource scans and previews by game path, with explicit manual refresh for expensive reloads.

### Python v2 Event Dispatch

- Risk: editor notifications such as selection changes can become unexpectedly expensive if they initialize Python or import plugin modules when no plugin callback is registered.
- Suggested fix: keep event emission lazy; do not initialize Python for passive editor notifications, and check for registered callbacks before entering plugin dispatch.

### Sky Rendering

- Risk: `SkyRenderer` handles resource lookup, loose texture loading, sky brush geometry collection, GL state, and render submission in one class.
- Risk: `canRender()` can traverse sky brush geometry, and selection/locked renderers depend on `canRender()` to decide whether to hide original sky faces.
- Risk: sky brush geometry invalidation is currently coarse-grained; if future changes make it invalidate on selection changes, 3D selection feedback can become visibly slow on large maps.
- Suggested fix: split sky resource resolution, sky brush geometry caching, and rendering; invalidate geometry only when map visibility, brush geometry, or sky-related materials change.

## Medium Priority

### Model Browser and Resource Access

- Risk: UI-specific model browsing exposed `Map::gameFileSystem()` and `Map::reloadEntityModels(...)`, increasing the public surface of `mdl::Map`.
- Suggested fix: introduce a UI/resource facade or service for model browser operations, keeping `Map` focused on document state and core operations.

### Brush Chamfer Operations

- Risk: chamfer algorithms are implemented directly in `mdl::Brush`, increasing the core class size and upstream merge conflict surface.
- Suggested fix: move chamfer code into a dedicated geometry helper or command module, leaving `Brush` with smaller primitive operations.

### Preferences and Misc UI

- Risk: Misc preferences can become a catch-all for Python plugins, Pie Menu, language, and editor behavior.
- Suggested fix: keep the upstream preferences structure and use dedicated panes for Python plugins, Pie Menu, language, and custom tools.

## Lower Priority

### Generated Python Package Metadata

- Status: cleaned. `python/src/trenchbroom_api.egg-info` was removed from the repository, and `*.egg-info/` is ignored.
- Keep this as a guardrail: generated Python packaging metadata should not be committed again.

### Development Scripts and Debug Artifacts

- Risk: skybox/debug helper scripts and local notes can accumulate in the repository root.
- Suggested fix: keep reusable tools under `scripts/` or `tools/dev/`, and ignore/delete one-off debug output.
