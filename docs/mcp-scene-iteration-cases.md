# MCP Scene Iteration Cases

This document tracks scene-level MCP test cases. The goal is to build each scene as a whitebox, review screenshots and geometry facts, then adjust MCP tools when the agent is forced into brittle or overly verbose workflows.

The scenes below are intentionally not prefab requests. Each one should be built from atomic or mid-level modeling operations such as polygon batches, path ribbons, arches, cuts, transforms, metadata, and viewport review.

## Review Principles

- Use whitebox materials only. The screenshot review should judge silhouette, scale, spatial logic, connectivity, and recognizable scene intent.
- Build in phases: playable/main massing, boundaries and supports, then markers/details. Each phase should have its own operation record.
- Start each scene from a clean standalone `.map` file. Do not accumulate multiple scene cases in one map, because cross-scene bounds, selection, and screenshot review become ambiguous.
- Prefer atomic and reusable tools over scene-specific prefab helpers.
- Record which MCP limitation blocked the agent, caused excessive context, or forced awkward geometry.
- Capture at least top, three-quarter, and detail/interior views once viewport camera control exists. Until then, use `viewport_focus` plus available captures.

## 1. Mountain Valley Boardwalk

Scene goal: A natural canyon with layered rock walls, a wood boardwalk, a small bridge, and a cave or tunnel mouth.

Atomic stress points:
- Irregular terrain and cliff walls.
- Paths that hug natural surfaces.
- Repeating boards, posts, and rail segments.
- Natural cave opening connected to built geometry.

Screenshot acceptance:
- The valley reads as a canyon rather than a box room.
- The boardwalk has a clear continuous route.
- Bridge and rail pieces align with the route.
- There are no obvious floating supports or gaps at cliff contact points.

Likely MCP gaps:
- Terrain and organic rock editing may be too coarse.
- Missing path-based rail/support generation.
- Missing contact checks between built objects and terrain.

Useful future atomic tools:
- `terrain_patch_from_grid`
- `path_ribbon`
- `rail_from_path`
- `support_posts_under_path`
- `geometry_contact_analyze`

## 2. Coastal Lighthouse And Breakwater

Scene goal: A lighthouse on a rocky coast with a breakwater, dock, rocks, and a small internal stair or service platform.

Atomic stress points:
- Cylinders, frustums, rings, and curved walls.
- Circular stairs or stacked platforms.
- Arc-aligned railings and repeated small details.
- Dock and seawall geometry with sloped edges.

Screenshot acceptance:
- The lighthouse silhouette is immediately readable.
- The tower, cap, dock, and breakwater have correct spatial relationships.
- Circular structures are smooth enough for whitebox.
- Stairs or platforms do not detach from the core.

Likely MCP gaps:
- Frustum and vertex-scaled cylinder support may be weak.
- Ring/arc wall creation may require too many manual sectors.
- Repeated rail or stone pieces may create context bloat.

Useful future atomic tools:
- `brush_create_frustum`
- `ring_wall_segment_batch`
- `repeat_along_arc`
- `repeat_along_path`

## 3. Chinese Courtyard

Scene goal: A courtyard with perimeter walls, moon gate, pavilion, rock garden, pool, and covered walkway.

Atomic stress points:
- Arch or circular wall openings.
- Sloped roofs and layered roof edges.
- Column grids and repeated beams.
- Curved and orthogonal route composition.

Screenshot acceptance:
- The courtyard, wall, pavilion, and moon gate are recognizable.
- The moon gate reads as round/arched, not just a rectangular hole.
- Roofs and columns are aligned and proportionate.
- Walking paths and interior/exterior transitions are clear.

Likely MCP gaps:
- Stable arch/opening generation is missing or immature.
- Roofs require many sloped prisms and trim bands.
- Decorative hierarchy can become prefab-like unless primitives are strong.

Useful future atomic tools:
- `arch_opening_from_wall`
- `roof_slope_prism_batch`
- `column_grid`
- `trim_band_create`

## 4. Abandoned Factory

Scene goal: A factory hall with steel platforms, beams, stairs, pipes, crane rails, damaged walls, and local lighting.

Atomic stress points:
- Repeating beams and platforms.
- Pipe paths with elbows.
- Wall cutouts and damaged openings.
- Entity batch creation for lights and markers.

Screenshot acceptance:
- The scene reads as industrial, not a generic box hall.
- Platforms, stairs, and rails connect logically.
- Pipes follow intentional paths and do not intersect randomly.
- Damaged walls remain valid brushwork.

Likely MCP gaps:
- Brush cut/opening tools are needed for doors, windows, and damage.
- Pipes along paths should not require many cylinder calls.
- Batch entity creation and operation history must stay readable.

Useful future atomic tools:
- `brush_cut_opening`
- `pipe_from_path`
- `beam_grid_batch`
- `entity_create_checked_batch`

## 5. Underground Metro Station

Scene goal: A station with platforms, rails, tunnel mouths, stairs, ticket hall, columns, and signage blocks.

Atomic stress points:
- Long aligned spaces.
- Arched or curved tunnel ceilings.
- Repeated lights, columns, and signs.
- Face/material operations over many brushes.

Screenshot acceptance:
- Platform and track layout is readable from top and 3D views.
- Tunnel entrances connect cleanly to the station volume.
- Stairs and concourse relationships make sense.
- Repeated elements are aligned and scaled consistently.

Likely MCP gaps:
- Need tunnel section primitives or reliable arch segments.
- Selection/filter defaults must avoid parent/world matches.
- Texture/UV lock and face tools matter even in whitebox.

Useful future atomic tools:
- `tunnel_section_batch`
- `track_pair_from_path`
- `face_texture_set_by_normal`
- `face_texture_set_by_filter`

## 6. Desert Ruin Temple

Scene goal: A stepped temple with columns, broken stone doors, underground entrance, sand dunes, and fallen blocks.

Atomic stress points:
- Stepped massing from profiles.
- Column arrays with partial damage.
- Controlled irregular edge chips.
- Terrain/building transitions.

Screenshot acceptance:
- The temple mass reads clearly as a stepped ruin.
- Broken elements look intentional, not random clutter.
- Entrance and route hierarchy are visible.
- Dunes or terrain do not swallow playable structure.

