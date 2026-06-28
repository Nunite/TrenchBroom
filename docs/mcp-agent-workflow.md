# TrenchBroom MCP Agent 工作流

本文档面向接入 MCP 的 Agent / Client，说明如何安全地使用当前分支暴露的工具完成“读取地图、生成白盒、放置资产、贴图、截图检查、验证、保存”的闭环。

底层架构和工具状态见 `docs/mcp-integration-plan.md`。这里不重复协议实现细节，只记录推荐工作方式和可直接复用的 Agent 指令。

## 基本原则

- TrenchBroom 是唯一真实状态；不要让 Agent 直接改 `.map` 文件。
- 写操作只使用 MCP 结构化工具，依赖 TrenchBroom 自身 transaction 和 undo/redo。
- 默认先用 `ReadOnly` 做诊断、查询、截图和 overlay；只有确实要修改地图时再切到 `Edit`。
- 白盒生成优先使用 Blockout IR 工具，不直接拼 brush 顶点。
- 所有尺寸默认使用 GoldSrc units，优先按 16 units 网格对齐。
- 每轮大修改后立刻调用 `map_validate` / `problems_check`，必要时用 `history_undo_mcp` 回滚最近一次 MCP 操作。
- 保存、关闭 dirty 文档、运行编译这类动作要由用户明确授权，Agent 不应静默执行。

## 启动检查

在 MCP client 中连接 `trenchbroom-mcp.exe` 后，Agent 应先执行：

1. `tools/list`
2. `tb_status`
3. `tb_doctor`
4. `documents_list`

判断规则：

- `tools/list` 为空且 `tb_status` 返回 `Forbidden`：TrenchBroom MCP mode 仍是 `Off`。
- `tb_status` 连接失败：TrenchBroom 未运行、bridge 未启动，或配置 token/pipeName 不匹配。
- `tb_status.processId`、`bridgeInstanceId`、`activeDocumentPath`、`documentFingerprint` 是本轮请求链路的身份锚点。写入前必须确认它们指向预期 TrenchBroom 进程和预期地图。
- `documents_list` 为空：需要先让用户打开地图，或在 `Edit` mode 下使用 `documents_open_verified` 打开绝对路径。
- `tb_doctor` 报告活动文档不存在：不要执行 map / selection / edit 工具，先激活或打开文档。
- 多个 TrenchBroom 进程同时存在时，MCP HTTP 端口只控制该端口 owner 所属进程；不要假设它就是屏幕上最前面的 TB。`scripts/mcp-call.ps1` 会显示端口 owner PID，并可与 `tb_status.processId` 对照。

本地快速 smoke 可用：

```powershell
scripts\mcp-smoke.ps1 -RawJson
```

如果 Codex / Claude 的 MCP 会话状态失效，但 TrenchBroom 本地 HTTP MCP 端口仍在，可以绕过客户端注册状态直接请求：

```powershell
scripts\mcp-call.ps1 -Tool tb_status
scripts\mcp-call.ps1 -ListTools
scripts\mcp-call.ps1 -Tool map_snapshot -RawStructured
scripts\mcp-call.ps1 -Tool selection_by_bounds -ArgumentsJson '{"min":[0,0,0],"max":[256,256,128],"detail":"full"}'
scripts\mcp-call.ps1 -ResourceUri tbmcp://operation/mcp-op-1
```

需要从命令行启动最新 Release TrenchBroom 并请求一次工具时：

```powershell
scripts\mcp-call.ps1 -Launch -KeepOpen -Tool tb_status
```

切换地图建议使用 verified open：

```powershell
scripts\mcp-call.ps1 -Tool documents_open_verified -ArgumentsJson '{"path":"D:/absolute/path/to/map.map","waitMs":5000,"activate":true}'
```

生成 MCP client 配置片段可用：

```powershell
scripts\mcp-config.ps1 -Print
```

脚本会输出通用 JSON、`mcpServers` JSON 和 Codex TOML 片段。第一版只生成文件，不直接覆盖用户全局配置。

## 推荐 Agent 指令

可以把下面这段作为 MCP client 中给 Agent 的项目指令：

