# MCP Long-Term Refactor Blueprint

This document describes the target shape for a long-term TrenchBroom MCP
refactor. It is a design roadmap, not an implementation plan for one patch.

The reference model is the official Blender Lab MCP design: a small bridge, a
bounded tool surface, compact discovery, and strong tests around the exposed
catalog. TrenchBroom should keep a stricter boundary than Blender because map
mutation needs document guards, undo transactions, live object identity, and
geometry validation.

`docs/mcp-development-governance.md` remains the normative rule set. If this
blueprint conflicts with it, follow the governance document first.

## Target Shape

The desired architecture is:

```text
Agent / Skill
  owns scene intent, recipe choice, visual judgement, workflow routing

Recipe Scripts
  own prefab-like composition and deterministic IR generation

IR
  owns large operation transport and preview/apply boundary

C++ MCP Kernel
  owns guarded editor execution, facts, recovery, validation, review

TrenchBroom Document
  owns native map state, selection, undo, rendering, and persistence
```

C++ MCP should feel less like a scene generator and more like a narrow editor
kernel with reliable facts and safe mutations.

## Execution Priority

Use this order when choosing follow-up work:

```text
delete exposed surface > compact outputs > improve discovery > add kernel primitive
```

Start by removing, hiding, or consolidating confusing tool surface. Then make
remaining diagnostics and searches smaller by default. Then improve skill,
status, doctor, search, and schema guidance. Add new C++ code only when the
missing capability is a reusable editor-kernel primitive that needs
TrenchBroom internals.

## Design Principles

### 1. Keep The Kernel Small

MCP tools should exist only for editor-kernel responsibilities:

- active document identity, document path, and fingerprint checks
- undoable transactions and operation history
- live object identity, selection, selectors, modules, and stale recovery
- atomic brush, entity, texture, face, transform, and CSG mutations
- map validation, geometry facts, slope/route continuity, and problem checks
- review rendering, screenshots, manifests, and resource paths
- IR preview/apply and compact diagnostics

Do not add C++ tools for scene families, building styles, route genres, or
gameplay concepts. Those belong in skill recipes.

### 2. Make Discovery Better Before Adding Tools

If the Agent cannot find the right path, prefer:

- better skill description/frontmatter
- better `initialize`, `tb_status`, or `tb_doctor` hints
- better `tb_tools_search` behavior
- better local docs or searchable guidance
- better schema wording for existing tools

Do not add alias tools or convenience wrappers just to make a workflow easier to
remember.

### 3. Tool Catalog Is A Contract

The visible MCP catalog should be treated as API surface:

- `Core`, `Modeling`, and `Full` profile membership is intentional
- hidden/searchable tools remain discoverable by exact name
- schema changes need focused catalog tests
- default-visible Modeling growth needs justification
- deleted tools need tests proving they are gone

Catalog drift should fail tests, not surprise Agents.

### 4. Compact Output By Default

High-volume tools should return summaries first:

- counts, bounds, samples, warnings, and paths
- `idsMode:"count"` or `"sample"` by default
- `detail:"summary"` by default
- full ids, surfaces, seams, stale records, and schemas only on request

Large payloads should go to resource files or manifests rather than the chat
context.

### 5. Visual Review Is Evidence, Not Truth

Screenshots and review renders help judge readability, but geometry claims must
be validated by tools:

- use `map_validate` and `problems_check` for map health
- use `operation_validate` and `module_validate` for generated ownership
- use slope and route continuity tools for ramps, stairs, routes, surf, slide,
  and terrain
- keep review labels bounded and contact sheets readable

### 6. No Arbitrary Execution Escape Hatch

Do not add generic tools such as `execute_trenchbroom_code`, `run_cpp`, or
`run_tb_script`. If a future debug escape hatch is unavoidable, it must be:

- hidden from Modeling
- expert-only in Full
- document-guarded
- audited in operation/history output
- unable to bypass undo and document safety

The preferred escape hatch is still a new generic primitive, validator, selector
operation, review feature, or skill recipe.

## Refactor Phases

## Implementation Status

Use this checklist as the execution tracker. Evidence must come from current
source, tests, committed changes, or real TB smoke output; intent alone does not
count.

### Phase 1 Status: Done

- Done: `Core / Modeling / Full` are the only advertised profiles; legacy
  `Balanced` config input migrates to `Modeling`.
