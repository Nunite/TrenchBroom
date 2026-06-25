# TrenchBroom MCP 集成与白盒生成计划

本文档记录当前分支接入 MCP / Agent 工作流的底层架构。目标不是让
Agent 直接编辑 `.map` 文件，也不是把 Python 插件系统暴露成一个任意
执行入口，而是先建立本机可控、可测试、可撤销的结构化编辑通道。

## 目标

- 让外部 MCP client 可以查询当前 TrenchBroom 状态、地图摘要、选择集和可用动作。
- 让后续 Agent 能通过结构化工具创建实体、放置资产和生成白盒关卡。
- 所有写操作必须进入 `MapDocument` transaction，并保留 undo/redo 语义。
- AI 不直接操作 brush 顶点；白盒生成先进入 Blockout IR，再由确定性代码生成合法 brush。
- MCP 相关代码与 `tb2`、资产浏览器、视图 overlay 保持边界清晰，避免形成新的巨型模块。

## 总体架构

```text
MCP Client
    |
    | Streamable HTTP / JSON-RPC MCP
    v
TrenchBroom 内置 127.0.0.1:37666/mcp
    |
    | UI thread dispatch
    v
MapDocument transaction / query services
```

兼容路径：

```text
MCP Client
    |
    | stdio / JSON-RPC MCP
    v
trenchbroom-mcp.exe           (兼容 shim，适配只支持 stdio 的客户端)
    |
    | QLocalSocket + legacy token
    v
TrenchBroom McpBridgeServer
    |
    | UI thread dispatch
    v
MapDocument transaction / query services
```

当前推荐主路径是 TrenchBroom 进程内的本地 HTTP `/mcp` 端点。`trenchbroom-mcp.exe`
只保留为很薄的 stdio 兼容层，真正的 MCP tool catalog、mode gating、transaction
和 UI thread dispatch 都放在 TrenchBroom / `TbMcpLib` 内。这样既兼容 stdio
客户端，又能让 Claude Code、Cursor、Codex 等支持 HTTP 的客户端直接连接运行中的
TrenchBroom，减少额外 exe、pipe 配置和鉴权状态不同步带来的维护成本。

职责划分：

- `TbMcpLib`：公共协议、DTO、错误码、tool catalog、配置读写和 JSON 序列化。
- `trenchbroom-mcp.exe`：外部 stdio MCP server，只做 MCP 协议适配和本地 bridge 转发；后续保留为兼容 shim。
- `McpBridgeServer`：运行在 TrenchBroom 进程内，接收本地请求，验证 mode，并在 UI thread 执行工具；旧 stdio 兼容路径仍保留 token。
- `McpHttpServer`：运行在 TrenchBroom 进程内，监听 `127.0.0.1`，提供最小 Streamable HTTP `/mcp` 端点。
- `MapDocument` / 现有 UI services：唯一真实状态；MCP 不直接读写 `.map` 文件。

## 当前实现状态

截至当前分支，MCP 底层已经完成以下检查点：

