# Custom Feature Refactor Notes

This file tracks structural issues found while adding tests for the migrated custom
features. These are intentionally not folded into the current test-hardening pass.

## MapFixture Header Coupling

New tests that include `mdl/MapFixture.h` can fail on MSVC unless the test file
also includes complete types for `Logger`, `gl::ResourceManager`, and
`kdl::task_manager`. `MapFixture` owns these through `std::unique_ptr`, so the
implicit inline destructor path requires complete types in each including
translation unit.

Follow-up: move `MapFixture` construction/destruction details to a `.cpp`, or
make `MapFixture.h` include the complete owned types itself.

## PieMenu Testability and UI Noise

`PieMenu` had selection math embedded inside `mouseMoveEvent`, plus an unused
private `getIndexForAngle` declaration and debug output from `showAt`. The angle
mapping has been extracted for unit testing, and widget-level tests now cover
enabled and disabled item execution through mouse move/release events. The widget
still mixes painting, input mapping, action execution, and popup lifetime in one
class.

Follow-up: split menu model/action collection from the popup widget.

## MiscPreferencePane Global Coupling

`MiscPreferencePane` builds the Pie Menu action picker directly from
`ActionManager::instance()` during construction and keeps the default plugin and
Pie Menu lists as private anonymous `QListWidget`s. Tests can cover loading and
resetting preferences, but they currently need a live `ActionManager` singleton
and have to identify lists by their contents.

Follow-up: extract the preference serialization and action-list model from the
widget, and give important child widgets stable object names if UI-level tests
remain useful.

## Python Scripting Test Fixture Coupling

The Python API has small helpers that can be tested directly, but meaningful
script smoke tests for `import tb2 as tb`, `tb.current_document()`, transactions,
selection, timers, and plugin panels currently depend on `PythonScripting`
running against a full `MapWindow`. That makes focused tests expensive and
harder to isolate from UI setup.

Follow-up: introduce a narrow scripting context interface or test fixture that
can provide the active `MapDocument`, logger, and plugin panel host without
constructing a full editor window.

Current lightweight `PythonUtils` tests pass, but Python initialization prints
`Could not find platform independent libraries <prefix>` in the test process.
The embedded runtime works for the tested C API conversions, but a full scripting
smoke fixture should configure Python home/path exactly like the application.

A `MapWindow` smoke test now exercises the real script entry point with
`import tb2 as tb`, `tb.current_document()`, `doc.entities`, `print()`, and a marker file
write. This exposed a Windows-only non-ASCII path bug where
`PythonScripting::runScript` opened the script via `_wfopen` but passed a narrow
`path.generic_string()` filename to `PyRun_SimpleFileEx`; the filename is now
passed as UTF-8 bytes. The remaining `<prefix>` warning means the embedded Python
runtime path still needs a proper application/test fixture setup before deeper
stdlib-heavy script tests are reliable.

The focused UI test matrix can also print `Unexpected SEH unhandled exception
filter on disengage` after Python-related tests on Windows. The tests pass, but
this points to another process-global runtime interaction that should be cleaned
up when the embedded Python lifecycle is refactored.

The lightweight tests now also exercise direct `tb.Vec3` and `tb.Plane` type
initialization and core math operations. These bindings still expose process-wide
`g_vec3Type` / `g_planeType` pointers, so repeated module initialization is not
fully isolated between test sections or interpreter contexts. That is acceptable
for the current smoke coverage, but the binding layer should eventually own type
registration through a dedicated module/context object instead of global mutable
state.

The `MapWindow` smoke test now also touches `doc.materials` and
`doc.material_collections` so the migrated `gl::Material` /
`gl::MaterialCollection` Python binding entry points are covered without loading
external assets. The default map window fixture can legitimately have empty
material lists, so this is only an API-shape smoke test, not a loaded-resource
behavior test.

Follow-up: add a focused scripting fixture that loads a small material collection
from test assets and verifies material names, collection names, and texture
dimensions through Python.

Python binding headers include `Python.h`, which conflicts with Qt's `slots`
macro if a test or implementation includes Qt widget headers first. The
`PythonPluginPanel` test uses a local `#undef slots` workaround before including
the Python binding headers, but this is an easy ordering trap and clang-format
include ordering can reintroduce it if the Python include is only reached
indirectly through a project header.