Likely MCP gaps:
- Need profile-based stepped geometry.
- Need controlled chip/chamfer operations.
- Need object metadata to track ruins, columns, terrain, and route pieces separately.

Useful future atomic tools:
- `stepped_mass_from_profile`
- `array_instances_as_brushes`
- `edge_chip_batch`
- `brush_chamfer_batch`

## 7. Snow Mountain Research Station

Scene goal: A modular station on a slope with snow terrain, antennas, bridge corridors, supports, and an ice cave.

Atomic stress points:
- Buildings sitting on uneven ground.
- Sloped ramps and bridge supports.
- Modular hard-surface blocks mixed with organic terrain.
- Contact and clearance checks.

Screenshot acceptance:
- The station reads as modular and elevated/slope-aware.
- Supports touch terrain or platforms cleanly.
- Bridge corridors connect between modules.
- Snow/ice terrain frames the scene without breaking navigation.

Likely MCP gaps:
- Need snap-to-surface or support generation.
- Need geometry contact analysis.
- Need reliable terrain decimation for smoother complex areas.

Useful future atomic tools:
- `snap_brush_bottom_to_surface`
- `support_posts_under_path`
- `terrain_adaptive_mesh`
- `geometry_contact_analyze`

## 8. European Castle Wall

Scene goal: Castle walls with towers, gatehouse, battlements, inner courtyard, and a ramp or stair route.

Atomic stress points:
- Walls along polylines.
- Round or polygonal towers.
- Repeated crenellations.
- Gate arches and wall openings.

Screenshot acceptance:
- The castle wall silhouette is obvious.
- Towers and gatehouse align with the wall path.
- Battlements repeat evenly without excessive object spam.
- Gate opening is passable and visually correct.

Likely MCP gaps:
- Wall-from-path and crenellation repetition are missing.
- Arched gates require reliable opening/cut tools.
- Round towers need controllable segment count and clean caps.

Useful future atomic tools:
- `wall_from_polyline`
- `crenellation_repeat`
- `gate_arch_cutout`
- `tower_from_profile`

## 9. Cyberpunk Street Corner

Scene goal: A street corner with shopfronts, layered facades, signs, vents, skybridge, and simple light strips.

Atomic stress points:
- Dense facade panels.
- Text or sign geometry.
- Repeating small props without huge context.
- Light/entity batches and route readability.

Screenshot acceptance:
- The street corner reads as urban and layered.
- Shopfronts and signs are placed on facades, not floating.
- Skybridge and vents have believable structure.
- Whitebox semantic colors or labels make functions distinguishable.

Likely MCP gaps:
- Text-to-brush or polygon import is needed.
- Facade panel batch tools could keep output compact.
- Light strips should be path-based, not many separate boxes.

Useful future atomic tools:
- `text_to_polygon_brushes`
- `facade_panel_batch`
- `sign_box_batch`
- `light_strip_from_path`

## 10. Cave Ruin Hybrid

Scene goal: A natural cave with underground river, stone bridge, ancient altar, torch points, and broken platforms.

Atomic stress points:
- Organic cave shell plus man-made structures.
- Route metadata and selection for review.
- Bridges, platforms, and ledges with intentional movement flow.
- Screenshot-based review of readability.

Screenshot acceptance:
- Cave and ruin elements are both recognizable.
- River/void, bridge, altar, and route are spatially clear.
- Broken platforms look intentional and remain traversable if required.
- Natural walls do not look like a rectangular room with bumps.

Likely MCP gaps:
- Organic cave wall generation needs better primitives.
- Static route analyzers should not over-constrain AI judgement.
- Need viewport camera control for repeatable screenshot review.

Useful future atomic tools:
- `organic_wall_from_height_grid`
- `platform_polygon_batch`
- `route_metadata_set`
- `viewport_camera_frame_bounds`
- `viewport_capture_scene_review`

## Iteration Log

Add one section per test run:

```text
Date:
Scene:
Build phases:
MCP tools used:
Screenshots:
What worked:
What failed:
Tool/context bottlenecks:
Proposed MCP changes:
Commit:
```

### 2026-06-27 - Scene 1: Mountain Valley Boardwalk

Build phases:
- `mcp-op-1` / `MCP Scene 1: canyon terrain massing`: canyon walls, river ribbon, and cave mouth massing. Result: `valid=true`, 20 brushes.
- `mcp-op-2` / `MCP Scene 1: boardwalk and bridge route`: curved boardwalk ribbon, bridge deck, and plank markers. Result: `valid=true`, 18 brushes.
- `mcp-op-3` / `MCP Scene 1: railings and supports`: side rails, support posts, bridge supports, and rail caps. Result: `valid=true`, 34 brushes.

MCP tools used:
- `blockout_create_batch` with `path_ribbon`, `prism`, and `box` operations.
- `selection_filter` scoped to the Scene 1 bounds.
- `viewport_capture_scene_review` with `objectIds`, `highlight=false`, and `clearSelectionBeforeCapture=true`.
- `map_validate` and `problems_check`.
- `documents_save` on the ignored local iteration map `build-release-codex/app/TrenchBroom/map_test/mcp_scene_iterations.map`.

