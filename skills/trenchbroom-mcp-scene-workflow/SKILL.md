---
name: trenchbroom-mcp-scene-workflow
description: Use when the user says tb mcp or TrenchBroom MCP, or asks to build, edit, validate, or review TrenchBroom scenes, maps, modules, routes, ramps, architecture, terrain, or prefab-like layouts with MCP. Use skill recipes for scene intent that should emit IR, then MCP preview/apply/validate/review.
---

# TrenchBroom MCP Scene Workflow

## Contract

Use this skill to turn scene intent into small, recoverable MCP operations. Keep the split strict:

- Skill: domain planning, module naming, route intent, visual/readability judgment, IR structure, validation order, and final critique.
- MCP: atomic geometry/entity edits, metadata storage, selectors, module tracking, geometry facts, history, map validation, and review renders.
- Do not ask MCP for scene-specific prefab behavior. Use generic primitives, metadata, selectors, modules, and IR.
- If a requested capability sounds like a named scene, route family, gameplay pattern, or architectural style, keep it in a recipe script. Only treat it as MCP feedback when it requires a reusable editor primitive, document/undo safety, live object identity, geometry validation, or review rendering.
- For MCP development boundaries, treat the repository `docs/mcp-development-governance.md` as the source of truth. This skill owns routing and creative judgement, not C++ MCP policy.

## Skill Policy

Prefer policies over rigid procedures. This skill should usually express goals,
constraints, validation requirements, recovery strategies, and heuristics. Use
strict "must" language only for editor safety, mutation boundaries, deterministic
recipe output, validation gates, compatibility, and failure recovery. Creative
planning, layout organization, aesthetics, gameplay interpretation, and high-level
design decisions remain under Agent control.

## Minimum Agent Checklist

For every TrenchBroom MCP scene task, satisfy this checklist before claiming the
work is done:

1. Bind first: call `tb_status`, `tb_doctor`, and `documents_list`; record
   `activeDocumentPath`, `documentFingerprint`, `processId`, and
   `bridgeInstanceId`. If `documents_list` is not visible in the current client
   profile, use `tb_status.openDocumentsSummary` or the `tb_doctor` document
   summary as a lower-confidence fallback and report that fallback.
2. Treat TrenchBroom MCP as the source of truth. Do not read or write `.map`
   files directly unless the user explicitly approves that fallback.
3. If a tool, operation type, parameter, recipe, or selector shape is uncertain,
   call `tb_tools_search(detail:"schema")` or inspect the recipe manifest before
   guessing.
4. Before mutating, record a `problems_check` baseline and pass both
   `expectedDocumentPath` and `expectedDocumentFingerprint` when the tool accepts
   document guards.
5. For generated geometry, set `moduleId` and useful `part` / `role` metadata;
   use `replace_module` or file preview/apply with `previewId` for iteration.
6. For ambiguous existing geometry, use the user's current selection plus
   `selection_inspect`; do not infer intent from bounds, classnames, or material
   selectors when selection context exists.
7. For linked entities, use `entity_link_chain_inspect`; stop and report the MCP
   capability gap if entity properties or links are unavailable.
8. Preview selectors before destructive actions. If selector counts disagree with
   module or history counts, inspect `matchedBeforeLimit`, `limitApplied`,
   `staleExcluded`, and the module/operation/metadata count diagnostics.
9. Review images are evidence, not validation. `renderReadable` only means the
   render output is readable; it does not prove semantic geometry correctness.
10. Run the relevant validators: at minimum `map_validate(groupByType:true)` and
    `problems_check`; add slope, route-continuity, or shell-seam analysis when
    the scene intent depends on those properties.
11. Save only after explicit user confirmation, using `documents_save_current`
    or `documents_save_as`.
12. Final reports must separate static validator results, visual review status,
    save status, BSP/game validation status, and any `notEvaluated` limitations.

## Common Flow

