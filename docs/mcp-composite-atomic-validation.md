# MCP 组合原子工具验收

本文档用于跟踪 TrenchBroom MCP 组合原子工具的验收场景。目标是沉淀可复用的建模原子，而不是把工具做成一组场景专属 prefab。

## 原则

- 一个能力至少要跨多个场景类型验证，再视为稳定 MCP primitive。
- 优先使用 `blockout_create_batch`、`brush_create_polygon_batch`、transform、metadata、operation history 和截图验收，而不是场景专属 helper。
- 生成的几何必须可检查、可选择、可 undo、可保存，并且由合法凸 brush 组成。
- 截图用于人工视觉验收，但真正判断工具质量时，要看同一个原子能力能否迁移到不同布局。
- KZ、赛道、城堡、工业和自然场景都只是测试语境，不应该变成硬编码 prefab 家族。

## 通用验收标准

每次验收场景都应该记录：

- `tb_status` 绑定信息：`processId`、`bridgeInstanceId`、`activeDocumentPath`、`documentFingerprint`。
- 场景名称和日期。
- 使用的 MCP 工具。
- operation ids。
- `history_status`：是否能 undo 最新 MCP operation，若不能，记录 `reasonIfUnavailable`。
- brush 数量和大致 bounds。
- 是按类型分阶段创建，还是一次 batch 提交。
- undo / redo 是否正常。
- `map_validate` 和 `problems_check` 是否通过。
- `operation_validate` 是否报告对象仍然 live。
- terrain/heightmap 场景必须先记录 `heightmap_preview_grayscale` 的 `estimatedBrushCount`、`outputBounds`、`warnings` 和 `suggestedParams`。
- 截图路径或 `viewport_capture_scene_review` 输出。
- 本次使用中发现的工具摩擦。
- 后续需要的工具调整。

场景通过必须满足：

- 主体结构能从截图里识别出来。
- 转角、连接、端点和重复元素是连贯的。
- 没有明显多余棍状物、缝隙、孤立 brush 或悬空支撑。
- 对象身份仍能通过 `history_list`、`operation_inspect` 和 `operation_validate` 追踪。
- 同一套工具模式至少看起来能迁移到另一类场景。

## 验收场景

### 1. 山路赛道

主要能力：

- 路径地面生成。
- 曲线和分段转弯。
- 边缘护栏或防撞墙。
- 跟随地形或高度变化的支撑。
- 路线宽度和高度变化。

可用工具：

- `blockout_create_batch`，配合 `path_ribbon`、`box`、`prism`、`repeat_translate` 和 `support_posts_between`。
- `brush_create_polygon_batch`，用于自定义路肩或标记。
- `viewport_capture_scene_review`。

验收重点：

- 路面在转弯处连续。
- 两侧护栏贴边，不漂移。
- 支撑位于高架段下方，而不是从路面穿出来。
- 同一套原子能力可以复用于桥梁、峡谷路径或 KZ route。

状态：未运行。

### 2. 城堡城墙巡逻道

主要能力：

- 沿多段路径生成墙体条带。
- 可行走的墙顶。
- 转角连接。
- 重复垛口、缺口和塔楼连接点。

可用工具：

- `path_ribbon`，用于墙顶巡逻道。
- `repeat_translate` / `repeat_grid`，用于垛口。
- `brush_create_polygon_batch`，用于塔楼 footprint 和斜角转角。

验收重点：

- 墙顶可行走且贴 grid。
- 转角连接干净闭合。
- 垛口间距稳定，并且不和转角重叠。
- 塔楼连接点清晰且可选择。

状态：未运行。

### 3. 峡谷木栈道

主要能力：

- 分段路径平台。
- 栏杆。
- 不规则路径下方的柱子和斜撑。
- 路线方向可读性。

可用工具：

- `path_ribbon` 或 polygon batch，用于栈道平台。
- `support_posts_between`，用于支撑柱。
- `repeat_translate`，用于木板或栏杆段。

验收重点：

- 栈道转弯有清晰方向，而不是 box spam。
- 栏杆贴住边缘。
- 支撑延伸到预期底部高度。
- 路径可以分阶段提交，避免一个非法装饰件阻止主栈道落地。

状态：未运行。

### 4. 地下管道或下水道

主要能力：

- 圆形或半圆形通道近似。
- 弯曲管道转角。
- 侧向出口。
- 分段连续性。

可用工具：

- `brush_create_cylinder_sector`。
- `blockout_create_batch`，配合 `cylinder_sector`、`cylinder`、`path_ribbon`、`box` 和 `prism`。

验收重点：

- 内部通道跨分段后仍然连贯。
- 侧向出口能干净接到管壁。
- 圆形近似对 GoldSrc 白盒来说足够 grid-safe。
- 不生成非法或凹 brush。

状态：未运行。

### 5. 工厂传送带与维修平台

主要能力：

- 重复机械模块。
- 平台链。
- 栏杆、支撑框架、斜坡和楼梯作为可复用原子。
- 对重复元素进行贴图和 face 操作。

可用工具：

- `repeat_translate`、`repeat_grid`、`box`、`prism`。
- `brush_create_boxes_batch`。
- 贴图和 face 工具。

验收重点：

- 重复结构在 MCP 调用和 operation history 中保持紧凑。
- 支撑和栏杆对齐传送带或平台边缘。
- face / texture 编辑不需要长期传递大 object id 列表。
- 创建后能 inspect 和 select 对应 operation。

状态：未运行。

### 6. KZ 曲线 Bhop 路线

主要能力：

- route metadata。
- 非 box 平台 footprint。
- 曲线上升路径组合。
- 通过平台形状和朝向表达玩家意图。

可用工具：

- `shape_library_list`。
- `brush_create_polygon_batch`。
- `brush_metadata_set`、`selection_by_metadata`。
- `route_geometry_analyze_chain`，只用于返回几何事实。

验收重点：

- 路线能表达起跳边和落点窗口。
- 平台不只是统一 box chain，除非用户明确要求。
- 难度判断保留给 Agent skill 和人工 review，不交给 MCP 静态结论。
- metadata 能找回 route，不需要在上下文里携带很长 object id 列表。

状态：未运行。

### 7. 寺庙台阶与阶地

主要能力：

- 分层阶地。
- 对称重复。
- 柱列。
- 轴向对齐和多边形平台组合。

可用工具：

- `stepped_mass`。
- `repeat_grid`。
- `brush_create_cylinder`。
- `brush_create_polygon_batch`。

验收重点：

- 阶地看起来是有意图的层级，而不是随机堆块。
- 柱列与路线和入口轴线对齐。
- 楼梯或斜坡能连接不同层级。
- 同一套原子能力可以复用于广场、防御工事或竞技场看台。

状态：未运行。

### 8. 自然洞穴白盒

主要能力：

- 地形或高度图表面。
- 不规则但仍凸的岩石平台块。
- 岩柱和洞口。
- 视觉复杂区域的自适应细分。

可用工具：

- `heightmap_import_grayscale`。
- `brush_create_polygon_batch`。
- `brush_create_cylinder` / `brush_create_cone`。
- `blockout_create_batch`，按地形、洞口和支撑分阶段提交。

验收重点：

- 洞穴轮廓不是纯矩形。
- 复杂区域能使用更细的 cell 或更小 polygon。
- 生成的 brush 仍然可选择、可保存。
- 截图验收能区分路线、洞口和主要体块。

状态：未运行。

## 优先队列

1. 山路赛道：验证路径、护栏、支撑和高度变化。
2. 城堡城墙巡逻道：验证转角连接、重复结构和可行走厚度。
3. KZ 曲线 Bhop 路线：验证 route metadata 和玩家意图 footprint。
4. 地下管道或下水道：验证曲线和圆形 brush 组合。

这四个场景应该优先运行，因为它们能在不依赖最终美术材质的情况下，同时压测路径组合、重复结构、曲线几何、对象身份、metadata 和截图验收。

## Findings 记录

每次真实 MCP 跑图后，在下面追加记录。

### 模板

- 日期：
- 场景：
- 地图：
- 绑定状态：
- History 状态：
- 使用工具：
- Operation ids：
- Heightmap preview：
- 结果：
- 截图验收：
- 发现问题：
- 建议工具调整：
- 已实现工具调整：
- Commit：
