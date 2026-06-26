# MCP Scene Iteration Cases

This document tracks scene-level MCP test cases. The goal is to build each scene as a whitebox, review screenshots and geometry facts, then adjust MCP tools when the agent is forced into brittle or overly verbose workflows.

The scenes below are intentionally not prefab requests. Each one should be built from atomic or mid-level modeling operations such as polygon batches, path ribbons, arches, cuts, transforms, metadata, and viewport review.

## Review Principles

- Use whitebox materials only. The screenshot review should judge silhouette, scale, spatial logic, connectivity, and recognizable scene intent.
- Build in phases: playable/main massing, boundaries and supports, then markers/details. Each phase should have its own operation record.
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