Use this as a lightweight editor loop for interactive scene work. Do not create
design documents, plans, or git commits for ordinary map edits unless the user
asks to change repository source, add a durable recipe, or commit project files.

1. Confirm binding before writing: connect with the configured Bearer token, call `tb_status`, `tb_doctor`, and `documents_list`, and record `processId`, `bridgeInstanceId`, `activeDocumentPath`, and `documentFingerprint`. If `documents_list` is not callable in the current profile or client, use `tb_status.openDocumentsSummary` / `tb_doctor` document summary only as a fallback and say so. Pass both document guards to later mutations. Record a pre-mutation `problems_check` baseline, including stable problem ids and whether the response is truncated. If `tb_status` returns `associatedSkills` containing `trenchbroom-mcp-scene-workflow`, treat this skill as the workflow owner for scene intent and recipe routing.
2. Open disposable maps with `documents_open_verified` when switching documents. Do not pass document guards to open a different map; use both path and fingerprint guards on mutating tools after the target document is active.
3. Split the scene into `moduleId`s and parts before geometry:
   - `moduleId`: stable session name such as `temple-main-hall`, `route-a`, `terrain-pass`.
   - `part`: structural part such as `floor`, `wall`, `roof`, `rail`, `support`, `marker`, `spawn`.
   - `role`: playable purpose such as `walkable`, `boundary`, `guidance`, `decoration`, `lighting`.
   - Optional: `routeId`, `order`, `temporary`, `generatedBy`.
4. Choose a quality intent from the user's goal: `draft` for fast blockout, `balanced` by default, and `smooth` only when the user explicitly wants strict curve quality. Add `qualityPolicy` to IR v1 and run the curve-quality preview before applying. Warnings do not block `draft` or `balanced`; a failed `smooth` policy blocks acceptance.
5. Prefer `ir_compile_preview` before bulk writes. New IR must contain `schemaVersion:1`. Use `applyMode:"create"` for a new module. For an existing generated module, use `applyMode:"replace_module"` and preserve the preview's IR hash, target module revision, content hash, and canonical object-set guards. For large operation sets, use file preview/apply with the returned `previewId`; if the file or target module changes, preview again.
6. For direct creation tools, pass `defaultMetadata` and per-operation `metadata`. For part-aware primitives, use `parts`, `partMaterials`, and `partMetadata` instead of generating extra parts and deleting them later.
7. Recover generated targets with `module_*` or structured selectors:
   - `module_list` defaults to live modules only; use `includeStale:true` only when debugging prior-document/session residue.
   - Use `module_inspect(idsMode:"count")` for counts, `idsMode:"sample"` for a small identity check, and `idsMode:"full"` only when a later tool truly needs every object id.
   - `selector_preview` before destructive actions.
   - `objects_select_by_selector` for inspection/editing.
   - `objects_delete_by_selector` only after preview confirms the intended count/sample.
   - Avoid carrying hundreds of object ids in context; use `idsMode: "count"` or `"sample"` unless full ids are necessary.
   - Use native groups only as a visible organization/selection layer for humans; keep semantic recovery in module metadata and selectors.
8. For dense existing maps or ambiguous brush ownership, prefer user selection over clever automatic brush matching:
   - Ask the user to select the target brushes in TrenchBroom, or use the current selection if they already did.
   - Use `selection_inspect` to confirm the current selection and read selected entity key/value properties before inferring intent from map-wide selectors. Use `detail:"summary"` by default, and `detail:"full"` or `includeProperties:true` when entity links such as `targetname` / `target` matter.
   - For linked entities, use `entity_link_chain_inspect` after confirming the selected start. It reads the live active map and reports missing targets, duplicate names, and cycles. If neither `selection_inspect` nor an equivalent entity property/link inspection tool is callable, stop and report the MCP capability gap; do not read the `.map` file as a fallback unless the user explicitly approves that source.
   - Then use selection-aware tools such as `geometry_analyze_selection`, `geometry_analyze_slopes`, `geometry_analyze_route_continuity`, `objects_transform`, and `render_review_current_scene(scope:"selection")`.
   - Do not invent complex bounds/material/metadata selector rules for old or manually edited geometry unless the user explicitly asks for that. User selection is the source of truth when brush counts get large.
