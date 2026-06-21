# 自定义功能路线图

本文档用于记录当前分支值得继续推进的自定义功能候选项。总体原则是：
保持 TrenchBroom 轻量、快速、专注 BSP/关卡编辑，不把它改造成 Blender
或 UE5 那种大而全的编辑器。

## 现代化编辑器待办

- [ ] 命令面板
  - 目标：让菜单、工具、视图开关、插件命令更容易被发现，不继续堆工具栏和菜单。
  - 可能入口：`ActionManager`。

- [ ] 统一资产浏览器
  - 目标：保留现有 Model Browser 的工作成果，并扩展成可放置材质、模型、Sprite、声音和实体模板的资产入口。
  - 重点：优先服务 GoldSrc/CS 1.6 的真实资产流。

- [ ] 视图叠加层管理器
  - 目标：统一管理 FPS、sky、2D 可读线条、grid、edge、fog、entity overlay 和后续 debug overlay。
  - 重点：避免每个显示功能都散落在不同菜单或临时开关里。

- [ ] Python 插件 v2 生产级收尾
  - 目标：让 `tb2` 优先稳定插件生命周期、卸载清理、错误展示和示例文档，再扩展大 API 面。
  - 重点：不要继续扩大 legacy `tb` 兼容负担。

- [ ] Outliner 搜索、过滤、锁定、隐藏、solo 和批量属性编辑
  - 目标：改善大型地图的对象管理，不改动 map 文件格式。
  - 重点：选择同步、层级显示和属性编辑要保持稳定。

- [ ] Prefab / 实体模板系统
  - 目标：保存并放置常用 entity/brush 组合，例如门、路径链、灯光、Sprite、CS 玩法实体。
  - 重点：先做轻量模板，不做 UE 蓝图式大系统。

- [ ] 更好的崩溃和诊断报告
  - 目标：记录最近操作、地图状态、游戏配置、插件状态和 Python 错误，让崩溃报告更可定位。
  - 重点：开发版优先，减少“崩了但没有线索”的情况。

## GoldSrc / CS 1.6 高价值待办

- [ ] GoldSrc 资产浏览器：MDL、SPR、WAD 纹理和声音
  - 优先级：高。
  - 原因：GoldSrc 教程仍然指出 TrenchBroom 虽然能加载 Sprite 和模型，但没有方便的 Sprite/Model 路径选择器，很多时候只能手填路径。
  - 第一阶段：可搜索的 model/sprite 列表、预览，以及拖放/放置到 `cycler`、`cycler_sprite`、`env_sprite`、`ambient_generic` 和 model 属性。

- [ ] GoldSrc 智能实体编辑器
  - 优先级：高。
  - 原因：GoldSrc FGD 中的 `studio`、`sprite`、`sound` 等属性有明确语义，但纯文本编辑要求用户记住路径和实体约定。
  - 第一阶段：为 `studio`、`sprite`、`sound` 和常见 Counter-Strike 实体属性提供路径选择控件。

- [ ] CS 1.6 配置向导
  - 优先级：高。
  - 原因：新用户配置 GoldSrc/CS 1.6 时通常要处理游戏路径、mod 目录、WAD、FGD、VHLT/SDHLT 编译工具和编译 profile。
  - 第一阶段：检测 Half-Life/Counter-Strike 安装目录，配置 game path、mod、编译工具、常用 WAD 和默认 VHLT profile。

- [ ] GoldSrc / VHLT 编译日志分析器
  - 优先级：高。
  - 原因：GoldSrc 编译日志会暴露 leak、missing texture、clipnodes、实体限制、资源限制等高频问题。
  - 第一阶段：解析 `-chart` 输出，显示资源使用进度条，并把常见错误链接到解释或地图视图。

- [ ] GoldSrc 限制预检面板
  - 优先级：中。
  - 原因：mapper 经常在流程后期才发现 clipnodes、entity、texture、lightdata 等限制问题。
  - 第一阶段：编译前做近似警告，编译后结合日志数据修正判断。

- [ ] WAD 工作流增强
  - 优先级：中。
  - 原因：GoldSrc 纹理依赖 WAD，且受顺序、调色板、命名规则和重复纹理影响。当前流程仍然需要用户手动管理很多细节。
  - 第一阶段：显示 WAD 来源和顺序、重复纹理来源、缺失 WAD 警告，以及按 WAD 分组的 used textures 报告。

- [ ] GoldSrc 纹理规则助手
  - 优先级：中。
  - 原因：GoldSrc 纹理名和前缀会影响透明、水、动画、切换动画和滚动等引擎行为。
  - 第一阶段：提示超长名称、空格、非法尺寸，并辅助 `{`、`!`、`+`、`+A`、`scroll` 等命名约定。

- [ ] GoldSrc Skybox 助手
  - 优先级：中。
  - 原因：当前分支已经支持 `skyname`，继续补齐六面 sky 资源检查和预览很自然。
  - 第一阶段：从 `gfx/env` 选择 `skyname`，诊断缺失面，并提供方向预览。

- [ ] FGD 兼容助手
  - 优先级：中。
  - 原因：社区 FGD 经常包含 J.A.C.K./Hammer 扩展，TrenchBroom 解析失败时用户需要手动排查和合并 FGD。
  - 第一阶段：更友好的解析诊断、include 链显示，以及可选生成 Half-Life + ZHLT/VHLT combined FGD。

## 调研备注

- TWHL 的 GoldSrc TrenchBroom 设置教程指出：TrenchBroom 可以加载 Half-Life 纹理、Sprite 和模型，但缺少方便的 Sprite/Model 文件浏览器，因此路径常常需要手动输入。
- upstream 长期 GoldSrc issue 表明，完整 GoldSrc 支持不只是 MDL 加载，还包括 Sprite、WAD、skin、bodygroup 和相关资产行为。
- GoldSrc 纹理教程强调 WAD 打包、8-bit indexed 纹理约束、15 字符纹理名限制和特殊纹理前缀。
- GoldSrc 编译教程和限制图表显示，编译日志解析是非常高价值的工具方向。