- Done: direct prefab placeholders and legacy direct blockout helper tool names
  are removed from the catalog, with legacy payload support kept in batch/IR.
- Done: `tb_doctor` and broad `tb_tools_search(detail:"schema")` use compact
  output, while exact-name schema lookup remains available.
- Done: `initialize`, `tb_status`, and `tb_doctor` expose the associated
  `trenchbroom-mcp-scene-workflow` skill hint.
- Done: catalog and bridge tests cover profile parsing, deleted tool names,
  compact doctor/search behavior, and skill hints.
- Done: `textures_list` is hidden from Modeling as duplicate material discovery
  surface; `texture_search` remains the default visible material lookup and
  exact search still finds `textures_list`.
- Done: the remaining Modeling-visible surface is covered by focused catalog
  tests and matches the skill's normal Agent workbench: status/open, IR
  preview/apply, selector/module/operation recovery, history recovery, common
  batch geometry, selection-aware CSG, validation, review, checked entities,
  texture search/apply, grouping, map validation, and problem checks.

Next Phase 1 item:

- Phase 1 is closed. Continue with Phase 2.

### Phase 2 Status: Done

- Done: the shared `idsMode` wording for high-frequency safe batch modeling
  helpers is centralized in the catalog and covered by a drift test.
- Done: the shared `moduleId` wording used by selector and module tools is
  centralized in the catalog and covered by a drift test.
- Done: shared live operation target wording for recovery-oriented tools is
  centralized in the catalog and covered by a drift test.
- Done: common compact-output `detail` wording is centralized in the catalog and
  covered by a drift test.
- Done: selector metadata field wording is centralized in the catalog and
  covered by a drift test.
- Done: lifecycle/category metadata values are centralized near the catalog
  edit path and covered by a focused catalog test.
- Done: catalog metadata tests prove common selector/module wording does not
  drift for the centralized fragments above.

Next Phase 2 item:

- Phase 2 is closed. Continue with Phase 3.

### Phase 3 Status: Partial

- Done: operation records carry document path/fingerprint and undo/redo guards
  reject wrong-document history operations before mutation.
- Done: `history_undo_to_operation` exists with structured partial-failure
  diagnostics.
- Done: stale target warnings are summarized by default in selector/module
  paths, with full detail opt-in.
- Done: compound operation expansion summaries exist for batch/curved corridor
  inspection paths.
- Done: `objects_delete_by_selector` reports explicit `mutatedDocument`,
  `retrySafe`, and `recoveryAction` diagnostics for no-match, selector error,
  and forbidden pre-mutation failure paths.
- Done: object delete tools report document identity on successful operation
  records and structured `mutatedDocument:false` / `retrySafe` /
  `recoveryAction` diagnostics for pre-mutation target failures.
- Done: entity property update/delete tools report document identity on
  successful operation records and structured `mutatedDocument:false` /
  `retrySafe` / `recoveryAction` diagnostics for pre-mutation target failures.
- Done: `texture_apply_by_filter` reports document identity on successful
  operation records and structured `mutatedDocument:false` / `retrySafe` /
  `recoveryAction` diagnostics for pre-mutation target failures.
- Done: `texture_apply` reports structured `mutatedDocument:false`,
  `retrySafe`, and recovery actions for pre-mutation material and face-target
  failures.
- Done: `texture_replace` reports structured `mutatedDocument:false`,
  `retrySafe`, and recovery actions for pre-mutation material, scope, and target
  failures.
- Done: `geometry_csg_selection` reports document identity on successful
  operation records and structured `mutatedDocument:false` / `retrySafe` /
  `recoveryAction` diagnostics for pre-mutation selection failures.
- Done: shared brush/batch mutation records from `McpBrushTools` report
  `mutatedDocument:true` and document identity in result summaries and history
  records.
- Done: asset placement mutation records report `mutatedDocument:true` and
  document identity in result summaries and history records.
- Done: `objects_delete_by_selector` delete records report
  `mutatedDocument:true` and document identity in result summaries and history
  records.
- Done: `objects_transform` precondition failures report
  `mutatedDocument:false`, `retrySafe`, and recovery actions before transforming
  objects.
- Done: safe problem-fix mutation records report `mutatedDocument:true` and
  document identity in result summaries and history records.
- Done: invalid `brush_create_polygon_batch` preflight results report
  `mutatedDocument:false`, `retrySafe`, and a recovery action without committing
  brushes.