Follow-up: centralize Python/Qt include hygiene, for example by wrapping Python
includes in a small compatibility header that saves/restores or undefines Qt's
`slots` macro around `Python.h`.

## Python Plugin Panel API Size

`PythonPluginPanel.cpp` contains a large collection of Python C API argument
parsing, Qt widget construction, callback lifetime management, object-name
lookup, and action execution glue in one translation unit. The new unit tests
cover the currently exposed controls, but the implementation is hard to audit:
several callback paths duplicate `g_currentFrame` setup, and the table/tree/list
helpers each hand-roll Python list conversion.

Follow-up: extract shared Python-to-Qt list conversion helpers, a small widget
registry/lookup layer, and a callback RAII wrapper that owns `Py_INCREF` /
`Py_DECREF` and current-frame restoration.

The `MapWindow` Python smoke tests now cover `tb.create_plugin_panel()` with a
named label and a button through the real plugin inspector. They also cover
`tb.register_callback("selection_changed", ...)`, duplicate callback
registration suppression, callback unregistering, `tb.list_actions()`, and the
`tb.execute_action()` success and missing-action error paths. The button API
only accepts display text and an optional action path; unlike labels and other
keyed controls, it does not expose a stable key/object name. Tests have to locate
it by text, which is acceptable for smoke coverage but weak for plugin authors
and future automated UI tests.

Follow-up: add a keyed `add_button_named` or make `add_button` accept an
optional key while preserving the existing script signature.

## ModelBrowser Layout Coupling

The Model Browser's path filtering and search behavior was originally embedded
inside `ModelBrowserView::doReloadLayout`, mixed with font measurement and cell
layout mutation. That made core browser behavior hard to test without rendering
infrastructure.

Follow-up: keep the extracted entry-building helper as the stable model layer and
move more browser state transitions, such as filesystem rescans and folder tree
construction, behind similarly testable helpers.

## Unified Asset Browser Architecture

The GoldSrc asset browser is now split into three explicit layers:

- `AssetBrowserModel` owns asset classification, GoldSrc asset roots
  (`models`, `sprites`, `sound`), supported extensions, scan result metadata,
  mod-root filtering, changed-path detection, and browser entry construction.
- `AssetPreviewProvider` owns file reads and CPU preview decoding. The view asks
  it for `AssetPreviewState` values, so `.spr` decode failures, missing files,
  and unsupported assets are represented before rendering begins.
- `ModelBrowserView` remains a Qt/OpenGL adapter. It receives already-scanned
  `BrowserAsset` values, renders placeholders/model previews/sprite previews,
  and caches SPR preview GL textures so `doRender()` does not read files or
  upload the same preview texture every frame.

This keeps the first unified browser iteration focused on file-backed GoldSrc
assets (`.mdl`, `.spr`, `.wav`) without committing WAD textures or entity
templates to the same scanner. Future WAD support should be added as a separate
asset provider backed by the current map's material collections/WAD list, with
`AssetBrowserModel` or a small registry merging provider results into a single
searchable index.

Follow-up: add a small asset-source registry before adding WAD textures, and
avoid putting WAD/material collection enumeration into the current disk
scanner.

## OutlinerModel Notification Granularity

`OutlinerModel` currently handles node insert/remove notifications one node at a
time and comments that batched removals are tricky. This is a risk area for drag
reorder or multi-delete workflows because Qt model begin/end row calls must be
carefully paired and grouped by parent/row range.

The current implementation also reacts to `nodesWereAddedNotifier` by calling
`beginInsertRows` after the map has already changed. Qt item models generally
need `beginInsertRows` before the underlying data changes and `endInsertRows`
afterwards, so this should be migrated to a will/did notification pair or a
model reset/update strategy that follows Qt's expected ordering.

Follow-up: add focused model tests around add/remove/reparent notifications and
consider grouping notifications by parent with contiguous ranges.

## OutlinerTreeWidget Coupling

`OutlinerTreeWidget` mixes tree construction, node sorting, filtering, selection
sync, lock/visibility clicks, context menus, drag/drop reparenting, and current
group/layer highlighting in one large widget. The new tests cover construction,
filtering, selection sync, Escape deselection, and rebuild on added nodes through
public `QTreeWidget` APIs, but several useful helpers remain private and can only
be exercised indirectly.

