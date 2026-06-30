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
Generate IR to a local file, run `ir_compile_preview_from_file`, apply it with
`ir_apply_from_file`, then recover targets by module/selector. Validate with
`geometry_analyze_slopes` and
`geometry_analyze_route_continuity(routeMode:"spiral", orderBy:"metadataOrder")`.
Review with a selector or module contact sheet capped to two panels. Record
`tb_status`, operation ids, module/selector counts, validation summaries,
preferred review path, visual judgment, and friction. Do not edit source or
commit.

## Prompt: Temple Courtyard Whitebox

Build a disposable TrenchBroom MCP scene for a temple courtyard whitebox with a
main hall, raised base, stairs, columns, entrance axis, simple lights, and spawn.
Use `$trenchbroom-mcp-scene-workflow` and the `temple_courtyard` recipe if
useful. Generate IR to a local file, preview/apply from file, recover by
module/selector, run `operation_validate`, `module_inspect`,
`selector_preview`, `map_validate(groupByType:true)`, and visual review. Report
whether repeated parts are recoverable without long object id lists and whether
the review image reads as a courtyard. Do not edit source or commit.

## Prompt: KZ Bhop/Slide Route

Build a disposable TrenchBroom MCP scene for a CS1.6 KZ-style bhop/slide route.
Use `$trenchbroom-mcp-scene-workflow` plus `$cs16-kz-trenchbroom` for gameplay
interpretation. Use the `kz_bhop_route` recipe if useful. Generate IR to file,
preview/apply from file, recover targets by `routeId/order/moduleId`, run
`geometry_analyze_slopes` for slide/surf parts and
`geometry_analyze_route_continuity(routeMode:"jump_chain", orderBy:"metadataOrder")`.
Review with a two-panel contact sheet. MCP should report geometry facts; the
skill/Agent should explain intended difficulty and route feel. Do not edit
source or commit.

## Required Report Shape

- Scenario name, map path, time, process id, bridgeInstanceId, documentFingerprint.
- Tools used and approximate MCP call count.
- Operation ids and module ids, using counts/samples rather than long ids.
- Validation results: operation/module/map, route continuity, slopes when applicable.
- Review paths, especially preferred contact sheet path.
- Visual verdict: what is readable, what is ambiguous, what needs detail images.
- Friction list with P0/P1/P2/P3 severity and owner: MCP, skill, recipe, or Agent.
- Next recommended fix, if any, phrased as generic MCP capability or recipe/skill workflow change.
