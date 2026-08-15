# Qt UI Modernization Progress

## Status

- Branch: `codex/qt-ui-modernization`
- Baseline commit: `4d3a1dca7` (`Restore shortcut preference semantics`)
- Last updated: 2026-08-15
- Current stage: Phase 2 main workbench shell complete; Phase 3 ready to start

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
7. Follow Code - OSS Modern UI's 4 px card gaps, 6 px compact-control radius, and 8 px
   workbench-surface radius without copying its DOM or adding a source dependency.
8. Keep inspector modes as labeled horizontal segmented navigation for now. A narrow
   icon-only activity rail would reduce clarity with the current icon set and remains an
   optional structural change.

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

Status: complete

- [x] Add a TrenchBroom theme token model.
- [x] Generate `QPalette` and QSS values from the same token source.
- [x] Extend `QProxyStyle` for shared control metrics and focus behavior.
- [x] Standardize hover, pressed, checked, selected, disabled, and focus-visible states.
- [x] Standardize 16 px and 20 px icon metrics.
- [x] Add deterministic visual checks for light and dark themes at 100%, 150%, and
      200% scale factors.

Expected benefit: high. This should remove most of the platform-default Qt appearance
without changing window structure.

### Phase 2: Main Workbench Shell

Status: complete

- [x] Reduce toolbar height and visual noise.
- [x] Move persistent grid and editing state into compact status-bar segments where
      appropriate.
- [x] Restyle map-view tabs and contextual tool controls.
- [x] Give the editor, inspector, and bottom panel consistent card borders, gaps, and
      rounded corners.
- [x] Evaluate a narrow activity bar and retain labeled segmented inspector navigation
      for this phase.

Expected benefit: very high. This phase creates most of the recognizable compact
workbench layout while retaining the current splitter ownership model.

### Phase 3: Inspector And Outliner

Status: in progress (first visual pass complete)

- [x] Standardize tree row height, indentation, rounded selection, hover, and inactive
      states.
- [x] Reserve stable 28 px columns for visibility and lock while retaining node type
      and group indicators in the name hierarchy.
- [x] Add a consistent inset search, sort, and properties action row.
- [x] Align property names, values, and actions into a predictable responsive layout.
- [ ] Introduce reusable collapsible section headers.
- [x] Verify multi-selection, Escape handling, and lock/visibility hit targets.
- [ ] Add warning indicators when the outliner has a stable warning data source.
- [ ] Complete manual keyboard navigation, inline editing, and drag-and-drop acceptance.

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
- Run `powershell -ExecutionPolicy Bypass -File scripts\ui-theme-acceptance.ps1` to
  capture the isolated welcome window, representative map workbench, and deterministic
  expanded Outliner/entity-properties state in light and dark themes at 100%, 150%,
  and 200% scale. The script validates image dimensions, device-pixel ratio, nonblank
  pixels, font support, and image hashes, then writes PNG and JSON evidence plus a
  labeled visual-comparison contact sheet under
  `build-release-codex\codex-logs\ui-theme-acceptance`.
- Use `--ui-snapshot <path> --ui-snapshot-theme system|light|dark
  [--ui-snapshot-page map|outliner] [map-file]` for a focused single capture. Omitting
  the map captures Welcome; supplying one captures the workbench. The Outliner page
  expands its hierarchy, properties panel, and active selection before rendering.
  Snapshot mode uses temporary preferences, disables background services, and renders
  through the native Qt platform without showing the window.
- Verify keyboard-only navigation, focus visibility, disabled states, and text fit.
- Keep generated screenshots and temporary visual reports out of commits unless they
  are intentionally selected as maintained references.

## External References

- Code - OSS Modern UI floating panels (pinned reference):
  <https://github.com/microsoft/vscode/blob/6c27443ce6fdf6ac798c64025d45175e2e23c4b4/src/vs/workbench/browser/media/floatingPanels.css>
- Code - OSS corner-radius sizes (pinned reference):
  <https://github.com/microsoft/vscode/blob/6c27443ce6fdf6ac798c64025d45175e2e23c4b4/src/vs/platform/theme/common/sizes/baseSizes.ts>
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
| 2026-08-15 | Complete | Added the theme-token foundation, palette generation, QSS expansion, and compact shared control styling. |
| 2026-08-15 | Verified | Passed the Release `TbUiLibTest "Theme"` test, built `TrenchBroom`, and completed a warning-free current-theme startup and screenshot check. |
| 2026-08-15 | Complete | Added first-class light theme tokens and deterministic in-process UI snapshot capture with PNG, JSON, and a labeled comparison contact sheet. |
| 2026-08-15 | Verified | Passed `UiSnapshot` and `Theme` tests, built Release `TrenchBroom`, and passed the hidden Light/Dark acceptance matrix at 100%, 150%, and 200% scale with readable native fonts. |
| 2026-08-15 | Complete | Pinned Code - OSS reference commit `6c27443c` and adapted its 4/6/8 px spacing and corner-radius hierarchy to Qt. |
| 2026-08-15 | Complete | Added rounded editor, inspector, and bottom-panel surfaces; compact segmented tabs; an inspector header; and a quieter toolbar with Grid moved to the status bar. |
| 2026-08-15 | Complete | Extended deterministic acceptance to capture a real map workbench as well as Welcome without showing native windows. |
| 2026-08-15 | Verified | Passed `MapWindow`, `Theme`, and `UiSnapshot` tests, built Release `TrenchBroom`, and passed the 12-state Welcome/Workbench Light/Dark DPI matrix. |
| 2026-08-15 | Complete | Added an inset Outliner toolbar, icon-only state columns, compact hierarchy metrics, rounded full-row interaction states, and a quieter responsive entity-property layout. |
| 2026-08-15 | Complete | Added deterministic Outliner snapshot setup with expanded hierarchy, visible properties, and active selection. |
| 2026-08-15 | Verified | Passed `OutlinerTreeWidget`, `OutlinerEntityPropertyEditor`, `MapWindow`, `Theme`, and `UiSnapshot` tests, built Release `TrenchBroom`, and passed the 18-state Welcome/Workbench/Outliner Light/Dark DPI matrix. |
| 2026-08-15 | Next | Finish Phase 3 with reusable collapsible section styling and manual inline-editing and drag-and-drop acceptance. |
