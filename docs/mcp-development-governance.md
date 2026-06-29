# MCP Development Governance

This document is the hard rule set for future TrenchBroom MCP development.
It is normative. If another MCP roadmap, workflow note, or implementation idea
conflicts with this document, follow this document first and update the older
text.

The goal is to keep C++ MCP as a small, reliable execution kernel while skill
recipes and Agent workflows handle scene intent and prefab-like composition.

## Layer Contract

### C++ MCP Owns

C++ MCP may own a capability only when it needs at least one of these
TrenchBroom-specific responsibilities:

- active document identity, `expectedDocumentPath`, or document fingerprint
- undoable map mutation through TrenchBroom transactions
- selection state, native groups, object identity, or stale/live object recovery
- selector/module resolution against the current map
- map validation, problem checks, geometry facts, slope analysis, or route
  continuity
- review rendering, isolated screenshots, manifests, or capture paths
- entity, texture, face, object, or brush edits that must touch live map state

### Skill Recipes Own

Skill recipe scripts own reusable scene composition and domain intent:

- temple, courtyard, house, industrial, terrain pass, track, route, KZ, bhop,
  surf, slide, or other prefab-like layouts
- repeated architectural grammar, decorative structures, and aesthetic choices
- gameplay interpretation, difficulty judgement, and route intent narratives
- parameterized scene families that can emit deterministic IR

Recipes must emit IR JSON files. They must not call TrenchBroom, MCP, or `tb2`
directly, and they must not edit `.map` files.

### IR Owns

IR is the boundary format between recipe intent and C++ execution:

- large operation transport
- deterministic recipe output
- preview before mutation
- transaction-backed apply through MCP
- metadata attachment such as `moduleId`, `part`, `role`, `routeId`, `order`,
  and `generatedBy`

## Hard Rules

### Rule 1: No Scene Prefab Tools In C++ MCP

Do not add C++ tools such as:

- `create_temple`
- `create_courtyard`
- `create_kz_route`
- `create_racetrack`
- `create_house`
- `create_industrial_room`
- `create_bhop_chain`

If the feature name describes a finished scene, game mode, architectural style,
or domain-specific layout, it belongs in a skill recipe.

### Rule 2: Start In Recipe Unless C++ Is Clearly Required

When a request can be expressed as existing IR operations plus metadata, implement
it in a recipe or skill workflow first. Promote it to C++ only after real use
shows that it is a generic editor primitive or validator.

Promotion to C++ requires all of these:

1. At least two independent recipes or workflows need the capability.
2. The behavior cannot be expressed cleanly with existing IR operations.
3. The behavior needs TrenchBroom internals, undo/document safety, live object
   identity, geometry validation, or review rendering.
4. The output can be compact by default.
5. The Modeling profile does not become noisier without a separate justification.

### Rule 3: Generic Primitives Are Allowed, Scene Semantics Are Not

Allowed C++ examples:

- `arc_ramp`
- `path_ribbon`
- `ramp_between`
- `brush_create_polygon_batch`
- `geometry_analyze_slopes`
- `geometry_analyze_route_continuity`
- selector/module/group/transform/review tools

Not allowed as C++ examples:

- "KZ beginner route"
- "temple courtyard"
- "ascending loop track with rails"
- "industrial room kit"
- "bhop challenge chain"

A primitive may be useful for routes or buildings, but its name and schema must
describe geometry or editor state, not a finished scene concept.

### Rule 4: Existing Borderline Tools Are Compatibility, Not Expansion Points

Legacy or convenience tools may remain for compatibility, but do not expand them
into richer scene systems.

Current classifications:

| Tool or path | Status |
| --- | --- |
| `blockout_create_batch` | Generic batch geometry, keep. |
| `ir_compile_preview_from_file` / `ir_apply_from_file` | Recipe transport boundary, keep. |
| `blockout_create_spiral_stairs` | Generic stair primitive, keep but do not turn into route prefab logic. |
| `blockout_create_room/corridor/ramp/doorway/cover/sky_shell` | Compatibility helpers, not default workflow. |
| `python_generate_blockout` | Legacy/script bridge. Prefer skill recipes that emit IR. |
| KZ/temple/courtyard/track/house layouts | Recipe candidates, not C++ MCP tools. |