```text
你正在通过 TrenchBroom MCP 控制一个正在运行的 TrenchBroom 实例。

工作规则：
1. 先调用 tb_status、tb_doctor、documents_list，确认 MCP 已启用且有活动文档，并记录 processId、activeDocumentPath、documentFingerprint。
2. 查询地图时使用 document_snapshot、map_snapshot、map_search、selection_get、fgd_entities_list、textures_list、asset_search。
3. 修改地图时只使用结构化 MCP 工具，不直接写 .map 文件，也不运行任意 Python 脚本。
4. 白盒生成必须优先使用 `blockout_validate` 和 `blockout_create_batch` 的 primitive operations；不要直接生成任意 brush 顶点。
5. 所有坐标和尺寸使用 GoldSrc units，并尽量按 16 units 网格对齐。
6. 每次写操作后读取 operationId，需要时再用 `operation_inspect(detail=ids)` 取对象 id；用截图工具检查，不要长期携带大 object id 列表。
7. 发现结果错误时优先使用 history_undo_mcp 回滚最近一次 MCP 操作，而不是用删除工具硬删一片对象。
8. 大型或破坏性写入应传 expectedDocumentPath，防止写到错误地图。
9. 保存、关闭 dirty 文档、运行 compile_run 前必须先说明影响并等待用户确认。
10. 如果工具返回 unsupported、Forbidden 或 Unauthorized，不要猜测修复；先向用户报告缺少的 mode、权限或当前实现限制。
```

## 读取地图

推荐顺序：

1. `document_snapshot`：确认活动文档路径、game config、dirty 状态。
2. `map_snapshot`：读取 worldspawn、实体数量、brush 数量、地图 bounds。
3. `map_search`：按 classname、targetname、属性或关键词查找对象。
4. `selection_get`：读取用户当前选择。
5. `selection_filter` / `selection_by_bounds`：按类型、材质、范围筛选对象。
6. `viewport_capture_current` 或 `viewport_capture_3d`：获得视觉反馈。
7. 场景自验收优先用 `render_review_operation` 或 `render_review_current_scene(scope=mcp_history)`：它们只渲染目标几何，默认 contact sheet 最多拼 2 张图，避免 Agent 读图时每格过小；所有单独 PNG 仍写入 manifest。
8. `viewport_capture_scene_review` 只作为真实 TrenchBroom 视口辅助调试使用。需要确认 UI 视角或用户当前视图时再调用；自动化验收不要依赖它来隔离对象。

对象操作必须使用 MCP 返回的 `objectId`，不要根据实体顺序或 UI 文本猜测内部指针。

## 白盒生成流程

适合“根据参考图搭一个 CS1.6 白盒”的最小闭环：

1. 先让 Agent 提炼布局，包括主房间、走廊、楼梯、门洞、掩体、出生点和目标点。
2. 给出 rough bounds，例如房间 `[0,0,0]` 到 `[512,384,192]`。
3. 确认全部尺寸按 16 units 对齐。
4. 对每个结构调用 `blockout_validate`，先检查尺寸、厚度、grid 和非凸风险。
5. 优先用 `blockout_create_batch` 一次提交 typed object `operations[]`，默认选择 primitive operations：`box`、`prism`、`cylinder`、`cylinder_sector`、`polyhedron`、`path_ribbon`、`repeat_translate`、`repeat_grid`、`stepped_mass`、`support_posts_between`。例如 `{"type":"box","min":[0,0,0],"max":[128,128,16]}`、`{"type":"path_ribbon","points2d":[[0,0],[512,0],[768,256]],"width":160,"minZ":0,"maxZ":16}`。
6. 只需要批量创建平台/跳块时用 `brush_create_boxes_batch`，传 `boxes: [{min,max,material?}]`；需要棱形、切角或引导形状时用 `brush_create_polygon_batch`，避免多次单 brush 调用产生大量 undo/history。
7. `blockout_create_room`、`blockout_create_corridor`、`blockout_create_stairs`、`blockout_create_ramp`、`blockout_create_doorway`、`blockout_create_cover`、`blockout_create_sky_shell` 属于 convenience helpers，只用于快速草图、旧 workflow 兼容或用户明确要求，不作为 Modeling 默认路径。
8. 默认只保留返回的 `operationId` / `resourceUri`，需要 ids 时再用 `operation_inspect(detail=ids)`。
9. 写入工具支持 `expectedDocumentPath`。当 Agent 已经从 `tb_status` 记录了目标地图路径时，大型批量创建、删除、贴图替换、heightmap import、实体创建和保存都应传这个 guard。
10. 用 `render_review_operation` 收集几何 review 包；复杂场景默认看 `preferredCapturePath` 的 2 图 contact sheet，必要时再打开 manifest 里的单独视图 PNG。
11. 根据截图调整尺寸或位置，必要时使用 `objects_transform` 平移/旋转/缩放。
12. 调用 `history_status` 确认最近 MCP operation 是否还在 undo 栈顶；再调用 `map_validate` / `problems_check`。