- 已新增 `TbMcpLib`，包含 mode、错误码、bridge config、bridge request/response 和 tool catalog。
- 已新增 TrenchBroom 内部 `McpBridgeServer`，支持本地 `QLocalServer` 和 mode gating；旧 stdio 兼容路径仍使用 token 转发。
- 已新增 TrenchBroom 内置 `McpHttpServer`，默认监听 `127.0.0.1:37666/mcp`，支持 `POST /mcp` JSON-RPC、notification `202 Accepted`，并为先探测 GET 的客户端提供轻量 SSE stream。
- 已抽出共享 `McpJsonRpc` 处理层，HTTP server 与 stdio shim 共用 `initialize`、`tools/list`、`tools/call` 逻辑；`tools/list` 在 `Off` 模式下仍返回已实现工具列表。
- 已新增 `trenchbroom-mcp.exe`，作为 stdio MCP server，支持 `initialize`、`tools/list`、`tools/call` 并转发到本地 bridge。
- 已接入基础查询工具：`tb_status`、`tb_doctor`、`documents_list`、`document_snapshot`、`map_snapshot`、`map_search`、`selection_get`、`actions_list`。
- 已接入第一批编辑器状态工具：`selection_set`、`overlay_set`、`overlay_clear`。
- 已接入 `action_execute`，但它要求 `Edit` 模式，因为任意 action 可能间接修改地图。
- 已接入文档生命周期工具：`documents_open`、`documents_activate`、`documents_save`、`documents_close`、`documents_export`。MCP 保存/导出要求绝对路径，关闭 dirty 文档必须显式传入 `discardChanges=true`，避免阻塞式确认对话框。
- 已接入选择与视图控制工具：`selection_filter`、`selection_by_bounds`、`selection_grow`、`viewport_focus`、`viewport_clear_marks`、`viewport_capture_current`、`viewport_capture_3d`、`viewport_capture_2d`。当前 `viewport_focus` 复用现有 action；截图工具可返回临时 PNG 路径或 base64。
- 已接入 FGD/schema 与 brush entity 工具：`fgd_entities_list`、`entity_schema`、`entity_create_from_schema`、`entity_tie_brushes`、`entity_untie_brushes`。
- 已接入事务型写入工具：entity 创建/更新/删除、brush primitive 创建。
- 已接入 MCP operation history：`history_list`、`history_undo_mcp`、`history_redo_mcp`。当前只在 MCP 操作仍位于 TrenchBroom 原生 undo/redo 栈顶时执行，避免误撤用户手动编辑。
- 已接入 GoldSrc 资产与材质工具：`.mdl/.spr/.wav` 搜索与放置、material 搜索与应用、face list/select、基础 face texture set、texture replace/copy/align。`asset_search` 返回 type、path、sourceRoot、absolutePath、displayName 和 lastModified。
- 已接入对象删除与变换工具：`objects_delete`、`objects_transform`，当前支持按 object id 删除，以及 translate/rotate/scale 三类确定性变换。
- 已接入地图验证与安全修复工具：`map_validate`、`problems_check`、`problems_fix`、`map_fix_all_safe`。自动修复只允许明确白名单内的 safe quick fix，不自动删除对象或做大范围结构性调整。
- 已接入编译与 leak 辅助工具：`compile_profiles_list`、`compile_run`、`compile_log_tail`、`leaks_load_pointfile`。`compile_run` 复用现有 Compile dialog 和 profile runner，不新建独立外部进程框架。
- 已接入 Blockout IR 第一版：room、corridor、stairs、ramp、doorway、cover、sky shell 和 validate。

仍未完成：

- 完整视图叠加层管理器；当前 `overlay_set` / `overlay_clear` 已能在 map view 中渲染轻量 bounds、point 和 label marker，但仍是 MCP bridge state 驱动的第一版，不是通用 overlay manager。
- 稳定 arch/torus primitive 生成器；当前 `brush_types_list` 会标记为 unsupported，不出现在默认 `tools/list` 可调用工具中。
- Prefab provider / prefab_create；当前只在 catalog 中保留 `prefabs_list`、`prefab_create` 未实现占位，不出现在默认 `tools/list`。
- 更完整的高级 UV 对齐模式；当前 MCP face alignment 先覆盖 reset/paraxial/parallel 这类可稳定映射到现有 API 的模式。
- MCP tool handler 已按 document/action/asset/brush/compile/entity/history/object/problem/selection/texture 等文件拆分。后续新增工具应继续保持这个边界，不要把逻辑重新堆回 `McpBridgeServer.cpp`。

## 配置与安全模式

配置路径：

```text
%APPDATA%/TrenchBroom/MCP/config.json
```

第一版字段：

```json
{
  "pipeName": "trenchbroom-mcp-<user>",
  "token": "<random-token>",
  "mode": "Off",
  "httpEnabled": true,
  "httpHost": "127.0.0.1",
  "httpPort": 37666,
  "toolProfile": "Modeling"
}
```

模式：

- `Off`：默认值，不启动 bridge。
- `ReadOnly`：允许读取地图、选择、动作列表，以及设置/清理 MCP overlay 等安全 UI 状态操作。
- `Edit`：允许结构化写操作，所有写入进入 transaction。
- `Danger`：预留给未来 `run_tb2_script` 或专家级能力；第一版不实现。