9. Validate after each meaningful phase:
   - `history_status`, `operation_validate`, and `module_validate`.
   - `map_validate(groupByType:true)` and `problems_check` before reporting done. If both problem responses are untruncated, compare stable ids and report `introduced`, `resolved`, and `preExisting`; otherwise compare grouped counts only and label the result low-confidence.
   - `geometry_analyze_route_continuity` for ramps, platforms, roads, stairs, rails, and route seams. Read `walkableContinuous`, `qualityStatus`, `acceptancePassed`, and `notEvaluated`; do not use legacy `passed` as the completion verdict.
   - `geometry_analyze_slopes` for ramp/surf/slide/wedge/ascending intent. Use `passed`, `slopeCount`, warning samples, and `recoveryAction` from the summary before asking for full detail. If a sloped surface was intended and `passed` is false or `slopeCount` is 0, treat the build as failed and rebuild with a true slope primitive.
   - `geometry_analyze_shell_seams` for recipe output that annotates `shellSeams`, such as `path_tunnel`. This validates only annotated shell intent; missing annotations mean rebuild with an annotated recipe or use visual review, not automatic inference.
10. Optionally collect visual evidence with `module_render_review`, `render_review_selector`, `render_review_operation`, or `render_review_current_scene(scope:"selection")`. For shape-sensitive modules, prefer one `edgeMode:"all"` construction view and one `edgeMode:"silhouette"` outline view. `renderReadable` only means the render output is readable; `qualityValid` is a legacy alias and is not geometry acceptance. Review evidence never changes `acceptancePassed`; if review was skipped, report `visualReview:"not_run"`.
11. Report module revision/content hash, problem delta, `completionState.saveRequired`, visual review status, BSP compile status, and `notEvaluated`. Never describe map validation as BSP or game-collision validation. Save only after explicit user confirmation, using `documents_save_current` for the active persistent map or `documents_save_as` for a requested path. Report friction as P0 crash/wrong-map/data loss, P1 blocked real workflow, P2 awkward or context-heavy, or P3 documentation/default issue.

An IR apply is one aggregate operation. Keep `undoOperationId` / `parentOperationId`
as the undo/redo target and treat `childOperationIds` / `auditOperationIds` as audit detail. If apply fails, require
`mutatedDocument:false`, `partialMutation:false`, and `retrySafe:true` before retrying.
If stdio times out, mutation state is unknown: inspect `history_status` and recent
operations before retrying. If an operation or review resource was evicted from the
bounded session cache, follow its `recoveryAction` instead of guessing stale ids.

## Default Tool Choice

Treat the Modeling profile as the normal Agent workbench. It intentionally exposes the high-frequency path and keeps expert/debug tools searchable instead of visible by default.

| Task | Default tools |
| --- | --- |
| Bind/open/status | `tb_status`, `documents_open_verified`, `map_snapshot`, `history_status`, `documents_save_current` / `documents_save_as` after explicit user confirmation |
| Bulk geometry | `ir_compile_preview`, `ir_compile_preview_from_file`, `ir_apply`, `ir_apply_from_file`, `blockout_create_batch`, `brush_create_boxes_batch`, `brush_create_polygon_batch`, `heightmap_preview_grayscale`, `heightmap_import_grayscale` |
| Target recovery | `selection_inspect`, `entity_link_chain_inspect`, `selector_preview`, `module_list`, `module_inspect`, `operation_inspect`, `operation_validate`, `geometry_analyze_selection` |
| Iteration edits | `objects_transform` on selection or selector, `objects_delete_by_selector`, `entity_properties_update`, `entity_properties_delete`, `texture_apply_by_filter`, `texture_align_face` |
| Boolean geometry | `geometry_csg_selection` after user or selector-driven brush selection |
| User-visible organization | `group_create_from_selection`, `group_inspect`; search for `group_rename_selected` / `group_ungroup_selected` when needed |
| Validation | `geometry_analyze_selection`, `geometry_analyze_slopes`, `geometry_analyze_route_continuity`, `geometry_analyze_shell_seams`, `module_validate`, `map_validate(groupByType:true)`, `problems_check` |
| Visual review | `render_review_selector`, `render_review_operation`, `render_review_current_scene`, `module_render_review` |