不要让 Agent 直接使用 `brush_create_from_planes` 做普通白盒。它是 `Full` / 搜索可发现的专家工具，只适合已有明确 plane 数据且需要严格校验的场景。复杂批量结构也不要默认走 `python_generate_blockout`；除非用户明确要求脚本生成，优先用 typed batch primitive operations 分阶段落地。

## 斜面 / Surf / Slide 验收

涉及 `ramp`、`wedge`、`ramp_between`、surf、slide 或任何可跑斜面时，不能只看 `operation_validate` 和 review 截图。`operation_validate` 只能证明对象仍 live，review 图只能证明大体结构可读；它们不能证明坡面沿路线方向上升还是下降。

推荐流程：

1. 小样优先：先用一段 `ramp_between` 验证工具语义，再批量生成路线主体。
2. 新路线优先用 `{"type":"ramp_between","start":[...],"end":[...],"width":...,"thickness":...}`，表达“沿 start -> end 行进”；旧 `ramp(min,max,axis)` 只用于兼容或快速草图。
3. 批量创建后立即调用 `geometry_analyze_slopes(operationId=..., start=..., end=...)` 或传 `routeDirection`。
4. 检查每个候选坡面返回的 `normal`、`slopeDegrees`、`riseDirection`、`heightDeltaAlongRoute`、`classification`。预期上坡应看到 `classification=ascending` 且 `heightDeltaAlongRoute > 0`；反向复测应暴露 `descending`。
5. 再调用 `geometry_analyze_route_continuity(operationId=..., start=..., end=...)` 检查相邻可跑面。重点看 `continuous`、每个 seam 的 `verticalStep`、`horizontalGap` 和 `classification`；例如 ramp 顶面接到平台底面时会报 `step_up` 和约等于平台厚度的 `verticalStep`。
6. 最后看 `render_review_operation` 的 contact sheet。默认最多 2 张并列；如果需要更多视角，打开单独 PNG，不要把过多视图挤进一张图。
7. 复杂路线按主体、护边、标记分阶段创建，保留少量关键 `operationId`，降低 history 追踪和回滚成本。

## KZ 平台链工作流

这里使用的是通用 route/metadata/shape MCP 工具；KZ 只是 Agent skill 和用户语境，不是 TrenchBroom MCP 内部模块。CS1.6 KZ route 设计不要默认生成一串大 box。平台形状应表达玩家意图：起跳边、落点窗口、下一跳方向和容错。当前推荐流程：

1. `shape_library_list`：读取支持的 footprint vocabulary，例如 `diamond`、`trapezoid`、`chamfered_rect`、`half_hex`、`arrowhead`。
2. `brush_create_polygon_batch`：一次提交多个凸多边形平台。每个平台传 `points2d`、`minZ`、`maxZ`、可选 `material` 和 `metadata`。
3. 在 metadata 中写入 `routeId`、`movementType`、`intent`、`difficulty`、`incomingDirection`、`outgoingDirection` 等信息。metadata 第一版只在 MCP session 内有效，不写入 `.map`。
4. `selection_by_metadata(routeId=..., select=true)`：需要继续编辑某段 route 时按 metadata 找回对象，不要在上下文中长期携带大 object id 列表。
5. `route_geometry_analyze_chain`：用 ordered `objectIds` 或 `routeId` 计算 `edgeGap`、`effectiveDistanceIdeal`、`effectiveDistanceBadLanding`、`heightDelta`、`lateralOffset` 和 `landingWindowArea`。
6. 结果只能作为静态几何事实，不能承诺 CS1.6 实机可通关，也不负责 hard/god 等难度裁判。难度语感应由 Agent 的 KZ/CS1.6 skill 结合路线意图、movement 类型和实测反馈判断；`sv_airaccelerate`、server plugin、玩家速度、落地状态和高度差都会影响实际难度。