HTTP 主路径不要求 bearer token；它只绑定 `127.0.0.1`，并由 Preferences 中的
`Off` / `ReadOnly` / `Edit` mode 控制是否监听和允许哪些工具。旧 stdio shim 到
`QLocalSocket` bridge 的兼容路径仍携带 config token，避免旧客户端误连其他本地 pipe。

安全约束：

- HTTP server 只能默认绑定 `127.0.0.1`，不绑定 `0.0.0.0`。
- Streamable HTTP 请求必须校验 `Origin`，避免 DNS rebinding 类攻击。
- HTTP 请求不需要 `Authorization` header；stdio shim 读取 config token 并转发给 legacy bridge。
- `tools/list` 应返回稳定的已实现工具列表，不因为当前 `Off` / `ReadOnly` / `Edit` mode 返回空列表；实际调用时再由 mode gating 返回 `Forbidden`。这能避免 Claude Code 显示 `connected · no tools`。
- `toolProfile` 控制 `tools/list` 默认暴露范围：`Core` 只暴露状态、搜索和 batch/operation 工具；`Modeling` 为推荐默认，暴露原子建模、point entity/FGD schema、batch IR、Python IR、几何验证和基础 face/texture 工具；`Balanced` 保留通用编辑器工具；`Full` 暴露全部专家和调试工具。
- `Danger` 不通过 UI 暴露，不进入默认 tool list。

## MCP Transport 设计细节

MCP 官方 2025-06-18 规范定义两种标准 transport：

- `stdio`：客户端启动一个本地子进程，通过 stdin/stdout 传 JSON-RPC。客户端应尽量支持它；server 不得向 stdout 写非 JSON-RPC 日志。
- `Streamable HTTP`：server 作为独立服务进程提供单个 MCP endpoint，例如 `/mcp`，用 HTTP POST / GET 传 JSON-RPC 和可选 SSE。

对 TrenchBroom 来说，长期更适合以内置 Streamable HTTP 为主：

- TrenchBroom 本来就是唯一真实状态，直接在进程内暴露 `/mcp` 可减少 `trenchbroom-mcp.exe -> QLocalSocket -> TrenchBroom` 的中转层。
- Claude Code 支持 `claude mcp add --transport http <name> <url>`，HTTP server 断线后有自动重连；stdio server 是本地子进程，生命周期更依赖客户端。
- 内置 HTTP 能在 Preferences 中显示真实 URL、状态和可复制配置命令，用户体验比维护一个额外 exe 更直接。
- stdio exe 仍有价值：Claude Desktop、部分旧客户端或只支持 stdio 的环境可以继续用它作为兼容 shim。

当前 HTTP server 是最小 Streamable HTTP 实现，同时提供一个轻量 SSE 兼容 stream：

- `POST /mcp` 接收单个 JSON-RPC request / notification。
- request 返回 `application/json` 单个 JSON-RPC response。
- notification 返回 HTTP `202 Accepted`。
- `GET /mcp` 返回 `text/event-stream` 并保持连接。当前只发送 readiness comment，不承载工具调用结果；这主要用于兼容 Claude Code 交互式会话或其他优先打开 SSE 的客户端。
- 支持 `initialize`、`notifications/initialized`、`ping`、`tools/list`、`tools/call`，与 stdio shim 共用同一套 request handler。

为避免重复实现，当前已抽出公共协议层：

- `McpJsonRpc`：处理 `initialize`、`tools/list`、`tools/call`，产出 JSON-RPC response，并封装 tool result 的 `content` / `structuredContent`。
- `McpStdioServer`：只负责 stdin/stdout 行协议。
- `McpHttpServer`：只负责 Qt HTTP transport、Origin 校验和 response code。

## 第一批 Tools

第一阶段只实现稳定基础能力：