The implementation also carries comment-level uncertainty about whether layers
should be shown or flattened, and contains non-ASCII inline comments. That policy
should be made explicit before deeper behavior tests are added.

Follow-up: extract an outliner tree model/view-state helper for sorting,
filtering, and node-to-item mapping, and keep context menus/drag-drop as thinner
widget adapters.

## Outliner Property Editor Selection Semantics

`OutlinerEntityPropertyEditor` has an empty-state branch for `No entity
selected`, but it reads `map.selection().allEntities()`. In the model layer,
`Selection::allEntities()` deliberately returns the worldspawn node when there
is no explicit selection, so the editor shows worldspawn properties instead of
the empty state in normal fixture setup. The new test documents the current
behavior rather than forcing the unreachable branch.

Follow-up: decide whether the outliner property panel should edit worldspawn on
empty selection or show a true empty state, then make that policy explicit with a
dedicated helper instead of relying on `Selection::allEntities()` semantics.

Property edits are applied through map commands such as `setEntityProperty` and
`removeEntityProperty`, which use the map's `applyAndSwap` path. Tests that hold
raw `EntityNode*` pointers across an edit can observe invalid/stale objects; the
new property editor test has to reacquire the selected entity after each command.

The editor also rebuilds the property row widgets after edits and deletes old
widgets through Qt's deferred deletion path. Tests and feature code must not hold
child widget pointers across commands that call `scheduleUpdate(true)`.

Follow-up: document pointer lifetime expectations for UI/plugin code that calls
map commands, and consider exposing stable IDs, post-command lookup helpers, or a
small property-editor view model so tests can assert state without depending on
ephemeral child widgets.

## Remaining Integration Test Gaps

## Automated Coverage Matrix

- Python scripting: `MapWindow` covers `import tb2 as tb`, `tb.current_document()`,
  document entity access, material/material-collection binding entry points,
  stdout script execution, transactions, `selection_changed` callbacks, timers,
  `tb.list_actions()`, `tb.execute_action()` success/error paths, plugin panel
  creation, `tb.create_brush`, selection brush APIs, and face attribute edits.
- Python utility bindings: `PythonUtils` covers string conversion, `tb.Vec3`,
  and `tb.Plane`; `PythonPluginPanel` covers named labels, fields, checkboxes,
  combos, visibility/enabled state, text areas, list/table/tree/html/color
  controls, button callbacks, and clearing.
- Plugin UI: `PluginInspector` covers plugin panel add/close/empty states.
- Pie Menu: `PieMenu` covers angle mapping, action item building, enabled and
  disabled mouse execution; `MapWindow` covers real `MapViewBase::showPieMenu()`
  action execution against a live map view.
- Preferences: `MiscPreferencePane` covers default plugin and Pie Menu
  preference loading/reset behavior.
- Model Browser: `ModelBrowserView` covers browser entry building, folder
  navigation entries, and search matching behavior.
- SmartModelEditor: `SmartModelEditor` covers model path normalization and
  `SmartPropertyEditorManager` selection for `model` / `mdl` keys.
- Outliner: `OutlinerModel` covers node indexing, display updates, lock/visible
  columns, drag mime data, and drop reparenting; `OutlinerTreeWidget` covers
  tree build, filtering, selection sync, Escape clear, and rebuild on added
  nodes; `OutlinerEntityPropertyEditor` covers selection notifications and
  property add/edit/remove behavior.
- Path tools: `PathTool` covers point stack behavior, path_corner chain
  creation, missing definition handling, and keyboard controller shortcuts;
  `MapViewToolBox` covers PathTool registration, activation, creation, and
  modal tool exclusivity.
- Model/geometry customizations: `GoldSrcMdlScaler` covers binary scaling and
  rejection cases; `Brush` and `Map_Geometry` cover chamfer behavior and
  undo/redo for vertices and edges.

The current automated coverage exercises the migrated custom feature helpers,
model-level operations, and lightweight Qt widgets, including Python document,
transaction, timer, selection callback, action-list/error-path, and plugin panel
smoke tests; plugin panel controls; default plugin/Pie Menu preference loading;
model browser entry building; PathTool entity creation and `MapViewToolBox`
activation; OutlinerModel basics; OutlinerTreeWidget tree/filter/selection
behavior; GoldSrc MDL scaling; and Brush/Map chamfer commands.