Screenshots:
- Current/window: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782500834093.png`
- 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782500834182.png`
- 2D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782500834231.png`

What worked:
- The batch IR path was compact enough: 52 operations produced 72 new scene brushes across three transactions without direct plane editing.
- The 2D review clearly shows a continuous boardwalk/bridge route running between two irregular canyon edges.
- The new scene review flags reduced screenshot clutter by allowing focus without persistent selection/highlight overlays.

What failed:
- The 3D clean screenshot reads as a long solid rectangular/cliff block more than an open canyon; the boardwalk is hidden by the high rock mass from the automatic angle.
- The selected/highlighted screenshot showed the route, but 73 highlighted objects made the image too noisy for reliable visual review.
- `map_validate` returned four pre-existing empty-property warnings on `worldspawn` / `light_environment`; they are not Scene 1 brush geometry failures, but they still pollute automated pass/fail summaries.

Tool/context bottlenecks:
- `viewport_capture_scene_review` can focus bounds, but still lacks explicit orbit/elevation/free-camera framing, so a scene can be technically focused but visually occluded.
- Organic canyon walls are currently made from tall prisms, which produces boxy silhouettes and hides interior detail. The agent needs lower-level terrain/profile primitives, not a canyon prefab.
- Repeated posts/rails are still verbose in IR. A generic `repeat_along_path` or `support_posts_under_path` would reduce script complexity while staying atomic.
- Validation output needs a way to distinguish baseline/pre-existing map warnings from warnings introduced by the latest operation or selected scene bounds.

Proposed MCP changes:
- Implemented this run: `viewport_capture_scene_review.highlight` and `clearSelectionBeforeCapture` so review can focus objects without selection/overlay clutter.
- Next useful primitives: `viewport_camera_frame_bounds` or `viewport_orbit_capture`, `terrain_profile_wall_batch`, `repeat_along_path`, `support_posts_under_path`, and scoped `problems_check` / baseline warning diff.

Commit:
- `Improve MCP scene review capture options`

### 2026-06-27 - Scene 1 Follow-up: Camera-Controlled Review

Build phases:
- No new map geometry. This run improved screenshot automation after the first Scene 1 review proved that focus-only 3D captures could be occluded by large exterior brush massing.

MCP tools used:
- `viewport_camera_frame_bounds` to orbit the visible 3D camera around explicit bounds or selected object bounds.
- `viewport_camera_set` to place the 3D camera at an explicit `position` looking at a `target`.
- `viewport_capture_scene_review` with the new `camera` object, `highlight=false`, and `clearSelectionBeforeCapture=true`.
- `tb_tools_search(detail=schema)` to verify both camera tools are visible in the `Modeling` profile.

Screenshots:
- Orbit bounds 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782501975992.png`
- Narrow route orbit 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782502002279.png`
- Interior look-at 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782502304964.png`
- Interior look-at 2D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782502305024.png`

What worked:
- `viewport_camera_frame_bounds` gives repeatable orbit camera placement and returns the actual camera position, direction, target, and distance.
- `viewport_camera_set` solves the interior/canyon/cave case where an orbit camera sees only the exterior shell.
- The interior look-at screenshot made the boardwalk, support posts, rail band, and canyon wall spacing visible enough for AI review.

What failed:
- Orbiting combined object bounds can still frame unrelated or oversized massing if selection/bounds are too broad.
- Scene 1 geometry still reads more like a boxed trench or industrial corridor than a natural mountain canyon because the cliff walls are tall prisms and the top/exterior mass dominates.
- Explicit camera placement currently relies on the agent choosing good positions; there is no automatic multi-view plan or occlusion scoring yet.

Tool/context bottlenecks:
- Automated review needs a small multi-camera preset or caller convention: exterior orbit, route/interior look-at, and top/2D plan.
- Scene selection should be scoped by operation/metadata where possible, not broad world bounds.
- Natural terrain still needs lower-level profile/terrain primitives, not a canyon prefab.

Proposed MCP changes:
- Implemented this run: `viewport_camera_frame_bounds`, `viewport_camera_set`, and `viewport_capture_scene_review.camera`.
- Next useful primitives remain `terrain_profile_wall_batch`, `repeat_along_path`, `support_posts_under_path`, and scoped `problems_check` / baseline warning diff.

Commit:
- `Add MCP camera controls for scene review`

### 2026-06-27 - Scene 2: Coastal Lighthouse And Breakwater

Build phases:
- `mcp-op-1..6`: lighthouse island base, stacked tower cylinders, lantern room, and cap. Result: 6 cylinder brushes from atomic `brush_create_cylinder`.
- `mcp-op-7` / `MCP Scene 2: service deck rails and straight dock`: service deck cross beams, rail blocks, dock deck, posts, and small end platform. Result: `valid=true`, 14 brushes.
- `mcp-op-8` / `MCP Scene 2: breakwater rocks and markers`: curved breakwater ribbon, rock blocks, dock end rails, beacon markers, and lantern details. Result: `valid=true`, 25 brushes from 18 operations.
- `mcp-op-9`: translated all Scene 2 operation objects back inside GoldSrc soft map bounds after the initial placement at `x=7000+` produced out-of-bounds warnings.

MCP tools used:
- `brush_create_cylinder` for lighthouse round tower parts.
- `blockout_create_batch` with `box` and `path_ribbon` operations for dock, rocks, rail bands, and breakwater.
- `operation_inspect(detail=ids)` plus `objects_transform` to move the generated module as one scene.
- `map_validate`, `problems_check`, `documents_save`.
- `viewport_capture_scene_review` with explicit `camera.position` / `camera.target` for visual review.

Screenshots:
- 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782502762644.png`
- 2D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782502762715.png`
- Focused 3D after `viewport_layout_set(onePane)`: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782503155480.png`

What worked:
- The lighthouse silhouette is immediately readable: stacked cylindrical tower, wider lantern deck, cap, cross service platform, and dock connection.
- `path_ribbon` produced a curved breakwater without many individual box calls.
- `viewport_camera_set` made it easy to choose a useful 3D review angle instead of fighting the current editor camera.
- Operation history was usable enough to collect generated object ids and move a complete scene module after validation found a placement issue.
- I could open the captured PNGs locally and visually verify the scene without reading the `.map` file: the lighthouse tower, dock, and breakwater were recognizable in the 3D screenshot.

What failed:
- `brush_create_cylinder` has no batch mode, so the tower required six separate atomic MCP calls and six undo steps.
- Cylinder brushes generated non-integer vertex warnings in `problems_check`; this is probably from circular vertex math and needs a snap/grid strategy.
- Initial scene placement outside the soft map bounds produced many warnings before translation; scene iteration needs a documented safe placement envelope or a map occupancy helper.
- Bounds selection by broad region overlapped Scene 1 after translation, proving that spatial selection alone is unsafe for multi-scene iteration. Operation ids or metadata should be preferred.
- 2D screenshot showed only a partial plan view; scene review still needs a multi-camera or framed 2D bounds mode.
- The first two-pane 3D capture included the adjacent 2D grid pane in the image. Switching to `onePane` produced a cleaner 3D capture, but a large neighboring brush mass still entered frame, so screenshot review needs stronger operation-scoped isolation or camera obstacle awareness.