- `tb_status`：返回 TrenchBroom 版本、bridge mode、当前文档数量和活动文档状态。
- `tb_doctor`：诊断 bridge 配置、mode、活动文档和可用工具。
- `documents_list`：列出当前打开文档。
- `document_snapshot`：返回活动文档基本信息。
- `map_snapshot`：返回地图摘要，例如 worldspawn 属性、实体数量、brush 数量和 bounds。
- `map_search`：按 classname、targetname、属性和文本搜索对象。
- `selection_get`：返回当前选择。
- `selection_set`：结构化设置选择；它只改变编辑器选择状态，不写 map 文件，因此当前允许在 `ReadOnly` 中使用。
- `selection_filter`：按 type、classname、targetname、material、bounds 或文本过滤对象，可选择是否替换当前选择。
- `selection_by_bounds`：按 intersects / contains 选择逻辑 bounds 匹配的对象。
- `selection_grow`：把当前选择扩展到 parents、children 或 siblings。
- `viewport_focus`：聚焦当前选择或传入 object ids；当前通过现有视图 action 执行。
- `viewport_clear_marks`：清理 MCP overlay 状态，可选清空当前选择。
- `viewport_capture_current`：截取当前 TrenchBroom 窗口，默认写入临时 PNG，也可返回 base64；这是只读视觉反馈工具，不改变当前视图状态。
- `viewport_capture_3d` / `viewport_capture_2d`：截取当前可见的 3D / 2D map viewport。优先使用当前聚焦视图；如果当前视图类型不匹配，则从活动 `MapWindow` 中查找第一个可见的对应视图。
- `fgd_entities_list`：列出当前 game config/entity definition manager 中的 entity class，可按 point/brush 和文本过滤。
- `entity_schema`：返回指定 entity class 的 FGD 属性、类型、默认值和 point entity bounds。
- `entity_create_from_schema`：使用 FGD 默认值创建 point entity，并叠加调用方传入的属性。
- `entity_tie_brushes`：把选中或指定 world brush 绑定成 brush entity。
- `entity_untie_brushes`：把选中或指定 brush entity 中的 brush 移回当前合适父节点。
- `actions_list`：列出可执行 actions。
- `action_execute`：执行已注册 action，遵循 action 自身 enabled 状态；由于 action 可能修改地图，当前要求 `Edit` 模式。
- `overlay_set` / `overlay_clear`：设置或清理 MCP overlay 状态；第一版会在 map view 中绘制 object bounds、point marker、bounds marker 和 label，不改变真实选择集或地图数据。

第二阶段已开放：

- `documents_open`、`documents_activate`、`documents_save`、`documents_close`、`documents_export`
- `entity_create`、`entity_update`、`entity_delete`
- `brush_types_list`
- `brush_create`
- `brush_create_box`、`brush_create_wedge`、`brush_create_cylinder`
- `brush_create_cone`、`brush_create_pipe`、`brush_create_sphere`
- `brush_create_pyramid`、`brush_create_tetrahedron`、`brush_create_from_planes`
- `asset_search`、`asset_place_model`、`asset_place_sprite`、`asset_place_sound`
- `textures_list`、`texture_search`、`texture_apply`、`texture_replace`
- `texture_align_face`、`texture_copy_from_face`
- `face_list`、`face_select`、`face_texture_set`
- `objects_delete`、`objects_transform`
- `map_validate`、`problems_check`、`problems_fix`、`map_fix_all_safe`
- `compile_profiles_list`、`compile_run`、`compile_log_tail`、`leaks_load_pointfile`
- `history_list`、`history_undo_mcp`、`history_redo_mcp`

第三阶段已开放 Blockout IR：

- `tb_tools_search`
- `operation_inspect`
- `operation_select`
- `operation_validate`
- `blockout_create_batch`
- `blockout_create_curved_corridor`
- `blockout_create_room`
- `blockout_create_corridor`
- `blockout_create_stairs`
- `blockout_create_ramp`
- `blockout_create_doorway`
- `blockout_create_cover`
- `blockout_create_sky_shell`
- `blockout_validate`

## MCP Tool 使用策略

当前分支采用三层 tool 架构，目标是降低 Agent 上下文消耗，同时保留足够的建造上限：