### Rule 5: Default Responses Must Be Compact

Every new or changed high-volume tool must support compact output by default:

- use `idsMode:"count"` or `idsMode:"sample"` for generated ids
- make full ids opt-in with `idsMode:"full"`
- make full face/seam/object listings opt-in with `detail:"full"`
- return counts, samples, bounds, warnings, and resource paths before long arrays
- do not return hundreds of ids in the default path

If a response cannot be made compact, redesign the tool before adding it.

### Rule 6: Selectors And Modules Are The Agent Target System

New workflows must prefer:

- `moduleId`, `part`, `role`, `routeId`, `order`, `generatedBy`
- structured JSON selectors
- native groups only for human-visible organization and manual selection

Do not make Agents carry long object id lists across turns when a selector,
module, operation id, or current user selection is enough.

For dense old maps and ambiguous ownership, prefer user selection over complex
automatic brush matching.

### Rule 7: Review Is Evidence, Not Validation

Visual review must stay readable and bounded:

- contact sheets default to at most two panels
- individual captures remain available in the manifest
- dense labels must auto-hide or stride
- review paths should be absolute or directly openable

Route, ramp, stair, surf, slide, and terrain claims must be backed by geometry
validation, not screenshots alone.

### Rule 8: Validation Semantics Must Be Explicit

Route-like validation must declare intent with modes such as:

- `continuous`
- `stepped`
- `jump_chain`
- `spiral`
- `closed_loop`

Do not infer a closed loop just because a route looks circular. Use
`closedLoop:true` only when the final surface is meant to connect to the first.

If a smooth slope is intended, `geometry_analyze_slopes` must report at least one
slope. `slopeCount=0` is a failed build for ramp/surf/slide/ascending intent.

### Rule 9: Modeling Profile Growth Requires Justification

Before adding a default-visible Modeling tool, answer:

1. Is this on the common Agent path?
2. Is it safer or clearer than an existing visible tool?
3. Would a skill rule be enough instead?
4. Can the tool be hidden but searchable?

Default-visible tools should cover status/open, IR preview/apply, selector/module
recovery, transform/delete, validation, review, and common atomic creation/editing.
Debug, viewport, low-level, legacy, and duplicate convenience entries should be
hidden but searchable.

### Rule 10: Tests And Real TB Acceptance Are Required

Every non-trivial MCP change must include:

- focused catalog tests when schema/profile/search behavior changes
- focused bridge tests when tool behavior changes
- compact-output tests for high-volume responses
- real TB disposable-map smoke when the change touches mutation, review,
  validation, document guards, or startup behavior
- no new crash logs during acceptance

Documentation-only governance changes need static checks, not a Release rebuild.

### Rule 11: IR Compatibility Must Be Intentional

IR is a public boundary between recipes and C++ MCP. Changes to IR shape must be
version-aware:

- keep old accepted fields working when practical
- add new fields as optional first
- return warnings for deprecated fields before removing them
- reject unknown or unsupported schema versions with a structured error
- document any breaking IR change in the governance/roadmap docs and recipe
  validation path

Recipes should include a schema/version marker once the IR shape changes beyond
small additive fields.

### Rule 12: Tool Lifecycle Must Be Explicit

Every MCP tool should fit one lifecycle state:

- `stable`: default for commonly used tools
- `experimental`: hidden/searchable until real workflows prove it
- `legacy`: kept for compatibility, with replacement guidance
- `deprecated`: replacement exists; no new workflow should use it

Do not remove a tool immediately unless it is unsafe. Hide or deprecate first,
keep exact-name search working, and add replacement text in the schema. Changing
default profile visibility counts as a compatibility change and needs catalog
test coverage.

### Rule 13: Performance Budgets Must Be Clear

MCP tools should have predictable cost. New high-volume tools must define:

- expected input size and target count
- default timeout or practical runtime expectation
- maximum returned id/detail volume in default mode
- behavior when work is too large: summarize, paginate, write a resource, warn,
  or reject before mutation

Review and validation tools should prefer bounded summaries, small samples, and
resource paths over huge inline payloads.

