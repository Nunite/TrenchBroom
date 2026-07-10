# Codex CLI Regression Prompts

Use these prompts for disposable, serial MCP regression workers. Each worker must
use TrenchBroom MCP only, must not edit source files, and must write a concise
report to the `-o` path supplied by the caller.

Command template:

```powershell
codex exec --profile tb-mcp-flow --ephemeral --sandbox read-only --cd <repo> -o <report.md> "<prompt>"
```

Before dispatching, start the current Release TrenchBroom build and open a
disposable `.map`. Stop the sequence on any P0: crash, wrong-map write, data
loss, or unrecoverable dirty document.

## Prompt: Ascending Spiral Route

Build a disposable TrenchBroom MCP scene for an ascending spiral road route.
Use `$trenchbroom-mcp-scene-workflow` and the `ascending_loop` recipe if useful.
Record `tb_status` and a `problems_check` baseline. Choose and record a quality
intent, defaulting to `balanced`. Generate IR to a local file, run
`ir_compile_preview_from_file` with `applyMode:"create"`, apply it with
`ir_apply_from_file`, then recover targets by module/selector. Iterate an existing
module only through guarded `replace_module`. Validate with
`geometry_analyze_slopes` and
`geometry_analyze_route_continuity(routeMode:"spiral", orderBy:"metadataOrder")`.
Optionally review with `edgeMode:"all"` and `edgeMode:"silhouette"`, capped to two
panels. Record acceptance fields, module revision/hash, problem delta, save,
review, BSP status, and friction. Do not edit source or commit.

## Required Report Shape

- Scenario name, map path, time, process id, bridgeInstanceId, documentFingerprint.
- Tools used and approximate MCP call count.
- `undoOperationId`, audit operation ids, and module ids, using counts/samples rather than long ids.
- Module revision/content hash and canonical/live counts.
- Validation results: `walkableContinuous`, `qualityStatus`,
  `acceptancePassed`, `notEvaluated`, operation/module/map, and slopes when applicable.
- Problem delta as introduced/resolved/pre-existing ids, or grouped low-confidence
  counts when truncated.
- Optional construction/silhouette review paths. If skipped, state `visualReview:not_run`.
- Visual verdict only when review was actually inspected.
- Save-required and BSP-compile status; do not imply either was completed.
- Friction list with P0/P1/P2/P3 severity and owner: MCP, skill, recipe, or Agent.
- Next recommended fix, if any, phrased as generic MCP capability or recipe/skill workflow change.