When a needed capability is not visible, or when an operation type/parameter is uncertain, search for it explicitly with `tb_tools_search(detail:"schema")` and check `visibleInCurrentProfile:false`. Do this before guessing `blockout_create_batch` operation names such as tube, arch, or corridor; for path-shaped arches and pipes, prefer the `path_sweep` recipe. Hidden/searchable tools are for specific expert cases:

- `viewport_capture_*` and `viewport_capture_scene_review`: UI viewport/layout/camera debugging. For normal scene review, use the geometry review tools above.
- `render_review_targets`: low-level review when you already have exact object or operation targets. Prefer selector/module/operation wrappers.
- `selection_by_metadata` and `brush_metadata_*`: legacy metadata workflow. Prefer structured selectors and module tools.
- `map_search`, `history_list`, `operation_select`, `module_select`, and `ir_validate`: diagnostic or manual recovery helpers. Prefer `map_snapshot`, `history_status`, `operation_inspect`, `selector_preview`, `module_inspect`, and file IR preview/apply in the default workflow.
- Fine-grained face/texture/entity schema helpers: use only when batch semantic tools are insufficient.

Do not add duplicate alias tools to make a workflow easier to remember. Put the decision rule here in the skill, and keep MCP as atomic execution plus facts.

## Group Rules

Native TrenchBroom groups are for Outliner organization and user-facing selection convenience. They are not a replacement for Agent metadata:

- Use `moduleId`, `part`, `role`, `routeId`, and `order` for semantic recovery and validation.
- Use `group_create_from_selection` after coarse structures exist and are already selected, such as `route-road`, `route-rails`, `route-supports`, or `temple-main-hall`.
- Keep groups coarse. Do not create a group for every brush, stair step, rail post, or temporary helper.
- For dense old maps, prefer asking the user to select the intended brushes, then group that selection or run selection-aware transform/analyze directly.
- After grouping, continue route validation through `geometry_analyze_slopes` and `geometry_analyze_route_continuity` using metadata/order or user selection. Do not infer route order from group names.
- Grouping changes the map hierarchy, so avoid using it as an automatic IR/apply step unless the user asked for visible organization.

## Recipe Scripts

For complex prefab-like scenes, use skill recipe scripts to generate IR files,
then apply those files through MCP. Read `references/recipes.md` when you need
recipe parameters, grouped examples, validation commands, or IR shape details.

Common recipe path:

1. Run `python <skill>/scripts/list_recipes.py` to choose a recipe and see its recommended validation path.
2. Run `python <skill>/scripts/recipes/<recipe>.py --describe` to inspect parameters when needed.
3. Generate or choose params from `scripts/examples/<recipe>/minimal|default|stress.json`.
4. Run the recipe with `--params <params.json> --out <ir.json>`.
5. Confirm the generated file contains `schemaVersion:1`. Use
   `ir_compile_preview_from_file`, then apply with the returned `previewId`.
6. Recover targets with `module_inspect` / `selector_preview`, validate, and review.

Recipes must not call TrenchBroom, MCP, or `tb2` directly. MCP remains the only
map mutation layer.

Prefab-like recipes must declare `qualityPolicy` and
`reviewPolicy:{recommended:true,required:false}`. Their manifest must recommend a
review tool, but review remains optional evidence and is not a static recipe or
`acceptancePassed` gate. Never claim a visual verdict when review was not run.