- 高层 outcome tools：用于确定性生成常见结构，例如 `blockout_create_spiral_stairs` 和 `blockout_create_curved_corridor`。当用户要“做一个旋转楼梯/弧形走廊”时，应优先调用这类工具，而不是让 Agent 逐个创建 brush。
- 中层 Batch Blockout IR：`blockout_create_batch` 是默认主力入口。Agent 一次提交 `operations[]`，TrenchBroom 在内部完成 grid snap、validation、brush 编译和一次 transaction。失败时不提交任何 brush。
- 低层 atomic tools：`brush_create_*` 保留给建模精修或单个特殊 primitive。默认 `Modeling` profile 直接暴露常用原子工具（box、prism、cylinder sector、from_planes 等）和 point entity 创建链路（`fgd_entities_list`、`entity_schema`、`entity_create`、`entity_create_from_schema`）；其他 UI、编译、资产、brush entity 绑定和高层 prefab 式工具通过 `tb_tools_search` 按需发现，或切换 `Balanced` / `Full`。

批量/创建工具的默认返回遵循 compact result 约定：

- 默认 `detail=summary`，只返回 `operationId`、`transactionName`、数量、bounds、validation 和 `resourceUri`。
- `detail=ids` 才返回 object ids。
- `detail=full` 仍优先通过 `tbmcp://operation/<id>` resource 取详情，不鼓励把大量 brush summary 直接塞进对话上下文。
- JSON-RPC `tools/call` 的文本 `content[0].text` 只放短摘要；完整结构在 `structuredContent`，并为 operation 返回 `resource_link`。
- `resources/read` 支持读取 `tbmcp://operation/<id>`，用于按需查看输入参数、object ids、validation 和生成详情。

`blockout_create_batch` 第一版支持的 operation 类型：

- `box`
- `prism`
- `cylinder_sector`
- `room`
- `corridor`
- `curved_corridor`
- `stairs`
- `ramp`
- `doorway`
- `cover`
- `sky_shell`

`blockout_create_curved_corridor` 是 batch compiler 的薄封装，会生成 floor、ceiling、inner wall、outer wall 和可选 caps。它用于替代几十次 `brush_create_cylinder_sector` 调用，既减少上下文噪声，也保证一次 undo/redo。

## Legacy Bridge 协议

`trenchbroom-mcp.exe` 和 TrenchBroom 内部 bridge 使用一条本地 JSON 消息协议。这是
stdio 兼容 shim 的内部协议；推荐的 HTTP `/mcp` 入口不需要用户配置 token。请求：

```json
{
  "id": "request-1",
  "token": "<config-token>",
  "tool": "tb_status",
  "mode": "ReadOnly",
  "params": {}
}
```

成功响应：

```json
{
  "id": "request-1",
  "ok": true,
  "result": {
    "mode": "ReadOnly"
  }
}
```

失败响应：

```json
{
  "id": "request-1",
  "ok": false,
  "error": {
    "code": "Unauthorized",
    "message": "Invalid MCP bridge token"
  }
}
```

写操作成功时额外返回：

```json
{
  "operationId": "mcp-op-42",
  "transactionName": "MCP: Create box brush",
  "changedObjectIds": ["node:12"]
}
```

## Blockout IR

Blockout IR 是 Agent 生成白盒的主要入口。它把“房间、走廊、楼梯、门洞、掩体、天空壳”等语义结构转换为合法 BSP brush，而不是让 AI 直接拼 brush 顶点。

基础规则：

- 坐标默认使用 GoldSrc units。
- 默认 grid 为 16；第一版工具要求调用方传入已对齐的 GoldSrc units，后续会把 snap 策略显式纳入 `blockout_validate` 和 create 工具。
- 只生成凸 brush primitive。
- 门洞、窗口和开口通过拆分墙体生成，不做 CSG 差集。
- validation 失败时不提交 transaction，并返回错误列表。

示例 IR：

```json
{
  "type": "room",
  "origin": [0, 0, 0],
  "size": [512, 384, 192],
  "wallThickness": 16,
  "floorTexture": "concrete",
  "wallTexture": "brick"
}
```

## Overlay 与截图

MCP overlay 不应直接散落在 renderer 临时分支中。当前第一版使用轻量 bridge state 表达，并由 `MapViewBase` 的统一 render tail 绘制：

