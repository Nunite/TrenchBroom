# TrenchBroom Themes

TrenchBroom themes are single UTF-8 JSON files with the `.tbtheme` extension. A theme
can inherit another installed theme and override only the colors it needs. Theme files
cannot inject Qt stylesheets, change widget geometry, or load executable code.

## Installing a theme

Place the `.tbtheme` file in the `themes` directory inside the TrenchBroom user data
directory, then restart TrenchBroom and select it under Preferences > View > Theme.

- Windows: `%APPDATA%\TrenchBroom\themes`
- macOS: `~/Library/Application Support/TrenchBroom/themes`
- Linux and FreeBSD: `~/.TrenchBroom/themes`
- Portable mode: `<TrenchBroom directory>/config/themes`

The theme ID, rather than its display name, is stored in preferences. This allows a
theme author to rename a theme without resetting existing selections. If a selected
theme is temporarily unavailable, TrenchBroom uses the System theme without discarding
the missing theme's ID.

## File format

```json
{
  "schemaVersion": 1,
  "id": "example.midnight",
  "name": "Midnight",
  "author": "Example Author",
  "appearance": "dark",
  "inherits": "builtin.dark",
  "colors": {
    "accent": "#4f9bd8",
    "focusBorder": "#4f9bd8",
    "selectionBackground": "#24577a"
  }
}
```

`schemaVersion`, `id`, `name`, `appearance`, and `colors` are required. `author` and
`inherits` are optional. IDs must contain only lowercase letters, digits, dots, dashes,
and underscores; the `builtin.` namespace is reserved. Colors use `#RRGGBB` notation.
A theme without `inherits` must define every color token. Third-party themes must use
`light` or `dark` appearance; the `system` appearance is reserved for `builtin.system`
because it derives its values from the operating system palette.

The built-in IDs are:

- `builtin.system`
- `builtin.light`
- `builtin.dark`
- `builtin.blender`

The snapshot CLI accepts stable IDs as well as the compatibility aliases `system`,
`light`, `dark`, and `blender`:

```text
TrenchBroom --ui-snapshot output.png --ui-snapshot-theme example.midnight
```

A ready-to-install example is available at
[`docs/examples/midnight.tbtheme`](examples/midnight.tbtheme).

## Color tokens

| Token | Purpose |
| --- | --- |
| `windowBackground` | Main window and dialog background |
| `editorBackground` | Primary editor and browser canvas |
| `sidebarBackground` | Navigation and inspector sidebars |
| `panelBackground` | Tool and inspector panels |
| `elevatedBackground` | Menus, popups, and raised surfaces |
| `inputBackground` | Text fields and compact inputs |
| `alternateBackground` | Alternating rows and grouped browser cells |
| `buttonBackground` | Resting button surface |
| `hoverBackground` | Hovered interactive surface |
| `pressedBackground` | Pressed interactive surface |
| `selectionBackground` | Active selection |
| `inactiveSelectionBackground` | Selection without active focus |
| `border` | Subtle separators and control edges |
| `strongBorder` | High-contrast separators and control edges |
| `focusBorder` | Keyboard focus ring |
| `text` | Primary text |
| `secondaryText` | Secondary labels and metadata |
| `disabledText` | Disabled text and indicators |
| `inverseText` | Text drawn over selection or accent colors |
| `accent` | Links and active accents |
| `error` | Error state |
| `warning` | Warning state |
| `success` | Success state |

Files are loaded in filename order after the embedded built-in themes. Built-in IDs
cannot be replaced. The first third-party file for a duplicate ID wins. Invalid files,
unknown fields or tokens, missing parents, and inheritance cycles are skipped and
reported in the TrenchBroom log.

The Blender palette is adapted from Blender 5.2's default UI theme at commit
`fbe6228777e7d9afefcd61a413844e790ae75db7`.