Tool/context bottlenecks:
- Missing generic `brush_create_cylinders_batch` or support for solid-cylinder operations in `blockout_create_batch`.
- Need `snapMode` / `grid` handling for circular primitives so generated vertices can avoid non-integer warnings when desired.
- Need a safe-space query such as `map_find_empty_bounds` or at least a documented soft-bounds-aware placement helper.
- Need operation-scoped selection/review flows to avoid long id arrays and accidental cross-scene bounds overlap.

Proposed MCP changes:
- Implemented follow-up: add generic `cylinder` support to `blockout_create_batch`, not a lighthouse-specific helper.
- Add or expose circular primitive snap controls that can prefer integer/grid vertices over perfect radius.
- Improve scene review with operation ids directly, for example `viewport_capture_scene_review.operationIds`.
- Add scoped/baseline problem filtering so Scene 2 warnings can be distinguished from pre-existing worldspawn/light_environment warnings.

Commit:
- Pending follow-up: `Add batch cylinder blockout operation`.

### 2026-06-27 - Scene 3: Chinese Courtyard

Clean map:
- `build-release-codex/app/TrenchBroom/map_test/mcp_scene_03_chinese_courtyard.map`
- Created by copying the minimal Valve map fixture, then launching TrenchBroom directly with this map. Initial verification: `brushCount=0`, empty MCP history.
- `documents_open` from the previous scene map to this empty map made TrenchBroom exit / MCP return `502`; the stable workflow is command-line launch per scene.

Build phases:
- `mcp-op-1` / `MCP Scene 3: courtyard walls moon gate and pool`: foundation, courtyard walls, stepped moon-gate approximation, pool border, water depression, and main paving. Result: `valid=true`, 26 brushes.
- `mcp-op-2` / `MCP Scene 3: pavilion columns and roof`: pavilion plinth, four batched cylinder columns, beams, stepped roof, ridge, and trim. Result: `valid=true`, 19 brushes.
- Failed attempt before `mcp-op-3`: one oversized batch mixed walkway columns, rock garden, and details. A PowerShell IR expression produced malformed cylinder max values, and batch validation rejected the whole operation without committing brushes.
- `mcp-op-3` / `MCP Scene 3: covered walkway columns`: north/east covered walkway strips, repeated batched cylinder columns, beams, and ridge lines. Result: `valid=true`, 18 brushes.
- `mcp-op-4` / `MCP Scene 3: rock garden plaques and benches`: convex prism rocks, stepping stones, entrance plaques, and pool benches. Result: `valid=true`, 12 brushes.

MCP tools used:
- `blockout_create_batch` with `box`, `prism`, and the newly added `cylinder` operation.
- `map_snapshot`, `history_list`, `map_validate`, `problems_check`, and `documents_save`.
- `viewport_capture_scene_review` with orbit bounds and explicit look-at cameras.
- `viewport_layout_set(onePane)` for clean detail screenshots, then restored to `twoPanes`.

Screenshots:
- Overview 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782544557520.png`
- Overview 2D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782544557569.png`
- Pavilion/detail 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782544570688.png`
- Moon gate front 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782544622833.png`

What worked:
- The clean-map workflow made validation and bounds unambiguous: final `brushCount=75`, `entityCount=0`, bounds `[-1456,-896,-16]` to `[1456,848,368]`.
- The scene reads as a bounded courtyard with central paving, pool, pavilion, covered walkway, rock garden, entrance plaques, and benches.
- `blockout_create_batch` with `cylinder` significantly reduced column spam: pavilion and walkway columns were created inside two transactions instead of many atomic cylinder calls.
- Splitting the final details into two smaller batches let the stable walkway geometry commit even after the mixed batch revealed a malformed operation.

What failed:
- The moon gate reads as a stepped arch, not a true round opening. This is the expected limitation from lacking a generic arch/opening primitive.
- The roof is a stepped block approximation rather than sloped roof brushwork; it is readable from a distance but crude in close-up.
- `map_validate` / `problems_check` returned 16 warnings for non-integer vertices, all from cylinder columns. The batch cylinder support inherited the same circular-grid issue seen in Scene 2.
- 2D scene review still captured only a partial plan instead of framing the full scene bounds.
- The failed mixed batch returned only `operations[2]: max must contain exactly three numbers`; it did not report how many previous operations had compiled successfully before rollback or include a compact operation preview.

Tool/context bottlenecks:
- Need a generic arch/opening primitive, such as an arch wall segment or cylinder-sector-based arch frame, not a Chinese courtyard prefab.
- Need circular primitive snap controls for `cylinder` similar in spirit to `cylinder_sector.snapMode`, or a way to request grid-safe/octagonal columns.
- Need better batch failure diagnostics so generated IR can be debugged without manually opening temp JSON.
- Need framed 2D capture by bounds or operation id for plan-view scene review.
- Need a stable per-scene launch/create-clean-map helper. `documents_open` as a document switch is not robust enough for this workflow.

Proposed MCP changes:
- Implemented follow-up: improve `blockout_create_batch` validation diagnostics with failed operation index/type, successfully compiled operation count before rollback, and a compact operation preview.
- Later: add `arch_opening_from_wall` or a lower-level `brush_create_arch_frame` primitive.
- Later: add `viewport_capture_scene_review` support for operation ids and bounds-framed 2D capture.
- Later: add circular primitive snap/grid controls for `cylinder`.

Commit:
- Pending follow-up: `Improve batch blockout failure diagnostics`.

### 2026-06-27 - Scene 4: Abandoned Factory

Clean map:
- `build-release-codex/app/TrenchBroom/map_test/mcp_scene_04_abandoned_factory.map`
- Created from the minimal Valve map fixture and launched directly with `scripts/mcp-call.ps1 -Launch -KeepOpen -MapPath ...`.
- Initial verification: `brushCount=0`, empty MCP history.

Build phases:
- `mcp-op-1` / `MCP Scene 4: factory shell and damaged walls`: floor slab, segmented long walls, loading door gaps, damaged openings, and debris prisms. Result: `valid=true`, 23 brushes.
- `mcp-op-2` / `MCP Scene 4: roof trusses and partial roof`: repeated roof trusses, side roof strips, and a central ridge. Result: `valid=true`, 24 brushes.
- `mcp-op-3` / `MCP Scene 4: platforms stairs crane rails`: side mezzanines, stairs, ramp, crane rails, trolley block, guard rails. Result: `valid=true`, 35 brushes from 21 operations.
- `mcp-op-4` / `MCP Scene 4: pipes tanks machinery and lights`: tanks, horizontal pipe runs, elbows, vents, machinery blocks, cable tray, and hanging light blocks. Result: `valid=true`, 20 brushes.