- object id 高亮。
- label。
- point marker。
- bounds marker。
- 清理 overlay。

后续仍应接入 roadmap 中的“视图叠加层管理器”，与 FPS、2D readable outlines、sky、debug overlay 共享统一设置和刷新路径。当前实现不写 map、不改 selection、不参与 undo，只作为 Agent 视觉反馈层。

当前已接入：

- `viewport_capture_current`
- `viewport_capture_3d`
- `viewport_capture_2d`

它截取当前 TrenchBroom 窗口，默认写入 `%TEMP%/TrenchBroomMCP/viewport-<timestamp>.png`，也可以通过 `returnBase64=true` 返回 PNG base64。该工具只读，不改变用户当前视图状态。它的 `scope` 返回值为 `window`，提醒调用方这是整窗截图，不是单个 map viewport。

`viewport_capture_3d` 与 `viewport_capture_2d` 复用同一输出策略，但只截取对应的 map viewport，`scope` 分别返回 `3d` 和 `2d`。它们仍通过 `MapWindow` / `MapViewBase` 控件抓取，不绕过 UI 状态或直接操作 OpenGL framebuffer。

## 编译与 Leak 辅助

MCP 编译工具不重新实现外部进程运行器，而是复用 TrenchBroom 当前 Compile dialog 与 compilation profile：

- `compile_profiles_list` 返回活动文档当前 game config 中的 profiles、workdir spec 和 task 摘要。
- `compile_run` 选择指定 profile 并通过现有 Compile dialog 启动运行；它返回 `started=true`，不阻塞等待编译完成。
- `compile_log_tail` 返回当前 Compile dialog 输出框的尾部文本，用于 Agent 轮询编译结果。
- `leaks_load_pointfile` 先解析指定 `.pts/.lin`，成功后调用 `MapDocument::loadPointFile`，让现有视图渲染 leak path。

这些工具要求本地 `Edit` mode 才能运行外部编译或改变 leak 可视化状态。后续若要做真正的 headless compile session，应从 `CompilationRun` 抽出非 QWidget 输出 sink，而不是让 MCP 直接 fork 编译器。

## HTTP Smoke Workflow

本分支提供 `scripts/mcp-smoke.ps1` 用来验证 MCP client 到 TrenchBroom 内置 HTTP
端点的完整链路。它默认读取 `%APPDATA%/TrenchBroom/MCP/config.json` 中的
`httpHost` 和 `httpPort`，并只执行安全检查：

- `initialize`
- `tools/list`
- `tb_status`
- `tb_doctor`

使用前启动 TrenchBroom，并在 `Preferences > MCP` 中把 MCP mode 设置为 `ReadOnly`
或 `Edit`。默认 `Off` 模式下 HTTP endpoint 不监听，smoke 脚本会连接失败，这是预期的安全行为。

基础检查：

```powershell
scripts\mcp-smoke.ps1
```

输出完整结构化 JSON：

```powershell
scripts\mcp-smoke.ps1 -RawJson
```

验证 overlay marker：

```powershell
scripts\mcp-smoke.ps1 -Overlay
```

验证截图工具：

```powershell
scripts\mcp-smoke.ps1 -Capture 3D
```

清理 smoke overlay：

```powershell
scripts\mcp-smoke.ps1 -ClearOverlay
```

常见失败含义：

- `MCP config does not exist`：还没有启动过 TrenchBroom 或没有打开过 `Preferences > MCP`。
- `Connection refused / 目标计算机积极拒绝`：TrenchBroom 未运行，或 MCP mode 仍是 `Off`。
- `Forbidden`：当前 mode 不允许调用该工具，例如 `ReadOnly` 中调用写工具。

## MCP Client 配置片段

生成可复制到 MCP client 的配置片段：

```powershell
scripts\mcp-config.ps1 -Print
```

默认输出目录：

```text
build-release-codex\mcp-config
```

生成文件：

