# Qt UI Modernization Progress

## Status

- Branch: `codex/qt-ui-modernization`
- Baseline commit: `4d3a1dca7` (`Restore shortcut preference semantics`)
- Last updated: 2026-08-15
- Current stage: discovery and roadmap complete; implementation has not started

## Objective

Modernize TrenchBroom's desktop UI with a compact, consistent workbench design while
preserving the existing C++ editor core, Qt application lifecycle, input handling, and
OpenGL viewports.

The target is a TrenchBroom-specific design influenced by the information density and
visual hierarchy of VS Code. It is not a pixel-for-pixel VS Code clone and does not add
a runtime or source dependency on Code - OSS.

## Decisions So Far

1. Keep Qt Widgets for the first modernization effort.
2. Preserve `QOpenGLWidget` map and UV views instead of moving rendering to a web view.
3. Do not add Rust or Electron solely to improve appearance.
4. Use VS Code's public theme token taxonomy and default theme JSON files as design
   references only.
5. Establish TrenchBroom-owned theme tokens as the single source of truth for
   `QPalette`, `QProxyStyle`, QSS, and editor overlay colors.
6. Defer custom native title bars and general-purpose drag-and-drop docking until the
   lower-risk visual work has been evaluated.

## Repository Findings

- `app/TrenchBroom/resources/stylesheets/base.qss` is currently small and handles only
  a limited set of widget-specific adjustments.
- `app/TrenchBroom/src/Main.cpp` applies the dark theme through the Qt Fusion style, a
  custom `QPalette`, and a narrow `QProxyStyle` override.
- `MapWindow` uses horizontal and vertical splitters to arrange the map view, bottom
  information panel, and right inspector.
- The application already has reusable UI building blocks including `TabBar`,
  `TitleBar`, `Splitter`, `ContainerBar`, and `CommandPaletteDialog`.
- The main rendering and editing path is already separated from most widget styling,
  so a theme and workbench-shell pass can avoid changes to map geometry and rendering.
- The inspector and outliner contain substantial custom behavior and painting. They
  should be modernized after global metrics and state colors are stable.

## Proposed Theme Tokens

The initial token set should remain deliberately small:

- window, editor, sidebar, panel, input, and elevated backgrounds
- primary, secondary, disabled, and inverse text
- subtle, normal, strong, and focus borders
- hover, pressed, selected, inactive-selected, and disabled states
- accent, error, warning, success, and information colors
- compact control, toolbar, tab, tree-row, and status-bar heights
- narrow, normal, and wide spacing values
- small and normal icon sizes

Qt style sheets do not support reusable variables. Theme values should therefore live
in a typed C++ structure or a validated data file, with QSS placeholders expanded from
that source rather than duplicating literal colors throughout the application.

## Work Plan

### Phase 0: Discovery

Status: complete

- [x] Inspect the application and library boundaries.
- [x] Measure the current Qt UI and theme surface.
- [x] Identify the map-window layout and existing reusable UI controls.
- [x] Compare Qt modernization with Electron, Rust, and Code - OSS reuse.
- [x] Confirm that VS Code source is useful as a reference but unnecessary as a
      dependency.

### Phase 1: Theme Foundation

Status: pending

- [ ] Add a TrenchBroom theme token model.
- [ ] Generate `QPalette` and QSS values from the same token source.
- [ ] Extend `QProxyStyle` for shared control metrics and focus behavior.
- [ ] Standardize hover, pressed, checked, selected, disabled, and focus-visible states.
- [ ] Standardize 16 px and 20 px monochrome icon usage.
- [ ] Add visual checks for light and dark themes at common DPI scale factors.

Expected benefit: high. This should remove most of the platform-default Qt appearance
without changing window structure.

### Phase 2: Main Workbench Shell

Status: pending

- [ ] Reduce toolbar height and visual noise.
- [ ] Move persistent grid and editing state into compact status-bar segments where
      appropriate.
- [ ] Restyle map-view tabs and contextual tool controls.
- [ ] Give the inspector and bottom panel consistent headers and panel borders.
- [ ] Evaluate a narrow activity bar for switching primary inspector modes.

Expected benefit: very high. This phase creates most of the recognizable compact
workbench layout while retaining the current splitter ownership model.

### Phase 3: Inspector And Outliner

Status: pending

- [ ] Standardize tree row height, indentation, selection, hover, and inactive states.
- [ ] Reserve stable columns for visibility, lock, group, and warning indicators.
- [ ] Add consistent panel search and local actions.
- [ ] Align property names and values into a predictable two-column layout.
- [ ] Introduce reusable collapsible section headers.
- [ ] Verify multi-selection, keyboard navigation, inline editing, and drag-and-drop.

Expected benefit: very high. Difficulty is medium to high because the outliner has
significant custom interaction and painting code.

### Phase 4: Supporting Surfaces

Status: pending

- [ ] Restyle Console, Issues, and Python panel tabs.
- [ ] Expand the status bar with concise selection, grid, snap, mode, and warning state.
- [ ] Restyle the existing command palette as a focused workbench overlay.
- [ ] Apply the shared design system to preferences and high-traffic dialogs.

Expected benefit: medium. These changes complete consistency after the main editing
surface has established the visual language.

### Phase 5: Optional Structural Work

Status: deferred

- [ ] Evaluate a custom native title bar.
- [ ] Evaluate document tabs across maps.
- [ ] Evaluate general-purpose movable and dockable panels.

These items have high platform and interaction risk. They should proceed only if user
testing shows that the earlier phases do not meet the product goal.

## Initial Acceptance Criteria

- Existing map editing, input, focus, shortcuts, drag-and-drop, and OpenGL rendering
  behavior remains unchanged.
- Light and dark themes have complete active, inactive, disabled, hover, pressed,
  selected, and focus states.
- Text and icons remain readable at 100%, 150%, and 200% display scaling.
- The main editor dedicates more vertical space to map views than the current toolbar
  and contextual-control arrangement.
- Repeated controls use consistent heights, padding, border treatment, and icon size.
- The UI remains usable on Windows, macOS, and Linux without platform-specific layout
  breakage.

## Validation Strategy

- Build the focused `TbUiLibTest` target before running UI tests.
- Run relevant focused Catch2 filters for every changed component.
- Build the Release `TrenchBroom` target before completing a workbench phase.
- Capture comparable light and dark screenshots on representative desktop sizes and
  DPI scale factors.
- Verify keyboard-only navigation, focus visibility, disabled states, and text fit.
- Keep generated screenshots and temporary visual reports out of commits unless they
  are intentionally selected as maintained references.

## External References

- VS Code theme color reference:
  <https://code.visualstudio.com/api/references/theme-color>
- VS Code Dark Modern theme:
  <https://github.com/microsoft/vscode/blob/main/extensions/theme-defaults/themes/dark_modern.json>
- VS Code 2026 Dark theme:
  <https://github.com/microsoft/vscode/blob/main/extensions/theme-defaults/themes/2026-dark.json>
- Qt style sheets:
  <https://doc.qt.io/qt-6/stylesheet.html>
- Qt `QProxyStyle`:
  <https://doc.qt.io/qt-6/qproxystyle.html>

## Progress Log

| Date | State | Change |
| --- | --- | --- |
| 2026-08-15 | Complete | Reviewed the current Qt, document, tool, and OpenGL boundaries. |
| 2026-08-15 | Complete | Chose incremental Qt modernization over an appearance-driven Electron/Rust rewrite. |
| 2026-08-15 | Complete | Defined the phased roadmap, initial token scope, and acceptance criteria. |
| 2026-08-15 | Next | Create a focused main-window visual specification and implement the theme-token foundation. |