When adding or proposing a new recipe, do not add a matching C++ MCP prefab tool.
If the recipe reveals missing MCP support, phrase the feedback as a generic
primitive, selector/module operation, validator, review feature, or compact output
improvement. Promote recipe behavior into MCP only after repeated independent
workflows prove it is a generic editor capability.

Bundled prefab-like recipes are intentionally small: `ascending_loop`,
`path_sweep`, `path_tunnel`, `rect_shell`, `opening_wall`, `cover_block`, and `stair_run`. Removed or failed
visual-acceptance recipes should be rebuilt as new deterministic IR recipes only
after explicit human visual acceptance, not restored as C++ MCP prefab tools.

## Selector Rules

Use structured JSON selectors, not free text DSL:

```json
{"selector": {"moduleId": "route-a", "part": "rail"}}
```

Combine filters only when needed: metadata + type, moduleId + material, operationIds + bounds, classname + bounds. Always preview before select/delete/render when the selection is not obvious. If `selector_preview` or `render_review_selector` returns fewer objects than `module_inspect` or recent operation history, inspect `matchedBeforeLimit`, `limitApplied`, `staleExcluded`, `moduleObjectIdCount`, `operationObjectIdCount`, and `metadataRecordCount`; use `module_render_review(moduleId)` for full generated modules and `render_review_current_scene(scope:"mcp_history")` for recent MCP output when selector recovery is ambiguous.

Selectors are best for objects the Agent just generated with metadata. For old maps, mixed manual edits, entity chains, or dense brushwork, do not make the Agent guess targets with elaborate selector rules. Let the user select the intended objects in TrenchBroom, inspect them with `selection_inspect`, use `entity_link_chain_inspect` for key/value links when needed, then call selection-aware tools.

When the user asks to stretch, shorten, move, rotate, or rescale an existing module/route/part, prefer `selector_preview` followed by `objects_transform` with the same `selector`. Do not delete and rebuild just to make a small proportional or non-uniform transform. Use `idsMode:"count"` or `"sample"` unless full ids are truly needed.

When the user has selected the target geometry themselves, call `objects_transform` without `objectIds`, `operationIds`, or `selector`; it will operate on the current selection. This is preferred for ambiguous existing brushwork.

After transforming route-like targets, rerun `geometry_analyze_route_continuity` and, when slopes are involved, `geometry_analyze_slopes`, then review with `render_review_selector` or `module_render_review`.

For boolean brush edits, use `geometry_csg_selection` only after the intended
brushes are selected by the user, `selection_set`, or `objects_select_by_selector`.
Do not ask CSG to infer targets from scene intent. After CSG, inspect the operation,
run `map_validate`/`problems_check`, and review the affected selection or module
when the shape matters.

Do not look for direct legacy helpers such as `blockout_create_room`,
`blockout_create_doorway`, `blockout_create_cover`, or
`blockout_create_sky_shell`; those MCP tool names are removed from the catalog.
For room, corridor, sky shell, doorway, cover, and straight stair composition,
use `rect_shell`, `opening_wall`, `cover_block`, or `stair_run` recipes, or write
explicit primitive IR. `opening_wall` creates a new segmented wall with an
opening; it does not cut existing brushes. For openings in existing geometry,
select the target wall and cutter brush, then use
`geometry_csg_selection(operation:"subtract")`.

## Architectural Rules

For roofs, gables, and other visually directional wedge-like geometry, avoid
assuming `wedge axis` orientation from memory. When the silhouette matters,
prefer explicit `brush_create_polygon_batch` / IR polyhedron geometry, or create a
small sample first and inspect review output before building the full module.
`operation_validate` proves brush validity, not architectural readability.

## Material Rules