- `trenchbroom-mcp.http.json`：通用 HTTP MCP server 记录。
- `trenchbroom-mcp.mcpServers.json`：适合 Claude Desktop / Cursor 风格 `mcpServers` 配置的 JSON 片段。
- `trenchbroom-mcp.codex.toml`：适合 Codex 风格 MCP 配置的 TOML 片段。
- `README.md`：本地配置说明。

第一版脚本只生成配置，不直接改写用户全局配置文件，避免误覆盖现有 MCP server。后续如果要做一键安装，应先做备份、diff 预览和明确确认。

Claude Code 当前推荐用 HTTP transport，并通过 CLI 管理 MCP server：

```powershell
claude mcp remove trenchbroom -s user
claude mcp add --scope user --transport http trenchbroom http://127.0.0.1:37666/mcp
claude mcp get trenchbroom
```

`Preferences > MCP` 会显示真实 URL 和可复制的 Claude Code 命令。旧 stdio
shim 仍可用于只支持 stdio 的客户端：

```powershell
claude mcp remove trenchbroom -s user
claude mcp add --scope user --transport stdio trenchbroom -- build-release-codex\app\TrenchBroomMcp\trenchbroom-mcp.exe
```

stdio 命令中的 `--` 用于分隔 Claude Code 自己的参数和 server 子进程命令。

如果 Claude Code `/mcp` 显示 `connected · no tools`，优先检查：

- MCP server 的 `initialize` 响应是否声明了 `capabilities.tools`。
- `tools/list` 是否返回稳定的已实现工具列表。不要因为 TrenchBroom 当前是 `Off` 模式就返回空工具列表。
- 工具是否被客户端的 tool search 延迟加载。可用 `claude mcp get trenchbroom` 和 `/mcp` 面板确认连接状态。
- 如果使用 stdio shim，确认 `trenchbroom-mcp.exe` stdout 只输出 JSON-RPC 消息，日志必须写 stderr。

## 测试策略

底层库测试：

- bridge config 默认值、读取、写入和 legacy token 生成。
- request/response JSON roundtrip。
- tool catalog schema、required 参数和 mode gating。
- `ReadOnly` 禁止 edit tools。

集成测试：

- bridge dispatch 在 UI thread 执行。
- `tb_status` 不初始化 Python。
- `map_snapshot` 在无活动文档时返回明确错误。
- edit tool 成功提交 transaction，异常 rollback。
- document reload/close 后 MCP object id 失效。
- MCP history 只撤销 MCP 产生的操作。

Blockout 测试：

- room 生成 floor/walls/ceiling。
- doorway 通过合法拆分墙体生成。
- stairs/ramp 只生成凸 brush。
- invalid size / empty bounds 不提交。

## Roadmap

1. [x] 文档和风险记录。
2. [x] `TbMcpLib`：配置、DTO、tool catalog、错误码和测试。
3. [x] TrenchBroom 内部 `McpBridgeServer`：默认关闭，支持 mode gating 和 `tb_status`；legacy stdio 路径保留 token 转发。
4. [x] `trenchbroom-mcp.exe`：stdio MCP server，支持 `initialize`、`tools/list`、`tools/call`。
4.1. [x] 内置 Streamable HTTP `/mcp` server：默认监听 `127.0.0.1`，让支持 HTTP 的 MCP client 直接连接 TrenchBroom。
4.2. [x] 抽出 `McpJsonRpc`：stdio shim 和 HTTP server 共用同一套 JSON-RPC / tool call 实现。
5. [x] 只读 map / selection / action tools。
5.1. [x] 文档生命周期、选择过滤和视图控制 tools。
6. [x] transaction 编辑 tools 与 MCP history。
7. [x] GoldSrc asset placement tools。
8. [x] Blockout IR tools。
9. [~] overlay 与 viewport capture：当前已有 bridge overlay state、整窗截图和 2D/3D 子视口截图；真实 overlay 绘制待接入。
9.1. [x] compile profile / leak pointfile tools。
10. [x] 用户文档、示例 prompt 和 smoke workflow：当前已有本地 smoke 脚本和面向 Agent 的工作流文档。

每个阶段都应保持可构建，并带 focused tests。涉及写操作的阶段必须先证明 transaction 和 rollback 行为稳定。