MCP tools used:
- `blockout_create_batch` with `box`, `prism`, `stairs`, `ramp`, and `cylinder` operations.
- `map_snapshot`, `map_validate`, `problems_check`, `history_list`, and `documents_save`.
- `viewport_capture_scene_review` with an overview orbit camera and an interior look-at camera.

Screenshots:
- Overview 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782545252226.png`
- Overview 2D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782545252276.png`
- Interior 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782545263995.png`

What worked:
- The factory reads as an abandoned industrial hall: long shell, punched wall openings, roof trusses, side platforms, stairs, crane rails, and machinery blocks are all visible.
- Stairs/ramp/platforms stayed coherent and were created in one transaction for the main platform phase.
- The direct clean-map launch flow was stable for this scene.
- Batch failure diagnostics from the previous improvement were useful when checking malformed generated IR: invalid batches can now report the failing operation without committing partial geometry.

What failed:
- Repeating trusses, posts, guard rails, and light blocks required host-side loops; PowerShell array-expression pitfalls repeatedly dropped intended brush operations. The map remained valid, but authoring friction was high.
- Damaged walls are still built by composing many rectangular wall segments around holes; there is no generic cut/opening primitive for true wall damage.
- Pipes/tanks created with `cylinder` still produced four non-integer-vertex warnings.
- Interior screenshot shows platforms and openings clearly, but pipe/machinery detail is partly hidden behind the exterior shell; automated review still needs better interior multi-view presets.
- 2D capture remains partially framed rather than full-scene framed.

Tool/context bottlenecks:
- Need a generic repeat/array operation inside Batch IR so the agent can repeat any low-level operation without external script loops or huge JSON.
- Need cut/opening primitives for windows, loading doors, and damaged wall holes.
- Need pipe-from-path or repeat-along-path primitives for industrial pipe runs, but as generic path geometry, not a factory prefab.
- Need circular primitive snap/grid controls for cylinder.

Proposed MCP changes:
- Implemented follow-up: added generic `repeat_translate` to `blockout_create_batch` so one child operation can be repeated by count and offset inside the same transaction and validation path.
- Later: add generic `brush_cut_opening` / `opening_from_wall_segments` support.
- Later: add path-based pipe/beam helpers, e.g. `path_tube_segments` or `repeat_along_path`.
- Later: add bounds-framed 2D scene capture.

Follow-up validation:
- Unit/focused tests: `TbMcpLibTest "McpToolCatalog"` and `TbUiLibTest "McpBridgeServer batch blockout tools"`.
- Release app build: `cmake --build build-release-codex --target TrenchBroom --config Release --parallel`.
- Real MCP smoke: launched `mcp_repeat_translate_smoke.map`, confirmed `tb_tools_search` exposes the `repeat_translate` schema, created four repeated boxes in one `blockout_create_batch` operation, then used `history_undo_mcp` and verified the map returned to `brushCount=0`.

Commit:
- `a1e0c4514 Add repeat translate batch operation`.

### 2026-06-27 - Scene 5: Underground Metro Station

Clean map:
- `build-release-codex/app/TrenchBroom/map_test/mcp_scene_05_metro_station.map`
- Recreated from the minimal Valve map fixture after the first trial exposed invalid stair divisions.
- Initial verification: `brushCount=0`.

Build phases:
- `mcp-op-1` / `MCP Scene 5: station platforms tracks and shell`: floor slab, twin side platforms, central track trench, rails, exterior shell, end tunnel mouths, concourse blocks, and stairs. Final result: `valid=true`, 33 brushes.
- `mcp-op-2` / `MCP Scene 5: repeated columns lights signs and sleepers`: repeated platform columns, ceiling light bars, track sleepers, sign boards, small route markers, and end portal blocks. Result: `valid=true`, 83 brushes from 13 operations.
- `mcp-op-3` / `MCP Scene 5: tunnel arch cues gates and route markers`: stepped tunnel arch cues, ticket gates, concourse sign blocks, platform direction markers. Result: `valid=true`, 35 brushes.

MCP tools used:
- `blockout_create_batch` with `box`, `stairs`, `prism`, and `repeat_translate`.
- `map_snapshot`, `map_validate`, `problems_check`, `history_list`, `documents_save`.
- `viewport_capture_scene_review` with `twoPanes` overview and explicit interior camera.

Screenshots:
- Overview 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782547070957.png`
- Overview 2D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782547071009.png`
- Exterior/detail 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782547071091.png`
- Interior 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782547114768.png`

Validation:
- Final `map_snapshot`: `brushCount=151`, bounds `[-1792,-672,-32]` to `[1792,672,256]`, saved with `modified=false`.
- Final `map_validate`: `valid=true`, `count=0`, `safeFixableCount=0`.
- `history_list`: three live operations, all `valid=true`, `mismatchCount=0`, `staleObjectCount=0`.

What worked:
- `repeat_translate` was immediately useful: 13 operations created 83 repeated columns, lights, sleepers, and signs without host-side loops or giant object-id lists.
- `twoPanes` scene review produced usable 2D and 3D screenshots at full height; the previous four-pane 2D capture issue did not reproduce.
- The 2D overview clearly reads as an underground station: two platforms, central track pair, sleepers, end tunnel mouths, stairs, ticket hall, and repeated columns.
- Explicit inside-the-room camera control can verify enclosed spaces when exterior orbit shots are occluded by the shell.

What failed or constrained the agent:
- First trial used 6 steps across a 256-unit run, which produced `runStep=42.6667` and 12 `Brush has non-integer vertices` warnings. MCP allowed it at creation time, so the issue was only discovered by `map_validate`.
- Initial attempt to fix this by requiring stairs to align to the active grid was too strict: `riseStep=24` is a valid integer unit height even if the grid is 16. The correct generic rule is integer-unit vertices, not grid-only stair increments.
- Tunnel arches are still only hinted by stepped boxes and small prisms; there is no generic arched tunnel section primitive.
- Exterior orbit screenshots of enclosed scenes are poor for review. They show the shell, but not station usability.

Implemented MCP follow-up:
- Added `stairs` validation in the batch compiler: stair run/rise per step must be integer map units. Invalid stair divisions now fail before commit and return `failedOperationIndex`, `failedOperationType`, and a concrete run/rise diagnostic.
- Real MCP smoke verified the failing Scene 5 stair case returns `valid=false` with no committed brushes, then the final Scene 5 rebuild passed `map_validate` with zero warnings.

Proposed MCP changes:
- Add generic `tunnel_section_batch` / `arch_section_from_span` for arched ceilings and tunnel portals.
- Add an interior review helper or camera preset that can frame inside enclosed shell geometry without manual camera coordinates.
- Add optional `ensureIntegerVertices=true` validation mode for other batch operations that can produce fractional vertices.

Commit:
- `f35427ed0 Reject fractional stair batch geometry`.

### 2026-06-27 - Scene 6: Desert Ruin Temple

Clean map:
- `build-release-codex/app/TrenchBroom/map_test/mcp_scene_06_desert_ruin_temple.map`
- Created from the minimal Valve map fixture and launched directly with `scripts/mcp-call.ps1 -Launch -KeepOpen -MapPath ...`.
- Initial verification: `brushCount=0`.

Build phases:
- `mcp-op-1` / `MCP Scene 6: desert base stepped temple and entrance route`: large sand base, coarse dune prisms, five-layer temple massing, front stair route, and rear underground entrance volume. Result: `valid=true`, 29 brushes.
- `mcp-op-2` / `MCP Scene 6: columns broken doors and fallen stones`: repeated column rows, broken lintels, doorway pieces, and fallen stone blocks. Result: `valid=true`, 36 brushes.
- `mcp-op-3` / `MCP Scene 6: dunes transition markers and route readable edges`: additional dune transition prisms, side ramps, route edge markers, and rear entrance framing. Result: `valid=true`, 34 brushes.

MCP tools used:
- `blockout_create_batch` with `box`, `prism`, `ramp`, `stairs`, and `repeat_translate`.
- `map_snapshot`, `map_validate`, `history_list`, `documents_save`.
- `viewport_capture_scene_review` with overview, front entrance, and rear entrance cameras.

Screenshots:
- Overview 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782547419017.png`
- Overview 2D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782547419072.png`
- Front entrance 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782547419125.png`
- Rear underground entrance 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782547419297.png`

Validation:
- Final `map_snapshot`: `brushCount=99`, bounds `[-2048,-1536,-32]` to `[2048,1536,320]`, saved with `modified=false`.
- Final `map_validate`: `valid=true`, `count=0`, `safeFixableCount=0`.
- `history_list`: three live operations, all `valid=true`, `mismatchCount=0`, `staleObjectCount=0`.

What worked:
- The front entrance screenshot reads as a stepped ruin temple: central stair, columns, broken doorway pieces, fallen blocks, and layered massing are visible.
- Validation stayed clean with no non-integer vertices or safe-fixable problems.
- `repeat_translate` again reduced repeated columns and small route markers.
- Explicit detail cameras are more useful than a single overview for judging route hierarchy and broken doorway intent.

What failed or constrained the agent:
- The five-layer temple mass had to be manually described as five boxes. This is not a prefab issue; it is a missing generic stepped massing primitive useful for many scenes.
- Sand dunes made from a few prism/ramp brushes read as coarse raised slabs. Terrain remains too blocky for natural scenes.
- Broken stones are still hand-placed boxes/prisms. There is no controlled chip/chamfer primitive for damaged stone edges.
- The 2D overview was not a useful top-down scene review; it mostly showed large rectangle outlines and missed much of the readable detail.
- In a real MCP smoke after undoing a `stepped_mass` operation, `map_snapshot` correctly returned to `brushCount=0`, but `operation_validate` still reported the undone operation as live. That suggests operation validity can lag or mismatch after undo in this path.

Implemented MCP follow-up:
- Added generic `stepped_mass` to `blockout_create_batch`: creates `levels` stacked rectangular massing brushes with an xy `inset` per level and `stepHeight` per level. It is useful for temples, plinths, arenas, terraces, stepped terrain, and other layered forms without becoming a scene prefab.
- Added schema examples/properties for `stepped_mass`.
- Added tests for valid creation and collapsed-level rejection.
- Real MCP smoke verified schema discovery, creation of a 5-layer stepped mass, `map_validate valid=true`, and undo returning the map to `brushCount=0`.

Proposed MCP changes:
- Add generic controlled damage tools such as `edge_chip_batch` or `brush_chamfer_batch` for ruin/broken-stone work.
- Improve 2D scene review by forcing a true top view with fitted content and useful layer/depth behavior.
- Investigate `operation_validate` after undo so operation live/stale state matches the actual document state immediately.
- Add terrain-specific primitives or adaptive heightfield tools for natural dunes, still as generic terrain/mesh tools rather than desert-specific helpers.

Commit:
- `9adbfa91f Add stepped mass batch operation`.

### 2026-06-27 - Scene 7: Snow Mountain Research Station

Clean map:
- `build-release-codex/app/TrenchBroom/map_test/mcp_scene_07_snow_research_station.map`
- Created from the minimal Valve map fixture and launched directly with `scripts/mcp-call.ps1 -Launch -KeepOpen -MapPath ...`.
- Initial verification: `brushCount=0`.

Build phases:
- `mcp-op-1` / `MCP Scene 7: snow slope terrain and station modules`: stepped snow terrain, raised module foundations, main station blocks, bridge corridor massing, and landing pads. Result: `valid=true`, 20 brushes.
- `mcp-op-2` / `MCP Scene 7: supports antennas bridge details and ice cave`: bridge supports, antenna silhouettes, rail bands, equipment blocks, and a first-pass ice cave opening. Result: `valid=true`, 35 brushes.
- `mcp-op-3` / `MCP Scene 7: rails equipment snow banks and route markers`: additional rails, snow banks, equipment crates, route markers, and edge details. Result: `valid=true`, 63 brushes.

MCP tools used:
- `blockout_create_batch` with `box`, `prism`, `ramp`, `repeat_translate`, and related batch operations.
- `map_snapshot`, `map_validate`, `history_list`, and `documents_save`.
- `viewport_capture_scene_review` with overview and detail cameras.
- Local screenshot inspection of the generated PNGs with visual review.

Screenshots:
- Overview 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782548392346.png`
- Overview 2D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782548392409.png`
- Modules and bridge detail 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782548392482.png`
- Ice cave and supports 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782548392736.png`

Validation:
- Final `map_snapshot`: `brushCount=118`, bounds `[-2048,-1536,-64]` to `[2048,1536,912]`, saved with `modified=false`.
- Final `map_validate`: `valid=true`, `count=0`, `safeFixableCount=0`.
- `history_list`: three live operations, all `valid=true`, `mismatchCount=0`, `staleObjectCount=0`.

What worked:
- The overview reads as a modular station placed on a large stepped snow slope.
- The detail camera shows a believable hard-surface research base: bridge corridor, module blocks, rail bands, support posts, antennas, and equipment are all legible in whitebox.
- Validation stayed clean after saving the map.
- Splitting terrain/modules, supports/details, and rails/equipment into separate operations kept operation history readable and reduced the risk of one malformed detail blocking the main massing.

What failed or constrained the agent:
- Bridge and platform supports still required manual coordinate estimates. The agent had to calculate support centers and heights rather than asking MCP to connect a deck to a known bottom/top surface.
- The ice cave reads only moderately; it looks closer to a rectangular portal and stepped cut than an organic ice opening.
- Snow terrain remains blocky. The existing batch primitives can express slopes and terraces, but not adaptive smoothness or natural contact.
- The 2D screenshot still did not produce a reliable full-scene top review. It captured useful local outlines, but not a consistently fitted plan view.

Implemented MCP follow-up:
- Added generic `support_posts_between` to `blockout_create_batch`. It creates square post brushes from `points2d`, `bottomZ`, `topZ`, and `postSize` in one transaction. This is useful for bridges, boardwalks, raised modules, platforms, catwalks, docks, and industrial structures without becoming a snow-station prefab.
- Added schema examples/properties for `support_posts_between`.
- Added tests for valid multi-post creation and invalid Z ranges.
- Real MCP smoke verified schema discovery, creation of three support posts, `map_validate valid=true`, and undo returning the smoke map to `brushCount=0`.

Proposed MCP changes:
- Add `support_posts_under_path` or a contact-aware support helper that can derive per-post bottom Z from terrain or selected surfaces.
- Add `geometry_contact_analyze` so the agent can check whether supports touch both terrain and deck without relying on screenshots.
- Improve natural terrain and cave tools with adaptive mesh/profile primitives, while keeping them generic.
- Improve 2D scene review with true top-view fitted capture by bounds or operation id.

Commit:
- `13ddcdce4 Add support posts batch operation`.

### 2026-06-27 - Scene 8: European Castle Wall

Clean map:
- `build-release-codex/app/TrenchBroom/map_test/mcp_scene_08_european_castle_wall.map`
- Created from the minimal Valve map fixture, then opened through MCP `documents_open`.
- Initial verification: `brushCount=0`. The MCP history still contained stale/undone records from the prior smoke map, so this run used the active document snapshot and Scene 8 transaction names as the authoritative evidence.

Build phases:
- `mcp-op-2` / `MCP Scene 8: castle ground curtain walls towers and gatehouse`: ground slab, curtain wall ribbons, four corner towers, central gatehouse, and inner courtyard base. Result: `valid=true`, 18 brushes.
- `mcp-op-3` / `MCP Scene 8: battlements wall walks and tower crowns`: first attempt at wall walks and crenellations. Result: created 100 brushes, then immediately undone because one `repeat_translate` operation accidentally used `offset=[0,0,0]`, producing overlapping duplicate tower crown brushes.
- `mcp-op-4` / `MCP Scene 8: battlements wall walks and tower crowns`: corrected battlements, wall walks, tower top slabs, and gatehouse crenellations. Result: `valid=true`, 86 brushes.
- `mcp-op-5` / `MCP Scene 8: gate opening stairs courtyard and markers`: gate void markers, stairs to wall walks, interior keep marker, courtyard blocks, and route/flag markers. Result: `valid=true`, 35 brushes.

MCP tools used:
- `blockout_create_batch` with `path_ribbon`, `box`, `cylinder`, `stairs`, `ramp`, `prism`, and `repeat_translate`.
- Hidden but searchable `documents_open` / `documents_save` to switch from the smoke map to a clean scene map and save the result.
- `map_snapshot`, `map_validate`, `problems_check`, and `history_list`.
- `viewport_capture_scene_review` with an orbit overview plus explicit gatehouse and wall-walk cameras.

Screenshots:
- Overview 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782549677880.png`
- Overview 2D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782549677941.png`
- Gatehouse detail 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782549678337.png`
- Battlement wall-walk detail 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782549678515.png`

Validation:
- Final `map_snapshot`: `brushCount=139`, bounds `[-1808,-1200,-32]` to `[1808,1200,464]`, saved with `modified=false`.
- Final `map_validate`: `valid=false`, `count=5`, `safeFixableCount=5`.
- `problems_check`: five `Brush has non-integer vertices` warnings, all on the 12-sided cylinder tower brushes (`mcp:3:8` through `mcp:3:12`). These are known circular primitive grid warnings, not random Scene 8 placement failures.
- Scene 8 live operations after correction: `mcp-op-2`, `mcp-op-4`, and `mcp-op-5` all reported `valid=true`; the bad `mcp-op-3` is retained in history as `undone=true`.

What worked:
- The gatehouse screenshot reads clearly as a castle wall: central doorway, heavy curtain wall, crenellations, corner towers, and flanking walls are visible.
- The wall-walk detail shot shows usable parapet rhythm and a readable top route along the wall.
- `repeat_translate` kept crenellations and repeated wall markers compact enough to author and review.
- The corrected three-phase build made undo and history audit practical: the bad battlement attempt was isolated and removed without touching the main wall massing.

What failed or constrained the agent:
- A zero-offset `repeat_translate` accidentally generated duplicate overlapping tower crown brushes. The batch compiler accepted it because the individual boxes were valid. This is dangerous because visual screenshots may not reveal exact duplicate overlap.
- Castle gates still had to be represented by manually splitting wall/gatehouse solids and leaving an opening. There is no generic arch/opening/cut primitive yet.
- Walls along a rectangular path required separate `path_ribbon` operations per side plus hand-placed tower intersections. A generic wall-from-polyline helper would be useful if it stays low-level and path-based.
- Round towers using `cylinder` still created non-integer vertex warnings even with grid snapping. For grid-clean castle towers, an explicit polygonal tower footprint or stronger cylinder snap behavior is needed.
- `documents_open` worked this time, but operation history persisted stale entries from the previous document. Scene tests should not equate non-empty history with a dirty scene.

Implemented MCP follow-up:
- Added validation for `repeat_translate`: when `count > 1`, `offset` must be non-zero. This prevents accidental exact duplicate brush generation inside batch IR.
- Updated schema text for `repeat_translate.offset` to document the rule.
- Added focused `TbUiLibTest` coverage for zero-offset rejection.
- Real MCP smoke verified a zero-offset repeat returns `valid=false` with `failedOperationType=repeat_translate` and leaves the clean smoke map at `brushCount=0`.

Proposed MCP changes:
- Add a generic `wall_from_polyline` or `path_wall_segment_batch` that creates straight wall runs from a centerline with height/thickness, but still returns ordinary brush geometry rather than a castle prefab.
- Add a generic `crenellation_repeat` or `repeat_along_polyline` for repeated objects along straight and turning paths.
- Add `arch_opening_from_wall` / `opening_from_wall_segments` for gates, tunnels, and windows.
- Fix or replace grid-safe circular primitives so polygonal towers can avoid non-integer vertex warnings without manual prism footprints.
- Consider an overlap/duplicate-bounds validator for batch results, because duplicated brushes are easy for agents to create and hard to spot in screenshots.

Commit:
- `5000ea9bd Reject zero-offset repeat translate operations`.

### 2026-06-27 - Scene 9: Cyberpunk Street Corner

Clean map:
- `build-release-codex/app/TrenchBroom/map_test/mcp_scene_09_cyberpunk_street_corner.map`
- Created from the minimal Valve map fixture.
- First attempted to switch from the prior smoke map with `documents_open`; it returned success, but the next MCP calls timed out and the TB process exited. The stable path was to relaunch TrenchBroom directly with this clean map.
- Initial verification after direct launch: `brushCount=0`, empty MCP history.

Build phases:
- `mcp-op-1` / `MCP Scene 9: street corner roads buildings and skybridge massing`: L-shaped street slabs, four building masses, corner storefront bands, and initial skybridge massing. Result: `valid=true`, 21 brushes.
- `mcp-op-2` / `MCP Scene 9: facade windows signs and service boxes`: repeated facade window panels, sign boxes, store signs, and service/vent boxes. Result: `valid=true`, 95 brushes from 22 operations.
- `mcp-op-3` / `MCP Scene 9: skybridge light strips street props and anchors`: skybridge details, street median/props, vertical light-strip cues, arrow markers, and bridge support posts. Result: `valid=true`, 62 brushes.

MCP tools used:
- `blockout_create_batch` with `box`, `prism`, `repeat_translate`, and `support_posts_between`.
- `map_snapshot`, `map_validate`, `problems_check`, `history_list`, and `documents_save`.
- `viewport_capture_scene_review` with overview and two explicit detail cameras.

Screenshots:
- Overview 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782550453174.png`
- Overview 2D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782550453227.png`
- Street corner detail 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782550453708.png`
- Skybridge/facade detail 3D: `C:\Users\Trh\AppData\Local\Temp\TrenchBroomMCP\viewport-1782550467180.png`