### Rule 14: Failure Recovery Must Be Structured

Mutating tools must either commit one clear transaction or fail before mutation.
Partial mutation is allowed only when explicitly documented and reported.

Failures should return structured diagnostics that tell the Agent:

- whether the document was mutated
- whether retrying is safe
- whether the active document/path/fingerprint mismatched
- whether ids were stale or selectors matched nothing
- whether validation failed before commit or rollback happened
- which recovery path to use: retry, refresh status, inspect detail, undo, or
  rebuild

Crash, wrong-map write, data loss, and unclear mutation state are P0 issues.

## New Capability Decision Checklist

Use this checklist before implementing a request:

1. Can it be expressed with existing IR operations plus metadata?
   - Yes: implement it in a recipe or skill workflow.
2. Does it need undo, document guard, selection state, object identity, validation
   geometry, or review rendering?
   - Yes: C++ MCP may be appropriate.
3. Is it a scene, route family, building style, gameplay object, or aesthetic
   pattern?
   - Yes: keep it in skill recipe.
4. Would it make the Modeling profile noisier?
   - Yes: hide it by default or put guidance in the skill.
5. Can output be summarized by default?
   - No: redesign before implementation.
6. Is this a repeated need across independent workflows?
   - No: keep it in recipe or workflow until the pattern proves itself.

When unsure, start in a recipe. Promote to C++ only after repeated real workflows
prove the need.

## Required Change Template

For any MCP feature proposal, record these answers in the implementation plan,
commit message, PR text, or matching design doc:

```text
Layer decision:
- Owner layer:
- Why not recipe:
- Why not existing MCP tools:
- Required TrenchBroom internals:
- Default output mode:
- Modeling profile visibility:
- Compatibility/lifecycle:
- Performance budget:
- Failure recovery behavior:
- Validation path:
- Real TB acceptance plan:
```

## Skill Synchronization Rules

When C++ MCP behavior changes, update the skill only for workflow decisions:

- which tool to call by default
- when to use recipes instead of direct MCP
- which validation mode to choose
- how to keep responses compact
- how to recover targets through selector/module/group/user selection

Do not put long C++ schema copies into the skill. MCP schema remains the source
for parameter details; the skill owns routing and judgment.

When adding a recipe, update the skill recipe manifest, examples, validator, and
recommended MCP validation path. Do not add a matching C++ prefab tool.

## Skill Development Rules

Skill constraints get stricter the closer they move to the editor kernel, and
softer the closer they move to creative intent.

- Workflow skills may choose tool order, validation order, recovery paths, and
  when to use recipes.
- Recipe scripts may encode prefab-like composition, but must emit deterministic
  IR only.
- Domain skills may judge style, gameplay, difficulty, or route intent, but
  must not claim editor facts that MCP did not validate.
- Skills must not mutate maps directly, call TrenchBroom internals, edit `.map`
  files, or bypass MCP document guards and undo transactions.
- When a skill needs new MCP support, describe it as a generic primitive,
  selector/module operation, validator, review feature, compact-output
  improvement, or failure-recovery improvement.
- Skills should express policies more than procedures: goals, constraints,
  validation requirements, recovery strategies, and heuristics.
- Avoid rigid step-by-step procedures unless editor safety, compatibility,
  deterministic recipe output, validation gates, or failure recovery require
  them.
- Prefer soft routing language such as "usually", "recommend",
  "common pattern", and "may". Reserve "must", "always", and exact sequences
  for safety, compatibility, deterministic output, validation, and recovery.
- Creative rules may be flexible. Mutation, validation, compatibility,
  performance, and recovery rules are strict.

## Review Checklist

Before merging or committing MCP work, verify:

- no scene-prefab C++ tool was added
- new primitives are generic and reusable
- large outputs are compact by default
- hidden/default profile behavior is intentional
- selectors/modules/groups remain the recovery path
- route/slope semantics are validated by geometry tools
- review images remain readable
- docs and skill routing are updated when workflow changes
- IR compatibility and tool lifecycle are intentional
- performance and failure behavior are documented for high-volume tools
- focused tests and real TB acceptance match the change risk

This is the guardrail that keeps MCP useful without letting it become a pile of
scene generators.
