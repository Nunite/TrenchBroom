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

## Common Flow

1. Confirm binding before writing: connect with the configured Bearer token, call `tb_status`, and record `processId`, `bridgeInstanceId`, `activeDocumentPath`, and `documentFingerprint`. Pass both document guards to later mutations. If `tb_status` returns `associatedSkills` containing `trenchbroom-mcp-scene-workflow`, treat this skill as the workflow owner for scene intent and recipe routing.
2. Open disposable maps with `documents_open_verified` when switching documents. Do not pass document guards to open a different map; use both path and fingerprint guards on mutating tools after the target document is active.
3. Split the scene into `moduleId`s and parts before geometry:
   - `moduleId`: stable session name such as `temple-main-hall`, `route-a`, `terrain-pass`.
   - `part`: structural part such as `floor`, `wall`, `roof`, `rail`, `support`, `marker`, `spawn`.
   - `role`: playable purpose such as `walkable`, `boundary`, `guidance`, `decoration`, `lighting`.
   - Optional: `routeId`, `order`, `temporary`, `generatedBy`.
4. Prefer `ir_compile_preview` before bulk writes. New IR must contain `schemaVersion:1`. For large operation sets, write the IR JSON to a local file and use `ir_compile_preview_from_file` / `ir_apply_from_file` so the conversation does not carry a huge payload. Apply with the returned `previewId`; if the file changes after preview, preview it again.
5. For direct creation tools, pass `defaultMetadata` and per-operation `metadata`. For part-aware primitives, use `parts`, `partMaterials`, and `partMetadata` instead of generating extra parts and deleting them later.
6. Recover generated targets with `module_*` or structured selectors:
   - `module_list` defaults to live modules only; use `includeStale:true` only when debugging prior-document/session residue.
   - Use `module_inspect(idsMode:"count")` for counts, `idsMode:"sample"` for a small identity check, and `idsMode:"full"` only when a later tool truly needs every object id.
   - `selector_preview` before destructive actions.
   - `objects_select_by_selector` for inspection/editing.
   - `objects_delete_by_selector` only after preview confirms the intended count/sample.
   - Avoid carrying hundreds of object ids in context; use `idsMode: "count"` or `"sample"` unless full ids are necessary.
   - Use native groups only as a visible organization/selection layer for humans; keep semantic recovery in module metadata and selectors.
7. For dense existing maps or ambiguous brush ownership, prefer user selection over clever automatic brush matching:
   - Ask the user to select the target brushes in TrenchBroom, or use the current selection if they already did.
   - Then use selection-aware tools such as `geometry_analyze_selection`, `geometry_analyze_slopes`, `geometry_analyze_route_continuity`, `objects_transform`, and `render_review_current_scene(scope:"selection")`.
   - Do not invent complex bounds/material/metadata selector rules for old or manually edited geometry unless the user explicitly asks for that. User selection is the source of truth when brush counts get large.
8. Validate after each meaningful phase:
   - `history_status`, `operation_validate`, and `module_validate`.
   - `map_validate(groupByType:true)` and `problems_check` before reporting done on dense maps. Use `passed`, compact counts/groups, safe-fix counts, and `recoveryAction` before asking for problem detail.
   - `geometry_analyze_route_continuity` for ramps, platforms, roads, stairs, rails, and route seams.
   - `geometry_analyze_slopes` for ramp/surf/slide/wedge/ascending intent. Use `passed`, `slopeCount`, warning samples, and `recoveryAction` from the summary before asking for full detail. If a sloped surface was intended and `passed` is false or `slopeCount` is 0, treat the build as failed and rebuild with a true slope primitive.
9. Review visually with `module_render_review`, `render_review_selector`, `render_review_operation`, or `render_review_current_scene(scope:"selection")` for user-selected targets. Pass an absolute `outputDir` for saved review bundles. Prefer contact sheets with at most two panels. Use `labelParts` only for important metadata parts such as rails, route surfaces, supports, markers, or spawn points; use `labelStride` / `autoHideLabelsThreshold` for dense ordered routes, then open individual PNGs only when needed.
10. Report friction as MCP design feedback: P0 crash/wrong-map/data loss, P1 blocked real workflow, P2 awkward or context-heavy, P3 documentation/default issue.

An IR apply is one aggregate operation. Keep `parentOperationId` as the undo/redo
target and treat `childOperationIds` as audit detail. If apply fails, require
`mutatedDocument:false`, `partialMutation:false`, and `retrySafe:true` before retrying.
If stdio times out, mutation state is unknown: inspect `history_status` and recent
operations before retrying. If an operation or review resource was evicted from the
bounded session cache, follow its `recoveryAction` instead of guessing stale ids.

## Default Tool Choice

Treat the Modeling profile as the normal Agent workbench. It intentionally exposes the high-frequency path and keeps expert/debug tools searchable instead of visible by default.

| Task | Default tools |
| --- | --- |
| Bind/open/status | `tb_status`, `documents_open_verified`, `map_snapshot`, `history_status` |
| Bulk geometry | `ir_compile_preview`, `ir_compile_preview_from_file`, `ir_apply`, `ir_apply_from_file`, `blockout_create_batch`, `brush_create_boxes_batch`, `brush_create_polygon_batch`, `heightmap_preview_grayscale`, `heightmap_import_grayscale` |
| Target recovery | `selector_preview`, `module_list`, `module_inspect`, `operation_inspect`, `operation_validate`, `geometry_analyze_selection` |
| Iteration edits | `objects_transform` on selection or selector, `objects_delete_by_selector`, `entity_properties_update`, `entity_properties_delete`, `texture_apply_by_filter`, `texture_align_face` |
| Boolean geometry | `geometry_csg_selection` after user or selector-driven brush selection |
| User-visible organization | `group_create_from_selection`, `group_inspect`; search for `group_rename_selected` / `group_ungroup_selected` when needed |
| Validation | `geometry_analyze_selection`, `geometry_analyze_slopes`, `geometry_analyze_route_continuity`, `module_validate`, `map_validate(groupByType:true)`, `problems_check` |
| Visual review | `render_review_selector`, `render_review_operation`, `render_review_current_scene`, `module_render_review` |

