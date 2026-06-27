# MCP Composite Atomic Validation

This document tracks validation scenes for TrenchBroom MCP composite atomic
tools. The goal is to grow reusable modeling atoms, not scene-specific prefab
generators.

## Principles

- Validate one reusable capability across multiple scene types before treating it
  as a stable MCP primitive.
- Prefer `blockout_create_batch`, `brush_create_polygon_batch`, transform,
  metadata, operation history, and screenshot review over scene-specific helpers.
- Keep generated geometry inspectable, selectable, undoable, saveable, and
  composed of valid convex brushes.
- Use scene screenshots as human review evidence, but judge tool quality by
  whether the same atom transfers to different layouts.
- Keep KZ, racing, castle, industrial, and natural scenes as test contexts only.
  Do not turn them into hardcoded prefab families.

## Common Acceptance Criteria

Every validation scene should record:

- Scenario name and date.
- MCP tools used.
- Operation ids.
- Brush count and rough bounds.
- Whether creation was staged by type or submitted as one batch.
- Whether undo/redo works.
- Whether `map_validate` and `problems_check` pass.
- Whether `operation_validate` reports live objects.
- Screenshot paths or `viewport_capture_scene_review` output.
- Tool friction found during the run.
- Follow-up tool changes, if any.

The scene passes only if:

- The main structure is recognizable from screenshot review.
- Turns, joins, endpoints, and repeated elements are coherent.
- No obvious unwanted rods, gaps, orphan brushes, or floating supports appear.
- Object identity remains usable through `history_list`, `operation_inspect`,
  and `operation_validate`.
- The same tool pattern would plausibly work in at least one other scene type.

## Validation Scenes

### 1. Mountain Road Track

Primary capabilities:

- Path floor generation.
- Curved and segmented turns.
- Edge barriers or guard rails.
- Supports that follow terrain or height changes.
- Width and elevation variation along a route.

Useful tools:

- `blockout_create_batch` with `path_ribbon`, `box`, `prism`,
  `repeat_translate`, and `support_posts_between`.
- `brush_create_polygon_batch` for custom road shoulders or markers.
- `viewport_capture_scene_review`.

Acceptance focus:

- The road surface is continuous through turns.
- Barriers follow both sides without drifting.
- Supports are placed under elevated sections, not protruding through the road.
- The same atoms can be reused for bridges, canyon paths, or KZ routes.

Status: Not run.

### 2. Castle Wall Walk

Primary capabilities:

- Wall strips along a multi-segment path.
- Walkable top surface.
- Corner joins.
- Repeated merlons, crenels, and tower connection points.

Useful tools:

- `path_ribbon` for wall walk floors.
- `repeat_translate` / `repeat_grid` for merlons.
- `brush_create_polygon_batch` for tower footprints and angled corners.

Acceptance focus:

- Wall top is walkable and grid-aligned.
- Corner joins close cleanly.
- Merlon spacing is stable and does not overlap corners.
- Tower connection points are explicit and selectable.

Status: Not run.

### 3. Canyon Wooden Walkway

Primary capabilities:

- Segmented path platforms.
- Railings.
- Posts and braces under irregular path sections.
- Directional readability.

Useful tools:

- `path_ribbon` or polygon batch for deck sections.
- `support_posts_between` for posts.
- `repeat_translate` for planks or railing segments.

Acceptance focus:

- Deck turns are readable and not just box spam.
- Railings stay attached to edges.
- Supports reach the intended base height.
- The path can be split into stages so one invalid decorative piece does not
  block the main walkway.

Status: Not run.

### 4. Underground Pipe Or Sewer

Primary capabilities:

- Cylindrical or half-cylindrical corridor approximations.
- Curved pipe turns.
- Side exits.
- Segment continuity.

Useful tools:

- `brush_create_cylinder_sector`.
- `blockout_create_batch` with `cylinder_sector`, `cylinder`, `path_ribbon`,
  `box`, and `prism`.

Acceptance focus:

- Inner passage remains coherent across segments.
- Side exit connects to the pipe wall cleanly.
- Circular approximation stays grid-safe enough for GoldSrc blockout work.
- No invalid or concave brushes are produced.

Status: Not run.

### 5. Factory Conveyor And Maintenance Platforms

Primary capabilities:

- Repeating mechanical modules.
- Platform chains.
- Railings, support frames, ramps, and stairs as reusable atoms.
- Texture and face operations on repeated elements.

Useful tools:

- `repeat_translate`, `repeat_grid`, `box`, `prism`.
- `brush_create_boxes_batch`.
- Texture and face tools.

Acceptance focus:

- Repetition is compact in MCP calls and operation history.
- Supports and rails line up with the conveyor/platform edges.
- Face/texture edits can target repeated elements without long object id lists.
- One operation can be inspected and selected after creation.

Status: Not run.

### 6. KZ Curved Bhop Route

Primary capabilities:

- Route metadata.
- Non-box platform footprints.
- Curved uphill path composition.
- Player intention expressed through platform shape and orientation.

Useful tools:

- `shape_library_list`.
- `brush_create_polygon_batch`.
- `brush_metadata_set`, `selection_by_metadata`.
- `route_geometry_analyze_chain` for geometric facts only.

Acceptance focus:

- The route communicates takeoff edges and landing windows.
- Platforms are not just a uniform box chain unless explicitly requested.
- Difficulty judgement stays in the Agent skill and human review, not in a
  static MCP verdict.
- Metadata can recover the route without carrying a large object id list.

Status: Not run.

### 7. Temple Steps And Terraces

Primary capabilities:

- Layered stepped masses.
- Symmetric repetition.
- Column rows.
- Axis-aligned and polygonal platform composition.

Useful tools:

- `stepped_mass`.
- `repeat_grid`.
- `brush_create_cylinder`.
- `brush_create_polygon_batch`.

Acceptance focus:

- Terraces read as intentional levels, not random stacked blocks.
- Columns align with route and entrance axes.
- Stairs or ramps connect levels.
- The same atoms can support plazas, fortifications, or arena seating.

Status: Not run.

### 8. Natural Cave Blockout

Primary capabilities:

- Terrain or heightfield surfaces.
- Irregular but convex platform chunks.
- Rock columns and cave openings.
- Adaptive detail in visually complex areas.

Useful tools:

- `heightmap_import_grayscale`.
- `brush_create_polygon_batch`.
- `brush_create_cylinder` / `brush_create_cone`.
- `blockout_create_batch` staged by terrain, openings, and supports.

Acceptance focus:

- The cave silhouette is not purely rectangular.
- Complex regions can use finer cells or smaller polygons.
- Generated brushes remain selectable and saveable.
- Screenshot review can distinguish the route, openings, and major masses.

Status: Not run.

## Priority Queue

1. Mountain Road Track: validates path, barrier, support, and height variation.
2. Castle Wall Walk: validates corner joins, repetition, and walkable thickness.
3. KZ Curved Bhop Route: validates route metadata and player-intent footprints.
4. Underground Pipe Or Sewer: validates curved/circular brush composition.

These four should run first because together they stress path composition,
repetition, curved geometry, object identity, metadata, and screenshot review
without requiring final art materials.

## Findings Log

Add new findings below after each real MCP run.

### Template

- Date:
- Scenario:
- Map:
- Tools used:
- Operation ids:
- Result:
- Screenshot review:
- Problems found:
- Tool changes proposed:
- Tool changes implemented:
- Commit:
