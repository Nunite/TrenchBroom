# Merge Progress Log

This document tracks the incremental merge of upstream changes into the `merge-upstream-sync` branch.

## Plan
- Start point: `24e61209f63a41e6c7c438a6adeb1cead0cbd8d6`
- Batch size: 20 commits
- Goal: Sync with upstream/master

## Merge History

| Date | Target Commit | Status | Notes |
|------|---------------|--------|-------|
| 2026-01-04 | `24e61209f63a41e6c7c438a6adeb1cead0cbd8d6` | **Success** | Merged start point. Conflicts resolved in MapViewBase and ToolBox. |
| 2026-01-04 | `a20d0dd30bbf72d2f904b860b76c7a4f04f5e203` | **Failed/Aborted** | Attempted merge but encountered significant conflicts due to directory restructure (`common/src/ui` -> `lib/TbUiLib`). Aborted to allow testing of the start point first. |
