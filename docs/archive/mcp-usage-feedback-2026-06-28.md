# TrenchBroom MCP 使用体验反馈（转开发 Agent）

日期：2026-06-28
反馈来源：2026-06-27 到 2026-06-28 的真实场景迭代
场景：`山路赛道`、`悬崖赛道`、`terrain / heightmap`、`多文档切换`
环境：Windows，TrenchBroom `2026.1-RC3`，本地 HTTP MCP，`scripts/mcp-call.ps1`

## 总评

- 如果只看建模能力，当前 MCP 大约是 `8/10`。
- 如果看真实使用体验，当前大约是 `5/10`。

结论不是“工具不能用”，而是“核心建模能力已经够用，但文档切换、实例绑定、会话稳定性和 terrain 参数体验还不够工程化”。

## 本轮实际完成了什么

- 用 `blockout_create_batch` 生成了山路 / 悬崖赛道主体。
- 用 `heightmap_import_grayscale` 多轮导入 terrain，并最终做出可验收的 `cliffside` 版本。
- 反复使用了 `map_snapshot`、`history_list`、`map_validate`、`problems_check`、`viewport_capture_scene_review`。
- 真实踩到了文档切换、MCP 绑定错误实例、history 断裂、heightmap 参数调优过硬等问题。

## 当前明显好用的地方

- `blockout_create_batch` 很有价值。`path_ribbon`、`support_posts_between` 这类 primitive 已经能支持真实白盒场景。
- `heightmap_import_grayscale` 虽然还不够顺手，但能力本身是成立的，确实能导出有意义的 brush terrain。
- `map_validate` 和 `problems_check` 组成了可靠闭环，适合每轮修改后立刻收口。
- 结构化错误信息整体可用。例如参数错误、brush 数超限、operation validation 失败时，定位比纯黑盒工具强很多。

## 主要问题

### 1. MCP 容易绑定到错误的 TrenchBroom 进程

这是本轮最痛的问题。

- 同时开了多个 TrenchBroom 窗口时，MCP 实际绑定到哪个窗口不透明。
- 本轮一度同时存在三个进程：
  - `mcp_composite_01_cliffside_road.map`
  - `mcp_composite_01_mountain_road_track.map`
  - `unnamed.map`
- 通过 `Get-NetTCPConnection -LocalPort 37666` 确认，HTTP MCP 端口实际绑在 `unnamed.map` 对应进程上，而不是我们正在迭代的赛道图。
- 结果是：
  - `tb_status` 看起来是正常的
  - `map_snapshot` 也能返回结果
  - 但操作落在错误地图上，或者 Agent 误以为自己在操作正确地图

影响：

- 这是高严重度问题，因为它会直接破坏 Agent 对“当前真实状态”的判断。
- 它不是单纯的易用性问题，而是状态一致性问题。

建议：

- `tb_status` 增加 `processId`、`windowTitle`、`bridgeOwnerPid`、`activeDocumentWindowTitle`。
- 增加一个显式工具，类似 `tb_bind_window` 或 `documents_open_and_bind(path)`。
- 如果当前系统里有多个 TrenchBroom 进程，MCP 应明确返回“当前绑定的是哪一个”，而不是让 Agent 猜。

### 2. `documents_open` 仍然不够稳定

这个问题本轮再次复现，和现有文档里的历史记录一致。

- `documents_open` 有时返回 `opened=true`。
- 但下一次紧跟着的 `map_snapshot` / `history_list` 会出现 `502 Bad Gateway`。
- 有时桥接恢复后，活动文档又回到了 `unnamed.map`，而不是刚刚打开的目标 map。

影响：

- Agent 很难把“切图”当成一个可靠原子操作。
- 这会逼着 Agent 反复做重连、确认、再确认，严重拖慢场景迭代。

建议：

- 提供一个更强语义的工具，例如：
  - `documents_open_and_wait`
  - `documents_open_clean_map`
  - `documents_open_verified`
- 该工具应在返回成功前完成三件事：
  - 文档已经打开
  - 活动文档路径已经切换到目标路径
  - bridge 在后续一次健康检查中仍然可用

### 3. `history` 过度依赖当前 bridge 会话

本轮多次出现下面的情况：

