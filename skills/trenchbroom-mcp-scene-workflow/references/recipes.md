# Recipe Workflow Reference

Use recipe scripts for complex prefab-like scenes that would otherwise tempt
adding scene-specific C++ MCP tools. Recipes generate IR JSON only; they must
not call TrenchBroom, MCP, or `tb2` directly.

## Boundary

- Skill recipes own domain layout, repeated structure, parameter expansion, and
  IR emission.
- MCP C++ owns document guards, undo transactions, selector/module state,
  validation, and review.
- Prefab-like recipes must include a visual acceptance path in
  `recommendedValidation`. Static IR validation is not enough; real acceptance
  must inspect review output for scene readability and intent.
- Python `tb2` plugins are human-facing UI/plugin workflows. If UI plugins need
  prefab logic later, reuse recipe scripts to emit IR instead of duplicating
  generation rules.

## Commands

List available recipes and recommended validation paths:

```powershell
python <skill>\scripts\list_recipes.py
python <skill>\scripts\list_recipes.py --json
python <skill>\scripts\list_recipes.py --recipe ascending_loop
```

Describe a recipe without generating IR:

```powershell
python <skill>\scripts\recipes\ascending_loop.py --describe
```

Validate params and generated IR without writing a file:

```powershell
python <skill>\scripts\recipes\ascending_loop.py --params <params.json> --validate-only
```

Generate IR:

```powershell
python <skill>\scripts\recipes\ascending_loop.py --params <params.json> --out <ir.json>
```

Validate all bundled examples and optionally emit IR files:

```powershell
python <skill>\scripts\validate_recipes.py --out-dir <tmp-ir-dir> --report <report.md>
```

Example params are grouped as:

- `scripts/examples/<recipe>/minimal.json`: fast smoke test.
- `scripts/examples/<recipe>/default.json`: normal generation path.
- `scripts/examples/<recipe>/stress.json`: larger regression path.

Legacy files directly under `scripts/examples/` remain compatible, but prefer the
grouped examples for new work.

## MCP Apply Flow

After generating IR, use:

1. `ir_compile_preview_from_file`
2. `ir_apply_from_file` with the returned `previewId` when available. Fall back
   to `path` only when the preview cache is unavailable or expired.
3. `module_inspect` or `selector_preview`
4. `geometry_analyze_slopes` / `geometry_analyze_route_continuity` when route-like
5. `map_validate(groupByType:true)`
6. `render_review_selector` or `module_render_review`

Use `idsMode:"count"` or `"sample"` unless full object ids are necessary.

If the flow fails, classify the failure before changing MCP code:

- recipe parameter problem: fix params or recipe defaults
- IR expression problem: fix recipe output or decompose into existing operations
- missing generic primitive/validator/review support: report as MCP feedback
- map/document/session problem: refresh status, reopen/verify, or retry safely
- validation/review problem: rebuild or iterate the recipe; do not claim success
  from screenshots alone

## Recipe Notes

Current bundled recipe coverage is intentionally narrow. Earlier house, temple,
KZ/bhop, cave, and terrain recipe drafts were removed after human visual
acceptance failed; rebuild them only as new recipes with inspected review output.

- `ascending_loop`: emits an `arc_ramp` operation plus rails, supports, markers,
  spawn, and light. `arc_ramp` expands into `arc_ramp_segment` operations whose
  selectable part is `ramp`; use `selector:{moduleId, part:"ramp"}` for slope
  analysis. A rising 360-degree route is a spiral, not a closed same-height loop;
  pass `closedLoop:true` only when the intended start/end seam is actually
  connected. Circular geometry can produce non-integer vertex warnings; treat
  them as grid cleanliness warnings only when slope, continuity, and review pass.

## IR Shape

Use a minimal JSON wrapper around existing atomic MCP operations:

```json
{
  "name": "MCP: Apply scene module",
  "moduleId": "route-a",
  "defaultMetadata": {
    "moduleId": "route-a",
    "generatedBy": "trenchbroom-mcp-scene-workflow"
  },
  "grid": 16,
  "material": "__TB_empty",
  "operations": [
    {
      "type": "path_ribbon",
      "points3d": [[0, 0, 64], [256, 0, 96]],
      "width": 128,
      "thickness": 16,
      "metadata": {"part": "road", "role": "walkable", "routeId": "main", "order": 1}
    }
  ],
  "entities": [
    {
      "classname": "light",
      "origin": [128, 0, 192],
      "properties": {"_light": "255 240 220 200"}
    }
  ]
}
```

Keep IR small and explicit. Do not invent scene operations unless MCP already
exposes them. If an intent cannot be expressed atomically, decompose it into
generic `blockout_create_batch`, `brush_create_polygon_batch`, entity, texture,
selector, and metadata operations.
