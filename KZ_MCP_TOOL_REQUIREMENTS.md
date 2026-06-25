# KZ MCP Tool Requirements

Goal: give AI low-level mapper controls for CS1.6 KZ route design. Avoid fixed bhop templates; expose shape editing, route intent, effective distance, and readability analysis.

## P0

### `brush_create_polygon_batch`

- Purpose: create many convex prism platforms from 2D polygons in one transaction.
- Inputs: `brushes[{points2d,minZ,maxZ,material,metadata}]`, `grid`, `select`, `transactionName`.
- Output: operation id, created object ids, validation summary.
- Need: enables diamond/trapezoid/chamfered platforms instead of box-only chains.

### `brush_chamfer_or_cut`

- Purpose: cut/chamfer existing brush corners or edges.
- Inputs: `objectIds`, `cuts[{corner|edge, amountX, amountY}]`, `grid`.
- Output: updated object ids, geometry validation.
- Need: lets AI refine box/prism shapes into route-guiding geometry.

### `route_platform_chain_create`

- Purpose: create a route-aware platform chain from waypoint intent, not a fixed template.
- Inputs: `waypoints[{center,shape,size,takeoffDirection,landingBias,heightDelta}]`, `material`, `grid`.
- Output: created object ids, per-segment summary.
- Need: AI controls shape, direction, height, and landing intent per platform.

### `kz_distance_analyze_chain`

- Purpose: analyze continuous bhop effective distance.
- Inputs: `objectIds` or `routeId`, `movementType`, optional `playerHull`.
- Output: `edgeGap`, `effectiveDistanceIdeal`, `effectiveDistanceBadLanding`, `heightDelta`, `lateralOffset`, `landingWindowArea`, warnings.
- Need: difficulty must consider bad landing + next takeoff, not only edge-to-edge gap.

### `platform_affordance_analyze`

- Purpose: check whether platforms guide the player correctly.
- Inputs: `objectIds`, optional `incomingDirection`, `outgoingDirection`, `movementType`.
- Output: readability score, takeoff-edge clarity, landing-window quality, false-affordance warnings, suggestions.
- Need: detects box chains with too many meaningless landing choices.

## P1

### `brush_shape_from_intent`

- Purpose: create one platform from mapper intent.
- Inputs: `intent`, `center`, `size`, `incomingDirection`, `outgoingDirection`, `difficulty`, `landingBias`, `material`.
- Output: created object id, chosen footprint, validation.
- Need: maps intent to diamond/trapezoid/chamfered shapes.

### `route_reflow`

- Purpose: adjust selected route geometry while preserving route endpoints.
- Inputs: `objectIds`, `mode`, `targetDifficulty`, `preserveStartEnd`, `grid`.
- Output: updated object ids, before/after metrics.
- Need: improve readability or difficulty without deleting and recreating everything.

### `landing_window_visualize`

- Purpose: create temporary debug markers for landing/takeoff logic.
- Inputs: `objectIds`, `showIdealLanding`, `showBadLanding`, `showTakeoffEdge`, `duration|persistent`.
- Output: marker object ids.
- Need: makes AI/user see intended landing windows and next-hop edges.

### `edge_mark_or_trim_create`

- Purpose: add thin visual trims/markers along selected edges or faces.
- Inputs: `edgeRefs|faceRefs`, `width`, `height`, `material`, `offset`.
- Output: created marker brush ids.
- Need: visually emphasizes takeoff edges, route direction, and denial zones.

### `view_readability_check`

- Purpose: test route visibility from player spawn or a camera point.
- Inputs: `origin`, `yaw`, `pitch`, `targets`, `checkOcclusion`.
- Output: visible targets, angles, distances, occlusion warnings.
- Need: start view should reveal the first intended action.

## P2

### `shape_library_list`

- Purpose: list supported platform footprint grammars.
- Outputs: `box`, `diamond`, `trapezoid`, `wedge`, `chamfered_rect`, `arrowhead`, `half_hex`, `slanted_plank`, `scene_embedded_ledge`.
- Need: gives AI explicit shape vocabulary.

### `brush_metadata_set`

- Purpose: attach mapper metadata to brushes.
- Inputs: `objectIds`, `metadata{routeId,intent,difficulty,takeoffEdge,landingWindow}`.
- Output: updated ids.
- Need: preserves route intent for later AI edits.

### `brush_metadata_get`

- Purpose: read mapper metadata from brushes.
- Inputs: `objectIds`.
- Output: metadata per object.
- Need: lets AI understand previously created route pieces.

### `selection_by_metadata`

- Purpose: select objects by route metadata.
- Inputs: `routeId`, `intent`, `difficulty`, `movementType`.
- Output: matching object ids, optional selection.
- Need: edits whole route segments without manual node tracking.

### `chain_variation_generate`

- Purpose: preview alternative route variants without applying them.
- Inputs: `objectIds`, `modes[]`, `targetDifficulty`, `constraints`.
- Output: candidate operation previews and metrics.
- Need: supports mapper iteration before destructive edits.

### `compile_safety_validate`

- Purpose: run common GoldSrc/TrenchBroom map safety checks.
- Checks: `__TB_empty`, missing material, non-convex brush, off-grid vertices, world bounds, skybox closure, spawn presence.
- Output: issue list with object ids and suggested fix category.
- Need: prevents compile failures after AI edits.

## Design Notes

- Prefer atomic tools over large templates.
- Preserve transaction ids and object ids for undo/selection.
- Return structured metrics, not prose-only summaries.
- Keep all generated geometry grid-snapped unless explicitly disabled.
- Every route tool should support optional metadata.