- 地图里明明还有 live geometry。
- 但 `history_list` 变成空。
- `history_undo_mcp` 返回 “No MCP operation is available to undo”。
- 之前的 `operationId` 也可能无法再 `operation_inspect`。

影响：

- Agent 丢失了最重要的“可回滚性”。
- 在大批量几何迭代里，这会迫使 Agent 退回到删对象或重开图的策略。

建议：

- 至少要把“为什么 history 不可用”说清楚。
- 更理想的方案是让 history 更明确地区分：
  - 当前 bridge session 是否连续
  - 当前 document 是否同一份
  - 当前 undo 栈是否已脱离 MCP 连续区间
- 如果做不到持久 history，也建议提供一个明确的状态工具，而不是返回空列表让 Agent自己推断。

### 4. `heightmap_import_grayscale` 参数过硬，成功路径不够直观

能力本身是有的，但调参成本明显偏高。

本轮反复要在人脑里平衡这些参数：

- `cellSize`
- `maxSize`
- `heightScale`
- `minCellSize`
- `maxCellSize`
- `errorTolerance`
- `maxBrushes`

典型问题：

- 参数一激进就超 `maxBrushes`。
- 参数一保守，地形就太粗。
- 返回 `valid=false` 时虽然是正确的，但 Agent 往往还得额外理解“为什么没有 commit”。
- 成功 commit 后，如果 heightmap 源图本身有问题，MCP 不会提示“几何语义可能不符合预期”，Agent 只能靠截图二次发现。

影响：

- terrain 工作流可用，但心智负担明显高于 `blockout_create_batch`。
- 它更像一个“低层能力”，还没长成高频迭代时足够顺手的中层 primitive。

建议：

- 增加 `heightmap_preview_grayscale` 或 dry-run 模式，至少返回：
  - 预计 brush 数
  - 采样尺寸
  - 预计 bounds
  - 是否会 commit
- 错误信息里可以更明确区分：
  - 参数合法但不会提交
  - 参数合法且会提交
  - 参数合法但 geometry 很可能过粗或过密
- 如果后续考虑中层工具，建议不要做“山路 prefab”，而是做更通用的：
  - `terrain_patch_from_grid`
  - `terrain_profile_strip`
  - `cliff_band_from_path`

### 5. 截图验收依赖当前相机，导致“几何对了但截图不一定对”

本轮有多次“图已经做出来了，但截图读起来不对”的情况。

- `viewport_capture_scene_review` 有用，但仍然强依赖当前 UI 相机状态。
- 3D 截图经常会因为相机太低、太近、太偏，导致场景被误读。
- terrain / 悬崖这类几何尤其容易被局部截图误导。

影响：

- AI 验收会被相机角度放大偏差。
- 工具本身正确，但 review 反馈不稳定。

建议：

- 支持更稳定的 preset：
  - overview orbit
  - route-following angle
  - top-fit
  - side cliff profile
- 支持按 `operationId` 或 bounds 自动 framing，而不是只依赖当前 camera。

## 本轮最值得优先修的点

如果只能先修三项，我建议优先级如下：

1. 明确 MCP 当前绑定的 TrenchBroom 进程和文档。
2. 提供稳定的 `open + bind + verify` 文档切换能力。
3. 提供更可预期的 `heightmap` 预览 / dry-run 工作流。

原因很简单：

- 第 1 和第 2 项解决的是“状态真实性”问题。
- 第 3 项解决的是“terrain 迭代效率”问题。

只要前两项不稳，Agent 即使几何能力再强，也会被错误状态拖住。

## 对开发 Agent 的建议

- 不建议先做更多场景专属 helper。
- 建议先补“状态绑定”和“切图可靠性”。
- terrain 方向建议继续做通用 primitive，不要退化成 hardcode prefab。
- 文档里已经多次提到 `documents_open` 不稳定，这次真实使用再次复现，优先级可以再提高。

## 一句话结论

当前 MCP 已经具备“能做真实白盒场景”的建模能力，但要让开发 Agent 和用户都放心高频使用，还需要优先补齐：

- 进程 / 文档绑定透明度
- 切图稳定性
- history 可解释性
- terrain 预览与调参体验