- Done: cached `ir_apply_from_file` previewId guard failures report
  `mutatedDocument:false`, `retrySafe`, and recovery actions before applying IR.
- Done: `selection_set` reports `mutatedDocument:false` on success because it
  changes editor selection state without mutating the map document.
- Done: `selection_by_metadata` reports `mutatedDocument:false` on success
  because it may change editor selection without mutating the map document.
- Done: `selection_filter` and `selection_by_bounds` report
  `mutatedDocument:false` on success because they may change editor selection
  without mutating the map document.
- Done: `group_create_from_selection` precondition failures report
  `mutatedDocument:false`, `retrySafe`, and recovery actions before grouping.
- Done: `group_rename_selected` precondition failures report
  `mutatedDocument:false`, `retrySafe`, and recovery actions before renaming.
- Done: `group_ungroup_selected` precondition failures report
  `mutatedDocument:false`, `retrySafe`, and recovery actions before ungrouping.
- Done: `brush_metadata_set` reports `mutatedDocument:false` on success because
  it updates MCP metadata state without mutating the map document.
- Done: `brush_metadata_set` precondition failures report
  `mutatedDocument:false`, `retrySafe`, and recovery actions before updating
  metadata state.
- Partial: mutation tools still need a full audit for consistent mutation state,
  retry safety, recovery action, and compact stale output.

Next Phase 3 item:

- Continue auditing one mutation family at a time for document identity,
  mutation-state, and recovery-action consistency.

### Phase 4 Status: Partial

- Done: project skill source is `skills/trenchbroom-mcp-scene-workflow`, with
  runtime sync through `scripts\sync-trenchbroom-mcp-skill.ps1`.
- Done: recipe validator enforces deterministic IR and requires a render-review
  validation path for prefab-like recipes.
- Done: bundled recipes cover houses, routes, courtyards, caves, terrain passes,
  ascending loops, and KZ/bhop gameplay layouts.
- Done: real TB visual smoke covers bundled recipe `minimal/default/stress`
  variants, and representative contact sheets have been inspected.
- Partial: recipe coverage can still expand, but new recipes should wait until
  the current ordered phase requires Phase 4 work.

Next Phase 4 item:

- When Phase 4 is active again, add recipe coverage only for a missing common
  scene family, with validator, runtime sync, real TB visual acceptance, and
  inspected review output.

### Phase 5 Status: Partial

- Done: route target filtering has explicit selector/module guidance, mixed
  target warnings, and explicit slope/continuity mode routing in skill guidance.
- Done: non-convex polygon batch diagnostics report `polygonDiagnostics` with
  failing point/edge information for invalid polygons.
- Done: texture apply/replace paths report material existence so missing
  materials are discoverable.
- Partial: visual review still mostly reports render quality, not "looks like
  the requested target" critique.
- Missing: generic organic primitives remain a future kernel decision; current
  cave/terrain strength stays in recipes.

Next Phase 5 item:

- After earlier phases close, improve one validation/review feedback path that
  catches visually bad but technically valid output without adding scene-prefab
  C++ behavior.

### Phase 1: Catalog And Discovery Cleanup

Goal: reduce tool-surface confusion without removing useful execution paths.

Work:

- keep `Core / Modeling / Full` as the only active profiles
- keep Modeling as the normal Agent workbench
- remove or hide duplicate convenience aliases
- keep legacy payload support inside batch/IR where compatibility needs it
- make `tb_doctor` and broad `tb_tools_search(detail:"schema")` compact
- expose associated skill hints in `initialize`, `tb_status`, and `tb_doctor`
- add catalog tests for profile visibility, deleted names, exact search, and
  schema summaries

Done when:

- Modeling has no scene-prefab direct helper tools
- broad diagnostics stay compact
- exact-name schema lookup still works
- skill routing is visible before scene work starts

### Phase 2: Tool Definition Single Source

Goal: reduce schema drift and repeated hand-maintained metadata.

Work:

- centralize common schema fragments where practical
- keep selector/module metadata wording consistent
- ensure tool descriptions mention compact defaults and recovery paths
- make lifecycle/profile/category/search behavior explicit in one place
- add tests that catch missing lifecycle/profile/category fields

Done when:

- adding a tool requires one obvious catalog edit path
- common selector/module docs stop diverging
- profile/search/schema tests fail on accidental exposure