Validation:
- Final `map_snapshot`: `brushCount=178`, bounds `[-1600,-1600,-32]` to `[1600,1600,896]`, saved with `modified=false`.
- Final `map_validate`: `valid=true`, `count=0`, `safeFixableCount=0`.
- Final `problems_check`: no problems.
- `history_list`: three live operations, all `valid=true`, `mismatchCount=0`, `staleObjectCount=0`.

What worked:
- The close street view reads as a dense urban street corner: tall facades, storefront bands, signs, vents, street props, and a skybridge are recognizable in whitebox.
- Validation stayed fully clean by avoiding circular primitives in this scene.
- Dense facade detail was possible with existing repeat operations, and the operation history stayed phase-readable.
- The skybridge and support posts created enough vertical layering to distinguish the scene from a plain intersection.

What failed or constrained the agent:
- Window grids and facade panels were awkward: a 2D grid of windows required many separate `repeat_translate` strips instead of one compact grid operation.
- Cyberpunk identity in whitebox depends heavily on signs, text, light strips, and facade density. Without text/label/sign primitives, the scene reads more like a dense sci-fi street than specifically cyberpunk.
- Running multiple MCP screenshot calls in parallel hit the bridge's single-request guard: one capture returned `Forbidden: MCP bridge is already handling another request`. Scene automation should serialize MCP calls or gain a queue.
- `documents_open` again proved less stable than launching a clean map directly, so per-scene command-line launch remains the preferred automation flow.
- Some camera angles were partly occluded by nearby tall building massing. The camera tools work, but automated scene review still needs better view presets or occlusion-aware framing.