示例 polygon batch：

```json
{
  "transactionName": "MCP: Route intro platforms",
  "grid": 16,
  "detail": "ids",
  "brushes": [
    {
      "points2d": [[0, 32], [32, 0], [64, 32], [32, 64]],
      "minZ": 0,
      "maxZ": 16,
      "metadata": {
        "routeId": "intro",
        "movementType": "bhop",
        "intent": "takeoff",
        "outgoingDirection": [1, 0, 0]
      }
    },
    {
      "points2d": [[160, 0], [224, 0], [224, 64], [160, 64]],
      "minZ": 0,
      "maxZ": 16,
      "metadata": {
        "routeId": "intro",
        "movementType": "bhop",
        "intent": "landing",
        "incomingDirection": [1, 0, 0]
      }
    }
  ]
}
```

如果需要凹形平台，先把 footprint 拆成多个凸 polygon，再用同一个 `brush_create_polygon_batch` transaction 提交。不要用 `brush_create_from_planes` 作为常规 KZ 平台生成路径。

## 资产与实体放置

GoldSrc / CS1.6 资产优先走统一资产工具：

1. `asset_search` 搜索 `.mdl`、`.spr`、`.wav`。
2. 模型用 `asset_place_model`。
3. Sprite 用 `asset_place_sprite`。
4. 声音用 `asset_place_sound`，默认创建 `ambient_generic` 并写入 `message`。
5. 放置后用 `selection_set` 或 `overlay_set` 标记结果。

实体优先走 FGD schema：

1. 已知 classname 时优先用 `entity_create_checked`，它会确认当前 FGD 支持该 point entity，再按 schema/defaults 创建。
2. 同时创建多盏 `light`、多个 spawn、route marker 或其他 point entity 时用 `entity_create_checked_batch`，一次验证 FGD、一次 transaction、一次 undo，不要连续调用很多次 `entity_create_checked`。
3. 不确定 classname 时用 `fgd_entities_list` 查找，再用 `entity_schema` 读取默认属性、类型和说明。
4. 需要完全显式控制 schema 流程时再用 `entity_create_from_schema` 创建 point entity。
5. `entity_update` 只修改明确给出的属性，不清空未知字段。

Brush entity 工作流：

1. 用 `selection_filter` 或 `selection_by_bounds` 找到 world brushes。
2. `entity_tie_brushes` 绑定成 brush entity。
3. 需要拆回 world 时使用 `entity_untie_brushes`。

## 材质与 Face 编辑

推荐顺序：

1. `textures_list` / `texture_search` 找材质。
2. `texture_lock_get` 查看 Texture Lock / UV Lock；需要固定后续几何变换的贴图行为时用 `texture_lock_set`。
3. 批量给一组 brush 贴材质时优先用 `texture_apply_by_filter`，它内部只编辑 brush faces，不需要先拼 object ids。
4. `face_list` 查看目标 brush 的 face index、法线和材质。
5. 单面修改用 `face_select` + `face_texture_set`，或 `texture_apply` 带 `objectId` / `faceIndex`。
6. 批量替换用 `texture_replace`，默认优先 `scope=selection`，谨慎使用全图替换。
7. 对齐用 `texture_align_face`，当前稳定模式为 `reset`、`paraxial`、`parallel`。
8. 复制贴图属性用 `texture_copy_from_face`。

删除或变换前，如果需要按条件找对象，优先用 `selection_filter` 的 `excludeWorld=true`、`selectableOnly=true`、`leafOnly=true` 参数。真正要按条件删除时用 `objects_delete_by_filter`，不要把 `selection_filter` 返回的父节点和子节点混在一起手动传给 `objects_delete`。

