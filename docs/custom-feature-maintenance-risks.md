# Custom Feature Maintenance Risks

This document tracks custom features that are useful, but currently more invasive or less maintainable than the upstream TrenchBroom style. Keep this file updated as the issues are fixed.

For a commit-oriented review of the current custom feature stack and suggested
optimization order, see `docs/custom-feature-architecture-review.md`.

## High Priority

### Python v2 Runtime and Plugin API

- Risk: `PythonV2Module.cpp` is a large mixed module containing bindings, UI panel construction, callbacks, transactions, and geometry helpers.
- Risk: process-global callback, event, handle, and transaction state makes document/window/plugin unload behavior harder to reason about.
- Suggested fix: split the v2 bindings by domain (`document`, `selection`, `geometry`, `panel`, `events`, `actions`) and move callback/timer ownership fully into `PythonPluginSession`.

### Outliner Tree and Property Editor

- Risk: `OutlinerTreeWidget` stores raw `mdl::Node*` in tree items and contains comments about avoiding `dynamic_cast` on deleted objects.
- Risk: `OutlinerEntityPropertyEditor` rebuilds its whole QWidget tree for many updates, which can reset scroll position and cause UI latency.
- Risk: embedded smart editors such as `SmartSkyboxEditor` can update map properties through a separate path from the native entity inspector; if they force a full Outliner rebuild, undo/redo and selection refresh can become visibly inconsistent.
- Risk: embedded smart editors that scan filesystem resources or load preview icons can make ordinary selection changes slow if they are rebuilt repeatedly.
- Suggested fix: move the tree to a model/view design with stable node ids or explicit invalidation; split property row creation into a row factory and update individual row values when possible; cache smart-editor resource scans and previews by game path, with explicit manual refresh for expensive reloads.

### Python v2 Event Dispatch

- Risk: editor notifications such as selection changes can become unexpectedly expensive if they initialize Python or import plugin modules when no plugin callback is registered.
- Suggested fix: keep event emission lazy; do not initialize Python for passive editor notifications, and check for registered callbacks before entering plugin dispatch.

### Sky Rendering

- Risk: `SkyRenderer` handles resource lookup, loose texture loading, sky brush geometry collection, GL state, and render submission in one class.
- Risk: `canRender()` can traverse sky brush geometry, and selection/locked renderers depend on `canRender()` to decide whether to hide original sky faces.
- Risk: sky brush geometry invalidation is currently coarse-grained; if future changes make it invalidate on selection changes, 3D selection feedback can become visibly slow on large maps.
- Suggested fix: split sky resource resolution, sky brush geometry caching, and rendering; invalidate geometry only when map visibility, brush geometry, or sky-related materials change.

### MCP / Agent Bridge

- 风险：MCP 会把编辑器控制能力暴露给外部进程，如果 mode、token 和工具边界没有持续收紧，很容易变成过宽的本地远控入口。
- 风险：`action_execute` 已经接入，但 action 可能间接修改地图；它必须继续要求 `Edit` 模式，不能放回 `ReadOnly`。
- 风险：后续 entity/brush 编辑工具如果绕过 `MapDocument` transaction 或直接改 `.map` 文件，undo/redo、selection、dirty 状态都会和普通编辑器行为分裂。
- 风险：让 Agent 直接创建任意 brush 顶点很容易生成非法或难排查的 BSP 几何；白盒生成必须优先走语义 Blockout IR 和确定性 brush primitive 编译。
- 风险：当前 `overlay_set` / `overlay_clear` 只是 bridge 内存状态，还没有真实接入 renderer；后续如果直接塞进某个 renderer，会和 FPS、sky、2D readable outlines 等功能继续分裂。
- 风险：tool catalog 会快速膨胀；如果每个功能都新增临时 JSON 形状而没有共享 DTO、mode gating 和 focused tests，后续 MCP 会很难维护。
- 风险：第二/三阶段工具已落地后，`McpBridgeServer.cpp` 同时包含 document/action/edit/asset/texture/blockout 逻辑，文件体积和职责都在快速变大。
- 风险：当前 MCP history 只在 MCP 操作仍位于原生 undo/redo 栈顶时工作；这是安全的第一版，但不等于完整的跨用户编辑操作历史管理。
- 风险：Blockout IR 第一版已经避免直接拼任意 brush 顶点，但 snap、尺寸约束、房间开口规则和错误报告还比较基础。
- 建议修复：MCP 默认关闭，本地 token 必须保留；协议层继续放在 `TbMcpLib`；下一步把 bridge tool handler 拆成独立模块；所有写操作继续使用命名 transaction 并补 rollback/真实地图集成测试；overlay 必须等视图叠加层管理器后再接真实绘制；不要开放低层任意 brush 顶点工具，除非先有严格 validation。

## Medium Priority

### Model Browser and Resource Access

- Risk: UI-specific model browsing exposed `Map::gameFileSystem()` and `Map::reloadEntityModels(...)`, increasing the public surface of `mdl::Map`.
- Suggested fix: introduce a UI/resource facade or service for model browser operations, keeping `Map` focused on document state and core operations.

### Brush Chamfer Operations

- Risk: chamfer algorithms are implemented directly in `mdl::Brush`, increasing the core class size and upstream merge conflict surface.
- Suggested fix: move chamfer code into a dedicated geometry helper or command module, leaving `Brush` with smaller primitive operations.

### Preferences and Misc UI

- Risk: Misc preferences can become a catch-all for Python plugins, Pie Menu, language, and editor behavior.
- Suggested fix: keep the upstream preferences structure and use dedicated panes for Python plugins, Pie Menu, language, and custom tools.

## Lower Priority

### Generated Python Package Metadata

- Status: cleaned. `python/src/trenchbroom_api.egg-info` was removed from the repository, and `*.egg-info/` is ignored.
- Keep this as a guardrail: generated Python packaging metadata should not be committed again.

### Development Scripts and Debug Artifacts

- Risk: skybox/debug helper scripts and local notes can accumulate in the repository root.
- Suggested fix: keep reusable tools under `scripts/` or `tools/dev/`, and ignore/delete one-off debug output.
