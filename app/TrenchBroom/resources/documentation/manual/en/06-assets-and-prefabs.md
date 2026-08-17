# Assets and Prefabs {#assets_and_prefabs}

## Unified Asset Browser {#asset_browser}

The **Assets** tab in the bottom info panel provides one browser for game assets and reusable prefabs. For enabled GoldSrc mods it presents the `models`, `sprites`, and `sound` roots together with a user `prefabs` root. The browser recognizes `.mdl`, `.spr`, `.wav`, and `.tbprefab` files.

Use the folder tree or breadcrumb row to navigate, and type in the path row when you need to enter a folder directly. The search field filters the current asset set. The thumbnail menu supports 50% through 300% sizes, and the reload button rescans enabled game and mod paths. Changes in watched asset directories are picked up automatically.

Model cells render their model preview, sprite cells render the first supported sprite frame, and selected sound cells expose a play/stop control when the platform can decode the file. A placeholder and the asset tooltip still identify files whose preview cannot be rendered.

Drag a model or sprite into a viewport to create an entity using that asset. Drag a sound to assign or create the appropriate sound reference where the current game and editor context support it. Drag a prefab to place its stored map objects.

### Reusable Prefabs {#reusable_prefabs}

Select map objects and click **Save selection as prefab** in the Assets toolbar to create a `.tbprefab` file and thumbnail. The prefab directory is configured under **Preferences > Misc > Tools**. Prefabs appear under the `prefabs` root and can be dragged into the map; right-click a prefab to rename or delete it.

Prefab files preserve the selected map text and relevant material collection paths. They are intended for reusable map fragments. Editing a placed copy does not modify the source prefab or other placed copies.