Use `texture_search` before relying on a named texture. `texture_apply`,
`texture_apply_by_filter`, and `texture_replace` report material existence fields;
if `materialExists` or `replaceMaterialExists` is false, treat the write as a
placeholder/tag and either choose a loaded material from `texture_search` or report
that the requested material was not available.

For sweep-style recipes, preserve texture intent in recipe params instead of
hard-coding one material. Use `partMaterials` for floor, wall, ceiling, pipe, and
cap material choices. Use `texturePolicy` metadata to record alignment intent:
`mode:"part_materials"` for direct material assignment, `mode:"metadata_hints"`
when a later agent or human should review/apply UVs, and `mode:"post_apply"` when
the planned workflow includes `texture_apply_by_filter`, `texture_align_face`, or
`texture_copy_from_face` after IR apply. Recipe metadata records intent; exact
face UV transfer still belongs to MCP face/texture tools after the generated
brushes exist.

For UV/material alignment, prefer `texture_align_face` on the current face
selection or a narrow target with `faceSemantic` / `normal`. Use `mode:"paraxial"`
for world/grid alignment, `mode:"parallel"` for face-plane alignment, and
`mode:"reset"` only when intentionally clearing custom UV axes. Search for
`texture_copy_from_face` or `face_texture_set` only when exact source-face copying
or numeric UV edits are required.

## Review Rules

Use review renderer output as optional evidence, not as an automatic validator. `renderReadable=true` only means the PNG/contact sheet was produced and readable; it does not prove no overlaps, no leaks, good tunnel closure, or design intent. Use `edgeMode:"all"` to expose construction seams and `edgeMode:"silhouette"` to judge the outer profile without internal Brush edges. Contact sheets are for quick recognition; if a scene is dense, inspect individual captures. For terrain or routes, request side/iso views and `verticalExaggeration` when height changes are subtle.

Keep labels sparse. Entity glyph markers remain useful without classname text on dense modules, so let `autoHideLabelsThreshold` hide entity/order/part labels by default. When the structure needs semantic callouts, prefer `labelParts:["road","rail"]` or another short part list instead of labeling every object.

## Route Rules

For route-like scenes, give every playable piece `routeId` and `order`. After generating:

- Run `geometry_analyze_route_continuity` with `start/end`, `routeDirection`, or `orderBy:"metadataOrder"`. Use `routeMode:"continuous"`, `"stepped"`, `"jump_chain"`, `"spiral"`, or `"closed_loop"` to declare intent. For circular routes, pass `routeMode:"closed_loop"` or `closedLoop:true` only when the final surface is meant to connect back to the first.
- Run `geometry_analyze_slopes` for every ramp/surf/slide/wedge/ascending section. `slopeCount=0` is a failure when a smooth slope was requested.
- Leave slope/continuity `detail` at the summary default for normal acceptance. Use `walkableContinuous`, `qualityStatus`, `acceptancePassed`, `notEvaluated`, and `recoveryAction`; use `detail:"full"` only when you need every face/seam object for debugging. Treat `seamRelation:"overlap"` as overlap, not a gap, even when legacy endpoint-distance fields are large.
- Treat discontinuities, vertical steps, wrong slope direction, rail intersections, and support posts piercing road surfaces as MCP feedback unless the design intentionally calls for them.
- For smooth ascending loops, prefer `arc_ramp` / `helical_ramp` or explicit true `ramp_between` segments. Do not treat `curved_corridor slopeStartZ/slopeEndZ` as a final smooth road surface; it is stepped/terraced unless the MCP result proves otherwise.
- `path_ribbon points3d` is useful for flat ribbons along 3D waypoints, but adjacent Z changes are not proof of an interpolated ramp surface. If height changes matter, verify slopes or rebuild with true ramp geometry.
- If preview/apply returns `offAxisRampMayProduceNonGridVertices`, choose an axis-aligned `ramp_between`/`wedge` for strict grid geometry, lower the grid, or explicitly accept off-grid diagonal ramp vertices.
- For generated stairs, selector metadata may normalize the authored part to `steps`; query `part:"steps"` or use a broader module/role selector when recovering stairs.
- Same-height route overlaps can be continuous; inspect seam `classification` and `continuous` instead of treating any overlap as a break.
- Prefer guarded `replace_module` over delete/rebuild for generated routes. Normal mutation/Undo/Redo reconciliation refreshes live module identity automatically. Use `module_compact` only as exceptional recovery for stale or legacy aliases.