贴图工具返回错误时不要尝试直接改 UV 原始数据；先缩小到单个 face，再确认 face index 是否仍有效。

## 编译与 Leak 检查

编译会运行外部工具，必须由用户授权，并要求 MCP `Edit` mode：

1. `compile_profiles_list` 查看可用 profile。
2. 用户确认后调用 `compile_run`。
3. 轮询 `compile_log_tail` 查看输出。
4. 如果发现 leak pointfile，调用 `leaks_load_pointfile` 加载 `.pts` / `.lin`。
5. 用 `viewport_capture_3d` 截图确认 leak path。

第一版 `compile_run` 复用 TrenchBroom 编译对话框，返回的是 `started=true`，不是阻塞等待编译结束。

## 回滚与保存

MCP 写操作会返回：

- `operationId`
- `transactionName`
- `changedObjectIds`

推荐规则：

- 一次布局阶段完成后调用 `history_list`，查看最近 MCP 操作时间线。每条记录包含 `createdAt`、`createdAtMs`、`toolName`、`transactionName`、`changedObjectCount`；有活动文档时还包含 `liveObjectCount`、`staleObjectCount` 和 `valid`。
- 需要查看某次操作创建/修改的对象时调用 `operation_inspect(detail=ids)`；需要检查对象是否仍然有效时调用 `operation_validate`。
- 删除类 operation 会标记 `operationKind=delete`，并把被删除对象放在 `deletedObjectIds` / `deletedObjectCount` 中。不要把 delete operation 的 `liveObjectCount` 当作“被删对象还活着”；删除记录主要用于审计和 undo/redo。
- `selection_get` 中 `brushFaceCount` 表示当前选择的 brush face 数量；整 brush 选择的总 face 数使用 `selectedBrushFaceCount` / `selectedBrushTotalFaceCount`。
- 发现最近一次 MCP 操作错误时调用 `history_undo_mcp`。
- 不要用 `objects_delete` 代替 undo，除非用户明确要删除对象。
- 保存前调用 `map_validate` 和 `problems_check`。
- 用户确认后再调用 `documents_save` 或 `documents_export`。
- 关闭 dirty 文档必须显式传入 `discardChanges=true`；否则工具会拒绝，避免弹阻塞对话框。

## 示例：创建一个小型 CS1.6 白盒

推荐工具序列：

1. `tb_status`
2. `map_snapshot`
3. `textures_list` 或 `texture_search`
4. `blockout_validate` 检查主体 bounds、走廊宽度、楼梯/斜坡尺寸和 sky shell 厚度
5. `blockout_create_batch` 用 primitive operations 一次或分阶段提交主体几何，例如房间墙/地/顶用多个 `box`，弧形或折线路线用 `path_ribbon`，重复支撑用 `repeat_translate` / `repeat_grid`
6. `brush_create_polygon_batch` 创建需要玩家引导的非矩形平台、切角平台或箭头形落点
7. `entity_create_checked_batch` 放置 spawn、基础 light 或目标点；创建前先确认 FGD/schema
8. `viewport_capture_scene_review` 做视觉验收
9. `map_validate` / `problems_check`
10. 用户确认后 `documents_save`

如果材质名不确定，先用 `texture_search` 查找 `dev`、`clip`、`sky` 等关键词；不要硬编码不存在的材质。

## 当前限制

- MCP 默认关闭，需要用户在 Preferences 中启用。
- `Danger` 模式和任意 `tb2` 脚本执行还没有开放。
- `prefabs_list` / `prefab_create` 只是 catalog 占位，当前不出现在默认可调用工具里。
- `brush_create_arch` / `brush_create_torus` 当前标记为 unsupported。
- Overlay 第一版能画 object bounds、point、bounds 和 label，但还不是全局通用 overlay manager。
- Viewport capture 截取的是当前 UI 控件状态，不是独立离屏渲染器。
- MCP history 只在 MCP 操作仍位于 TrenchBroom 原生 undo/redo 栈顶时可靠；用户手动编辑插入后，Agent 应重新读取状态再继续。