### Phase 3: Operation History And Recovery Hardening

Goal: make every mutation recoverable and diagnosable.

Work:

- ensure mutation records carry document path and fingerprint
- keep undo/redo guarded by document identity
- compact stale warnings by default
- expose operation expansion summaries for compound tools
- keep partial mutation diagnostics structured
- prefer selectors/modules over long id lists across turns

Done when:

- wrong-document undo/redo fails before mutation
- stale records are summarized unless `detail:"full"`
- compound operations can be inspected without huge payloads

### Phase 4: Recipe And IR Maturity

Goal: move scene generation strength out of C++ and into deterministic recipes.

Work:

- keep the project-owned skill source in
  `skills/trenchbroom-mcp-scene-workflow`
- keep recipe scripts deterministic and side-effect-free
- require recipes to emit IR files, not mutate maps
- add recipe manifests, examples, validators, and recommended MCP validation
  paths
- sync the runtime skill copy with `scripts\sync-trenchbroom-mcp-skill.ps1`
  after recipe or workflow changes
- require prefab-like recipes to declare a visual acceptance path through
  `module_render_review`, `render_review_selector`, or another review tool
- add recipe patterns for common scene families before asking for new C++ tools
- promote only repeated generic needs into MCP primitives

Done when:

- houses, routes, courtyards, caves, terrain passes, and gameplay layouts are
  recipe/IR workflows
- recipe validation fails when a prefab-like recipe has no review step
- MCP only sees generic operations, metadata, preview/apply, validation, and
  review

### Phase 5: Validation And Review Quality

Goal: make generated scenes easier to judge and fix.

Work:

- improve route target filtering through explicit selector/module metadata
- keep slope/continuity modes explicit
- add compact mixed-target warnings
- improve non-convex polygon diagnostics
- add generic organic primitives only when they remain scene-neutral
- improve texture/material existence checks and replacement workflows

Done when:

- prefab-like recipe acceptance includes inspected review output, not just IR
  validation or `map_validate`
- a visually bad but valid scene produces useful review feedback
- route/terrain validation does not confuse primary and secondary surfaces when
  metadata is explicit
- material problems are discoverable without manual guessing

## File And Module Direction

Long term, large MCP files should move toward responsibility-based units:

- catalog/profile/search/doctor/status
- document guard and bridge transport
- operation history and resources
- selector/module/object registry
- brush and batch primitives
- geometry validators and analysis
- review rendering
- IR preview/apply
- entities/textures/faces

Do not split files just because they are large. Split when it removes repeated
logic, makes tests narrower, or gives a clear ownership boundary.

## Skill Synchronization

The project-owned skill source is
`skills/trenchbroom-mcp-scene-workflow`. The runtime copy on this machine is
`C:\Users\Trh\.cc-switch\skills\trenchbroom-mcp-scene-workflow`.

When C++ MCP behavior changes, edit the project copy and sync only workflow
guidance into the runtime copy:

- which tool to use by default
- when to use recipe/IR instead of direct MCP
- which validation mode to choose
- how to keep output compact
- how to recover targets through selectors, modules, groups, or selection

Do not copy full C++ schemas into the skill.

Use these checks for skill and recipe changes:

```powershell
python skills\trenchbroom-mcp-scene-workflow\scripts\validate_recipes.py
powershell -ExecutionPolicy Bypass -File scripts\sync-trenchbroom-mcp-skill.ps1 -Check
```

## Non-Goals

This refactor should not:

- recreate Blender's arbitrary Python execution model
- turn MCP into an asset marketplace or external generator platform
- add scene-prefab C++ tools
- make Full profile the default Agent surface
- replace geometry validation with screenshots
- remove legacy compatibility payloads before old workflows have a migration
  path

## Acceptance Checklist

Before considering this refactor direction healthy, verify:

- Modeling profile is small enough for normal Agent work
- `tb_doctor` and broad tool search stay compact
- exact schema lookup still works
- deleted/hidden legacy tools have catalog tests
- mutation tools report document identity, mutation state, retry safety, and
  recovery action
- review outputs are readable and path-based
- prefab-like recipe workflows require visual review before acceptance
- route/slope claims are validated by geometry facts
- recipe workflows cover prefab-like scene requests
- skill routing is visible in initialization/status paths
- focused MCP tests and real TB disposable-map smoke match the risk of each
  implementation phase
