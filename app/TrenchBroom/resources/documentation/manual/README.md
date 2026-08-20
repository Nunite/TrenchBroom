# TrenchBroom manual documentation

The offline manual is built from Markdown with Pandoc and opened in the system browser. It has no Node, online, Qt WebEngine, or runtime package dependency.

## Source layout

English is the canonical source. The `en` and `zh_CN` directories contain the same 13 manual chapters in the same order, plus a separate Python API reference:

1. `00-introduction.md`
2. `01-getting-started.md`
3. `02-interface-and-tools.md`
4. `03-brush-editing.md`
5. `04-vertex-and-csg.md`
6. `05-materials-and-uv.md`
7. `06-assets-and-prefabs.md`
8. `07-entities-and-organization.md`
9. `08-preferences-and-compilation.md`
10. `09-python-plugins-guide.md`
11. `10-mcp-automation.md`
12. `11-game-config-and-expressions.md`
13. `12-references.md`

The standalone `python-api.md` source is generated as the Python API tab alongside the manual.

Every heading must have an explicit, language-independent `{#anchor}`. Translate visible text only. Keep anchors, macro arguments, code, paths, image targets, link targets, and inline code unchanged. Use `terminology.tsv` for the frozen core vocabulary.

Shared presentation files are `template.html`, `manual.css`, and `manual.js`. Images are stored once under `images`. Language-specific labels and relative resource paths are defined in each language's `metadata.yaml`.

## Build and validation

Build the focused targets from an existing configured build tree:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target GenerateManual
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target ValidateManual
```

The generated entry points are:

- `<build>/app/TrenchBroom/gen-manual/index.html`
- `<build>/app/TrenchBroom/gen-manual/zh_CN/index.html`

`ValidateManual` checks mirrored heading structure and anchors, stable macros and tokens, unchanged code blocks and targets, local files and fragments, generated HTML IDs and language metadata, unresolved placeholders, and translation freshness.

`translation-status.json` records the English SHA-256 used by each Chinese chapter. After updating an English chapter and its Chinese translation, refresh the fingerprints and rerun validation:

```powershell
python app/TrenchBroom/resources/documentation/manual/validate_manual.py `
  --manual-root app/TrenchBroom/resources/documentation/manual `
  --update-fingerprints
```

Do not update a fingerprint until the corresponding Chinese chapter is current.

## UI translations used by the manual

Localized `#menu(...)` output is generated from the tracked
`resources/translations/trenchbroom_zh_CN.qm` file. After changing
`trenchbroom_zh_CN.ts`, rebuild the `.qm` file with Qt Linguist before generating the manual:

```powershell
lrelease app/TrenchBroom/resources/translations/trenchbroom_zh_CN.ts `
  -qm app/TrenchBroom/resources/translations/trenchbroom_zh_CN.qm
```

`GenerateManual` depends on the compiled `.qm` file so that the shortcuts script and the
application use the same menu labels.

## Custom macros

The build generates `shortcuts.en.js` and `shortcuts.zh_CN.js` from the application action registry. Action keys and preference paths remain stable English identifiers; only final menu labels are translated.

- `#action(Controls/Map view/Duplicate and move objects)` prints the current shortcut for an action preference path.
- `#menu(Menu/Edit/Show All)` prints the translated menu path and its current shortcut.
- `#key(Return)` prints a localized key label.

Never translate macro arguments. `TransformKeyboardShortcuts.cmake` converts these macros into calls implemented by `manual.js`.