Some end-to-end workflows still require heavier UI fixtures or manual smoke
tests, but `MapViewBase::showPieMenu()` now has a focused integration smoke
through `MapWindow`: the test configures a real action path, asks the current
map view to show the popup, drives the `PieMenu` with mouse events, and verifies
that the action changes the document grid size.

This closes the previous action-execution gap, but it also exposed a brittle
popup lifetime edge in the test fixture. The menu must be detached and deleted
explicitly after the mouse-triggered action; otherwise the hidden `Qt::Popup`
child can survive into `MapWindow`/OpenGL widget teardown and crash on Windows.

Follow-up: split `MapViewBase::showPieMenu()` into a small action-host layer and
a thin popup adapter, so tests can cover action context and execution without
depending on `QOpenGLWidget`/`Qt::Popup` teardown order.

Python timer callbacks now have a `MapWindow` smoke test that registers
`tb.set_interval`, lets the Qt event loop run, writes a marker file in the
callback, and clears the timer. This protects the public API but still leaves
the timer implementation tied to process-global state (`g_timers`,
`g_nextTimerId`, and `g_currentFrame`) and Python lifetime ordering.

Follow-up: move timer ownership into a scripting/session object owned by the
active window or application controller, and provide deterministic teardown for
tests and map-window shutdown.

Python brush and face bindings now have a real `MapWindow` smoke test that
creates a brush with `tb.create_brush`, reads selection brush APIs, and updates
face texture, UV, rotation, and surface attributes. The implementation currently
modifies a copied brush and calls `BrushNode::setBrush` directly from Python
setters. Some setters emit `nodesDidChangeNotifier` manually, while others do
not, and these edits do not consistently go through the map command /
`applyAndSwap` path used by the rest of the editor.

Follow-up: route face edits through a document/map command API so undo/redo,
notifications, linked group handling, and UI refresh behavior are consistent.

`MapViewToolBox` originally exposed `pathTool()`, `togglePathTool()`,
`performPathCreation()`, and `removeLastPathPoint()` but did not construct or
register the `PathTool` in `createTools()`. The new toolbox test covers this
public entry now, but it also shows the feature is wired through several layers:
view actions -> `SwitchableMapViewContainer` -> `MapViewToolBox` -> `PathTool`.

Follow-up: consider centralizing custom map-view tool registration so adding a
new tool cannot leave a partially wired public API.

`PathToolController` now has lightweight keyboard coverage for undo/redo point
editing, Enter path creation, and Escape deactivation. The mouse-click path still
needs a focused picking fixture if we want automated coverage for hit-based point
placement and grid snapping through `InputState`.

An earlier `MapWindow` Python smoke attempt failed before entering the Python
section on Windows with `No mapping for the Unicode character exists in the
target multi-byte code page` during fixture setup. This was traced to
`fs::TestEnvironment::addNonAsciiDirs()` constructing non-ASCII paths from UTF-8
narrow string literals on Windows; the fixture now uses wide path literals for
those directory components.

Follow-up: build a reusable `MapWindow` scripting smoke fixture or a smaller
test double for the scripting context so these can be tested without launching a
full interactive editor session.

Python v2 now has a first production-architecture checkpoint: `pybind11` is
available through vcpkg/CMake, `tb2` registers as an embedded module,
`PythonRuntime` centralizes interpreter setup and script execution context,
manifest plugins load from `trenchbroom-plugin.json`, and `PythonPluginManager`
tracks plugin status/errors and cleanup. Focused tests cover manifest parsing,
`tb2.current_document()`, read-only document/selection/material access,
transaction smoke, plugin panel creation, event callback registration, and
plugin unload cleanup.

The stable import is now `tb2`; legacy `tb` scripts are kept only under
`python/examples/legacy_removed_tb` as migration references. v2 has since grown
session-owned callback/timer/panel cleanup, traceback/stdout/stderr coverage,
transactional write APIs, material collection browsing, selection transforms,
chamfer APIs, advanced PluginPanel controls, HTML views, and migrated manifest
examples including the Git panel.

Follow-up: keep strengthening handle invalidation coverage for map reload,
deleted nodes, and brush geometry replacement, and decide whether old legacy
source files should be archived or deleted from `lib/TbUiLib/src/python`.