When a needed capability is not visible, search for it explicitly with `tb_tools_search(detail:"schema")` and check `visibleInCurrentProfile:false`. Hidden/searchable tools are for specific expert cases:

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

Prefab-like recipes must have an explicit visual acceptance path. A recipe is not
accepted just because IR validation and `map_validate` pass; its manifest must
recommend `module_render_review`, `render_review_selector`, or another review
tool, and real workflow acceptance should inspect the rendered output for the
requested scene intent.

When adding or proposing a new recipe, do not add a matching C++ MCP prefab tool.
If the recipe reveals missing MCP support, phrase the feedback as a generic
primitive, selector/module operation, validator, review feature, or compact output
improvement. Promote recipe behavior into MCP only after repeated independent
workflows prove it is a generic editor capability.

Bundled prefab-like recipes are intentionally small: `ascending_loop`,
`rect_shell`, `opening_wall`, `cover_block`, and `stair_run`. Removed or failed
visual-acceptance recipes should be rebuilt as new deterministic IR recipes only
after explicit human visual acceptance, not restored as C++ MCP prefab tools.

## Selector Rules

Use structured JSON selectors, not free text DSL:

```json
{"selector": {"moduleId": "route-a", "part": "rail"}}
```

Combine filters only when needed: metadata + type, moduleId + material, operationIds + bounds, classname + bounds. Always preview before select/delete/render when the selection is not obvious.

Selectors are best for objects the Agent just generated with metadata. For old maps, mixed manual edits, or dense brushwork, do not make the Agent guess targets with elaborate selector rules. Let the user select the intended brushes in TrenchBroom, then call selection-aware tools.

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

For UV/material alignment, prefer `texture_align_face` on the current face
selection or a narrow target with `faceSemantic` / `normal`. Use `mode:"paraxial"`
for world/grid alignment, `mode:"parallel"` for face-plane alignment, and
`mode:"reset"` only when intentionally clearing custom UV axes. Search for
`texture_copy_from_face` or `face_texture_set` only when exact source-face copying
or numeric UV edits are required.

## Review Rules

Use review renderer output as evidence, not as the only validator. Contact sheets are for quick recognition; if a scene is dense, inspect individual captures. For terrain or routes, request side/iso views and `verticalExaggeration` when height changes are subtle.

Keep labels sparse. Entity glyph markers remain useful without classname text on dense modules, so let `autoHideLabelsThreshold` hide entity/order/part labels by default. When the structure needs semantic callouts, prefer `labelParts:["road","rail"]` or another short part list instead of labeling every object.

## Route Rules

For route-like scenes, give every playable piece `routeId` and `order`. After generating:

- Run `geometry_analyze_route_continuity` with `start/end`, `routeDirection`, or `orderBy:"metadataOrder"`. Use `routeMode:"continuous"`, `"stepped"`, `"jump_chain"`, `"spiral"`, or `"closed_loop"` to declare intent. For circular routes, pass `routeMode:"closed_loop"` or `closedLoop:true` only when the final surface is meant to connect back to the first.
- Run `geometry_analyze_slopes` for every ramp/surf/slide/wedge/ascending section. `slopeCount=0` is a failure when a smooth slope was requested.
- Leave slope/continuity `detail` at the summary default for normal acceptance. Use summary `passed` and `recoveryAction` first; use `detail:"full"` only when you need every face/seam object for debugging.
- Treat discontinuities, vertical steps, wrong slope direction, rail intersections, and support posts piercing road surfaces as MCP feedback unless the design intentionally calls for them.
- For smooth ascending loops, prefer `arc_ramp` / `helical_ramp` or explicit true `ramp_between` segments. Do not treat `curved_corridor slopeStartZ/slopeEndZ` as a final smooth road surface; it is stepped/terraced unless the MCP result proves otherwise.
- `path_ribbon points3d` is useful for flat ribbons along 3D waypoints, but adjacent Z changes are not proof of an interpolated ramp surface. If height changes matter, verify slopes or rebuild with true ramp geometry.
- If preview/apply returns `offAxisRampMayProduceNonGridVertices`, choose an axis-aligned `ramp_between`/`wedge` for strict grid geometry, lower the grid, or explicitly accept off-grid diagonal ramp vertices.
- For generated stairs, selector metadata may normalize the authored part to `steps`; query `part:"steps"` or use a broader module/role selector when recovering stairs.
- Same-height route overlaps can be continuous; inspect seam `classification` and `continuous` instead of treating any overlap as a break.
- After deleting/rebuilding a route module, use `module_inspect` live-only stats and `module_compact` to clear stale session records before reporting counts.

## Codex CLI Regression

When dispatching a fresh Codex CLI worker, keep the task narrow and disposable:

- Read `references/codex-cli-regression-prompts.md` for the three standard
  scenario prompts and report shape.
- Use `codex exec --profile tb-mcp-flow --ephemeral --sandbox read-only --cd <repo> -o <report.md> "<prompt>"`.
- Tell the worker not to edit source or commit.
- Tell the worker to use TrenchBroom MCP only, write to a disposable map, record status/operation ids/review paths/validators/friction, and stop on P0.
