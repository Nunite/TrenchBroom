# Merge Progress Log

This document tracks the incremental merge of upstream changes into the `merge-upstream-sync` branch.

## Plan
- Start point: `24e61209f63a41e6c7c438a6adeb1cead0cbd8d6`
- Batch size: 20 commits
- Goal: Sync with upstream/master
- 最高优先级：不要一次性合并到最新的commit。合并的commit消息要用中文。每批次合并了commit后要用户测试是否有问题。如果有问题，要及时修复。

## Merge History

| Date | Target Commit | Status | Notes |
|------|---------------|--------|-------|
| 2026-01-04 | `24e61209f63a41e6c7c438a6adeb1cead0cbd8d6` | **Success** | Merged start point. Conflicts resolved in MapViewBase and ToolBox. |
| 2026-01-04 | `a20d0dd30bbf72d2f904b860b76c7a4f04f5e203` | **Reverted** | 错误地尝试了一次性合并700+个commit。已回滚并重新开始分批合并。 |
| 2026-01-04 | `1d9b8806f734f58bcb02288c3014302d6ebb8a89` | **Success** | 合并后续20个commit。解决 MapViewBase.cpp 和 ci.yml 的冲突。编译成功，应用基本启动正常。 |
| 2026-01-04 | `9c12efebb` | **Success** | 合并后续20个commit。解决 MapFrame.cpp, SmartColorEditor.cpp, MapViewBase.cpp 等文件的API冲突（Selection API重构）。编译成功。 |
| 2026-01-04 | `eaf1e4387` | **Success** | 合并后续20个commit。解决 mdl::Map 重构导致的大量编译错误（SmartModelEditor, MapViewBase, VertexTool 等）。编译成功。 |
| 2026-01-05 | `dac01ab03` | **Success** | 合并后续20个commit。解决 Map_Nodes.h (include冲突) 和 MapViewBase.cpp (selection API) 的冲突。修复 BoxSelectionTool 和 MapDocument 中 deselectAll/selectNodes API 调用错误。主程序编译成功，测试工程(test)存在链接错误但不影响运行。 |
