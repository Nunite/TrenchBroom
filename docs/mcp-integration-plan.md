# TrenchBroom MCP 集成与白盒生成计划

本文档记录当前分支接入 MCP / Agent 工作流的底层架构。目标不是让
Agent 直接编辑 `.map` 文件，也不是把 Python 插件系统暴露成一个任意
执行入口，而是先建立可认证、可测试、可撤销的结构化编辑通道。

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
    | stdio / JSON-RPC MCP
    v
trenchbroom-mcp.exe
    |
    | QLocalSocket + token
    v
TrenchBroom McpBridgeServer
    |
    | UI thread dispatch
    v
MapDocument transaction / query services
```

职责划分：

- `TbMcpLib`：公共协议、DTO、错误码、tool catalog、配置读写和 JSON 序列化。
- `trenchbroom-mcp.exe`：外部 stdio MCP server，只做 MCP 协议适配和本地 bridge 转发。
- `McpBridgeServer`：运行在 TrenchBroom 进程内，接收本地请求，验证 token 和 mode，并在 UI thread 执行工具。
- `MapDocument` / 现有 UI services：唯一真实状态；MCP 不直接读写 `.map` 文件。

## 当前实现状态

截至当前分支，MCP 底层已经完成以下检查点：

- 已新增 `TbMcpLib`，包含 mode、错误码、bridge config、bridge request/response 和 tool catalog。
- 已新增 TrenchBroom 内部 `McpBridgeServer`，支持本地 `QLocalServer`、token 校验和 mode gating。
- 已新增 `trenchbroom-mcp.exe`，作为 stdio MCP server，支持 `initialize`、`tools/list`、`tools/call` 并转发到本地 bridge。
- 已接入基础查询工具：`tb_status`、`tb_doctor`、`documents_list`、`document_snapshot`、`map_snapshot`、`map_search`、`selection_get`、`actions_list`。
- 已接入第一批编辑器状态工具：`selection_set`、`overlay_set`、`overlay_clear`。
- 已接入 `action_execute`，但它要求 `Edit` 模式，因为任意 action 可能间接修改地图。
- 已接入事务型写入工具：entity 创建/更新/删除、box/wedge/cylinder brush 创建。
- 已接入 MCP operation history：`history_list`、`history_undo_mcp`、`history_redo_mcp`。当前只在 MCP 操作仍位于 TrenchBroom 原生 undo/redo 栈顶时执行，避免误撤用户手动编辑。
- 已接入 GoldSrc 资产与材质工具：`.mdl/.spr/.wav` 搜索与放置、material 搜索与应用。
- 已接入 Blockout IR 第一版：room、corridor、stairs、ramp、doorway、cover、sky shell 和 validate。

仍未完成：

- 真实 viewport overlay 渲染与 viewport capture。
- MCP 工具实现拆分。目前 `McpBridgeServer.cpp` 已承载较多 tool handler，后续应拆成 document/action/edit/asset/blockout/overlay 等模块，避免继续膨胀。

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
  "mode": "Off"
}
```

模式：

- `Off`：默认值，不启动 bridge。
- `ReadOnly`：允许读取地图、选择、动作列表，以及设置/清理 MCP overlay 等安全 UI 状态操作。
- `Edit`：允许结构化写操作，所有写入进入 transaction。
- `Danger`：预留给未来 `run_tb2_script` 或专家级能力；第一版不实现。

所有 bridge 请求必须携带 token。token 错误返回 `Unauthorized`，不会进入工具分发。

## 第一批 Tools

第一阶段只实现稳定基础能力：

- `tb_status`：返回 TrenchBroom 版本、bridge mode、当前文档数量和活动文档状态。
- `tb_doctor`：诊断 bridge 配置、token、mode、活动文档和可用工具。
- `documents_list`：列出当前打开文档。
- `document_snapshot`：返回活动文档基本信息。
- `map_snapshot`：返回地图摘要，例如 worldspawn 属性、实体数量、brush 数量和 bounds。
- `map_search`：按 classname、targetname、属性和文本搜索对象。
- `selection_get`：返回当前选择。
- `selection_set`：结构化设置选择；它只改变编辑器选择状态，不写 map 文件，因此当前允许在 `ReadOnly` 中使用。
- `actions_list`：列出可执行 actions。
- `action_execute`：执行已注册 action，遵循 action 自身 enabled 状态；由于 action 可能修改地图，当前要求 `Edit` 模式。
- `overlay_set` / `overlay_clear`：设置或清理 MCP overlay 状态；当前只保存 bridge state，后续再接入视图叠加层管理器进行真实绘制。

第二阶段已开放：

- `entity_create`、`entity_update`、`entity_delete`
- `brush_create_box`、`brush_create_wedge`、`brush_create_cylinder`
- `asset_search`、`asset_place_model`、`asset_place_sprite`、`asset_place_sound`
- `texture_search`、`texture_apply`
- `history_list`、`history_undo_mcp`、`history_redo_mcp`

第三阶段已开放 Blockout IR：

- `blockout_create_room`
- `blockout_create_corridor`
- `blockout_create_stairs`
- `blockout_create_ramp`
- `blockout_create_doorway`
- `blockout_create_cover`
- `blockout_create_sky_shell`
- `blockout_validate`

## Bridge 协议

`trenchbroom-mcp.exe` 和 TrenchBroom 内部 bridge 使用一条本地 JSON 消息协议。请求：

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

MCP overlay 不应直接散落在 renderer 临时分支中。当前第一版已使用轻量 bridge state 表达：

- object id 高亮。
- label。
- 清理 overlay。

后续必须接入 roadmap 中的“视图叠加层管理器”，与 FPS、2D readable outlines、sky、debug overlay 共享统一设置和刷新路径。接入前不要把 MCP overlay 绘制逻辑直接塞进具体 renderer。

截图工具放到第二轮后半：

- `viewport_capture_current`
- `viewport_capture_3d`
- `viewport_capture_2d`

截图必须是只读操作，不改变用户当前视图状态。

## 测试策略

底层库测试：

- bridge config 默认值、读取、写入和 token 生成。
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
3. [x] TrenchBroom 内部 `McpBridgeServer`：默认关闭，支持 token、mode 和 `tb_status`。
4. [x] `trenchbroom-mcp.exe`：stdio MCP server，支持 `initialize`、`tools/list`、`tools/call`。
5. [x] 只读 map / selection / action tools。
6. [x] transaction 编辑 tools 与 MCP history。
7. [x] GoldSrc asset placement tools。
8. [x] Blockout IR tools。
9. [ ] overlay 与 viewport capture。
10. [ ] 用户文档、示例 prompt 和 smoke workflow。

每个阶段都应保持可构建，并带 focused tests。涉及写操作的阶段必须先证明 transaction 和 rollback 行为稳定。
