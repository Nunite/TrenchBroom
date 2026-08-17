# TrenchBroom manual documentation

The offline manual is built from Markdown with Pandoc and opened in the system browser. It has no Node, online, Qt WebEngine, or runtime package dependency.

## Source layout

English is the canonical source. The `en` and `zh_CN` directories contain the same eight chapter filenames in the same order:

1. `00-introduction.md`
2. `01-getting-started.md`
3. `02-selection-editing.md`
4. `03-materials-assets.md`
5. `04-entities-outliner-layers.md`
6. `05-preferences-extension.md`
7. `06-advanced-topics.md`
8. `07-involvement-references.md`

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

## Custom macros

The build generates `shortcuts.en.js` and `shortcuts.zh_CN.js` from the application action registry. Action keys and preference paths remain stable English identifiers; only final menu labels are translated.

- `#action(Controls/Map view/Duplicate and move objects)` prints the current shortcut for an action preference path.
- `#menu(Menu/Edit/Show All)` prints the translated menu path and its current shortcut.
- `#key(Return)` prints a localized key label.

Never translate macro arguments. `TransformKeyboardShortcuts.cmake` converts these macros into calls implemented by `manual.js`.