Implemented MCP follow-up:
- Added generic `repeat_grid` to `blockout_create_batch`.
- `repeat_grid` repeats any non-repeat child operation over one to three axes using `counts` and `offsets`, with a total instance cap of 4096 and zero-offset rejection for axes with count greater than one.
- Updated schema examples/properties so `tb_tools_search(detail=schema)` shows the payload format.
- Added focused tests for valid 4x3 grid creation and invalid zero-offset grid axes.
- Real MCP smoke verified a 4x3 `repeat_grid` creates 12 brushes in one operation, `map_validate valid=true`, and undo returns the smoke map to `brushCount=0`.

Proposed MCP changes:
- Add a generic sign/text primitive such as `text_to_polygon_brushes` or a lower-level glyph/label brush generator.
- Add facade-oriented helpers that remain atomic, for example `panel_grid_on_plane` or `box_grid_on_plane`, for windows and light panels on vertical faces.
- Add client-side or server-side request queueing for screenshot-heavy automation, or document that MCP callers must serialize requests.
- Add safer scene switching or a `mcp_open_clean_map`/launch helper so `documents_open` instability does not interrupt automated iteration.
- Improve scene review cameras with automatic street/interior presets that avoid nearby occluders.

Commit:
- Pending follow-up: `Add repeat grid batch operation`.