When the user asks for a tunnel/corridor along `path_corner`, path, or route entities:

- Start by inspecting the current selection with
  `selection_inspect(detail:"full", includeProperties:true)`.
- If the selection contains exactly one `path_corner`, treat it as the route
  start. Build the chain by calling
  `entity_link_chain_inspect(classname:"path_corner", start:{source:"selection"}, nameKey:"targetname", nextKey:"target")`.
- If the selection is brush or face geometry and the user says "use this" /
  "用这个", treat the selection as a profile, material, or template cue, not as
  the path start. Use `path_sweep` with a custom profile for sloped, arched, or
  non-rectangular sections. Since MCP does not yet extract a sweep profile from
  selected brushes automatically, either derive an approximate profile from
  `selection_inspect` / `geometry_analyze_selection` and report that limitation,
  or ask the user for the profile dimensions before applying.
- If the current selection is not a start entity, call
  `entity_link_chain_inspect(..., start:{source:"selection"}, includeAllNodes:true)`
  to get candidate nodes, then use an explicit `start:{source:"targetname"}` once
  the start is known. Do not change the user's selection just to inspect entity
  properties; prefer `selector_preview(detail:"full")` when selecting would lose
  useful brush/profile context.
- Do not infer path order from spatial proximity when entity links are present.
- If `selection_inspect` or `entity_link_chain_inspect` is unavailable, stop and report that MCP cannot expose path_corner target links. Do not read the active `.map` file from disk unless the user explicitly approves that fallback.
- Generate IR with the `path_tunnel` recipe for a rectangular tunnel, or use
  `path_sweep` when the requested profile is arched, pipe-like, selected from
  brush geometry, or otherwise custom. Treat each `path_corner.origin` as the
  floor centerline for tunnel presets; the recipes use shared miter sections at
  nodes to avoid corner gaps.
- Use `qualityIntent:"smooth"` when the user asks for strict smoothness
  acceptance. For visibly smoother arch or pipe cross-sections, also raise
  `archSegments` or `pipeSegments`; this does not automatically curve a
  `path_corner` polyline.
- Preserve texture choices with `partMaterials` and `texturePolicy`. If the user
  wants exact copied UVs from existing brush faces, apply the recipe first, then
  use `texture_copy_from_face` or `texture_align_face` on the generated
  module/parts.
- Use file flow: generate an IR file, run `ir_compile_preview_from_file`, then call `ir_apply_from_file` with the returned `previewId`. If apply reports an IR hash mismatch, regenerate or re-preview the current IR file and apply the new `previewId`; do not reuse an old hash by hand.
- Validate with `geometry_analyze_shell_seams(selector:{moduleId})`, then `module_render_review(edgeMode:"all")`, `module_render_review(edgeMode:"silhouette")`, `map_validate(groupByType:true)`, and `problems_check`.
- Report seam validator results separately from visual review. `shellContinuous` / `acceptancePassed` are static annotation checks; review remains human-visible evidence.

## Codex CLI Regression

When dispatching a fresh Codex CLI worker, keep the task narrow and disposable:

- Read `references/codex-cli-regression-prompts.md` for the three standard
  scenario prompts and report shape.
- Use `codex exec --profile tb-mcp-flow --ephemeral --sandbox read-only --cd <repo> -o <report.md> "<prompt>"`.
- Tell the worker not to edit source or commit.
- Tell the worker to use TrenchBroom MCP only, write to a disposable map, record status/operation ids/review paths/validators/friction, and stop on P0.
