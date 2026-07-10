# Recipe Workflow Reference

Use recipe scripts for complex prefab-like scenes that would otherwise tempt
adding scene-specific C++ MCP tools. Recipes generate IR JSON only; they must
not call TrenchBroom, MCP, or `tb2` directly.

## Boundary

- Skill recipes own domain layout, repeated structure, parameter expansion, and
  IR emission.
- MCP C++ owns document guards, undo transactions, selector/module state,
  validation, and review.
- Every recipe declares `qualityPolicy` and
  `reviewPolicy:{recommended:true,required:false}`. The manifest must include a
  review path, but review is optional evidence and is not a static validation gate.
- Python `tb2` plugins are human-facing UI/plugin workflows. If UI plugins need
  prefab logic later, reuse recipe scripts to emit IR instead of duplicating
  generation rules.

## Commands

List available recipes and recommended validation paths:

```powershell
python <skill>\scripts\list_recipes.py
python <skill>\scripts\list_recipes.py --json
python <skill>\scripts\list_recipes.py --recipe rect_shell
```

Describe a recipe without generating IR:

```powershell
python <skill>\scripts\recipes\<recipe>.py --describe
```

Validate params and generated IR without writing a file:

```powershell
python <skill>\scripts\recipes\<recipe>.py --params <params.json> --validate-only
```

Generate IR:

```powershell
python <skill>\scripts\recipes\<recipe>.py --params <params.json> --out <ir.json>
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

Before generating IR, record `tb_status` document guards and an unmodified
`problems_check` baseline. Then use:

1. Select `draft`, `balanced`, or `smooth` from user intent; default to `balanced`.
2. Generate IR with `schemaVersion:1` and `qualityPolicy`.
3. Preview with `applyMode:"create"` for new modules or
   `applyMode:"replace_module"` for iteration.
4. Apply with the returned `previewId`. Replacement must retain the preview's IR
   hash, module revision/content hash, and canonical live object-set guards. If
   the preview, file, or module changes, preview again.
5. Recover with `module_inspect` or `selector_preview` and record module
   revision/content hash.
6. Run `geometry_analyze_slopes` and/or
   `geometry_analyze_route_continuity` when route-like. Use
   `walkableContinuous`, `qualityStatus`, `acceptancePassed`, and `notEvaluated`;
   legacy `passed` is not the acceptance verdict.
7. Run `map_validate(groupByType:true)` and `problems_check`. Compare stable
   problem ids when both responses are untruncated; otherwise compare grouped
   counts and label the delta low-confidence.
8. Optionally render construction evidence with `edgeMode:"all"` and outline
   evidence with `edgeMode:"silhouette"`.

Quality warnings do not block `draft` or `balanced`. Only an explicitly selected
`smooth` policy turns a quality overrun into failed acceptance. Review never
changes `acceptancePassed`; report it as `not_run` when skipped.

Use `idsMode:"count"` or `"sample"` unless full object ids are necessary.

If the flow fails, classify the failure before changing MCP code:

- recipe parameter problem: fix params or recipe defaults
- IR expression problem: fix recipe output or decompose into existing operations
- missing generic primitive/validator/review support: report as MCP feedback
- map/document/session problem: refresh status, reopen/verify, or retry safely
- validation problem: rebuild or iterate the recipe
- review concern: report the visual evidence separately from static acceptance

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
  them as grid cleanliness warnings only when slope and acceptance checks pass.
  The grouped examples intentionally use `draft` for minimal, `balanced` for
  default (36 segments), and `smooth` for stress.
- `rect_shell`: replaces legacy `room`, `corridor`, and `sky_shell` batch types
  with explicit box IR. Use `kind` only for recipe defaults and metadata.
- `opening_wall`: replaces legacy `doorway` batch type. It creates a freestanding
  segmented wall, not a cut into existing brushes.
- `cover_block`: replaces legacy `cover` batch type with one explicit box.
- `stair_run`: replaces legacy straight `stairs` batch type with ordered box
  steps; validate as route mode `stairs_or_steps` when used as a route.

## IR Shape

Use a minimal JSON wrapper around existing atomic MCP operations:

```json
{
  "schemaVersion": 1,
  "name": "MCP: Apply scene module",
  "moduleId": "route-a",
  "defaultMetadata": {
    "moduleId": "route-a",
    "generatedBy": "trenchbroom-mcp-scene-workflow"
  },
  "grid": 16,
  "material": "__TB_empty",
  "qualityPolicy": {"intent": "balanced"},
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

New recipes must emit integer `schemaVersion:1`. Unversioned IR is accepted only
for legacy compatibility and returns `legacyUnversionedIr`; future or invalid
versions are rejected before mutation.

## Manifest Policies

Use a fixed policy when a recipe has no user-selectable quality mode:

```json
{
  "qualityPolicy": {"intent": "balanced"},
  "reviewPolicy": {"recommended": true, "required": false}
}
```

For a selectable route policy, declare the parameter mapping instead:

```json
{
  "qualityPolicy": {
    "intentParam": "qualityIntent",
    "defaultIntent": "balanced"
  },
  "reviewPolicy": {"recommended": true, "required": false}
}
```
