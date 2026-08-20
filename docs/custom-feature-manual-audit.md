# 自定义功能、使用手册与截图能力盘点

## 目的与范围

本文档用于盘点当前 TrenchBroom fork 相对官方版本新增的用户功能，记录离线手册的
覆盖情况，并判断现有 UI 截图基础设施是否足以自动生成对应功能界面截图。

本次盘点日期为 2026-08-20，检查基线如下：

- 当前分支：`main`
- 当前提交：`93c8a9615`（`Update Python v2 usage documentation`）
- 与官方 `upstream/master` 的 merge base：
  `ce0e9d4bb58ef1e08e8c533159f320592d4daffc`
- 手册源文件：`app/TrenchBroom/resources/documentation/manual/{en,zh_CN}`
- 手册共用图片目录：`app/TrenchBroom/resources/documentation/manual/images`

从 merge base 到当前 `main` 约有 800 个提交、661 个文件发生变化。已回滚的实验功能
不作为当前功能列出；尚未合并的本地分支单独列出，不能提前写成当前版本已经支持的
功能。

### 状态说明

- **较完整**：已有可操作的文字说明和有效图片。
- **简略**：已经提到该功能，但缺少完整步骤、状态说明或对应截图。
- **缺失**：手册中没有形成有效说明。
- **可直接截图**：现有确定性 snapshot target 可以生成可用界面图。
- **需要场景**：截图框架已经存在，但要补测试地图、资源或确定的 UI 状态。
- **需要 target**：需要给 `UiSnapshotRunner` 新增专用截图入口。

## 当前 `main` 功能清单

### 1. Outliner 与层级管理

| 功能 | 手册状态 | 截图能力 |
| --- | --- | --- |
| 统一显示 Layer、Group、点实体、Brush Entity、worldspawn 和普通 Brush | 简略 | 可直接截图，但现有地图内容太少 |
| Outliner 与视口双向同步选择 | 简略 | 需要场景 |
| 高亮当前 Layer、当前 Group 和 Linked Group | 简略 | 需要场景 |
| 按文字、对象类型和选中状态过滤 | 简略 | 需要 target |
| 默认、类型和文件顺序排序 | 简略 | 需要 target |
| 直接切换可见性、锁定和导出排除状态 | 简略 | 需要场景 |
| Layer、Group、实体、Brush、聚焦、隔离、重命名和删除右键菜单 | 简略 | 需要打开菜单的 target |
| 在 Outliner 中创建 Layer | 简略 | 需要 target |
| Delete/Backspace 删除、Esc 清除选择和键盘行导航 | 简略 | 需要交互状态或步骤图 |
| 将对象拖到另一个 Layer 或 Group | 缺失 | 需要拖放前后状态 |
| 将普通 Brush 拖入 Brush Entity | 缺失 | 需要拖放前后状态 |
| 将 Brush Entity 中的 Brush 拖回目标 Layer 的 worldspawn | 缺失 | 需要拖放前后状态 |
| 内嵌编辑 worldspawn 和实体属性 | 缺失 | 基础面板可直接截图，完整内容需要场景 |
| 批量属性、choice、spawnflags、WAD、skyname、新增/删除属性和复制属性键 | 缺失 | 需要专用实体属性场景 |

这里常说的“Brush 拖动合并”实际是把 Brush 重归属到 Brush Entity，并不是 CSG
几何合并。“移动图层”目前是把对象移动到目标 Layer，也不是拖动 Layer 行进行排序。

主要实现：

- `lib/TbUiLib/src/outliner/OutlinerTreeWidget.cpp`
- `lib/TbUiLib/src/outliner/OutlinerEntityPropertyEditor.cpp`

### 2. 统一资产浏览器与 Prefab

| 功能 | 手册状态 | 截图能力 |
| --- | --- | --- |
| 在同一浏览器中显示 MDL、SPR、WAV 和 `.tbprefab` | 简略 | 现有 target 只能直接截空目录概览 |
| 文件夹树、面包屑、路径输入、搜索、刷新、目录监视和懒加载 | 简略 | 需要填充后的 GoldSrc 资源场景 |
| MDL 实时三维预览和 GoldSrc MDL 缩放 | 简略 | 需要场景 |
| GoldSrc SPR 首帧、透明模式和错误占位符预览 | 简略 | 需要 Sprite fixture |
| WAV 播放和停止试听 | 简略 | 需要 WAV fixture 和选中状态 |
| 将 MDL、SPR、WAV 拖入视口并创建相应实体 | 简略 | 需要确定性拖放 target |
| 缩放 MDL 和批量重载实体模型 | 缺失 | 需要打开模型右键菜单的 target |
| Smart Model Editor 从实体模型属性中选择资源路径 | 缺失 | 需要 target |
| 将选区保存为 Prefab | 简略 | 需要场景 |
| Prefab 缩略图、预览、拖放、重命名和删除 | 简略 | 需要 Prefab fixture 和右键菜单 target |
| 配置 Prefab 存储目录 | 简略 | Misc Preferences 可直接截图 |
| 放置 Prefab 时导入 WAD 和材质来源 | 简略 | 需要放置前后教程，单张截图无法说明完整行为 |

现有 `supporting` target 会打开 Assets 页面，但标准工作台 fixture 没有真实的 GoldSrc
资源，所以目前只适合展示布局，不足以制作模型、Sprite、声音和 Prefab 教程。

主要实现：

- `lib/TbUiLib/src/ModelBrowser.cpp`
- `lib/TbUiLib/src/ModelBrowserView.cpp`
- `lib/TbUiLib/src/AssetBrowserModel.cpp`
- `lib/TbUiLib/src/AssetPreviewProvider.cpp`
- `lib/TbUiLib/src/PrefabAsset.cpp`
- `lib/TbUiLib/src/PrefabTool.cpp`

### 3. 编辑与建模工具

| 功能 | 手册状态 | 截图能力 |
| --- | --- | --- |
| 原生 Chamfer Tool：边/顶点模式、距离、1-64 分段、实时预览和 Apply | 简略 | 需要选中边/顶点的 fixture 和 target |
| Smart Face Selection：Face Strip 和 Parallel Faces | 缺失 | 需要 target |
| Smart Face 的 Replace、Add 和 Remove 操作 | 缺失 | 需要 target |
| Smart Face 的角度/间隙容差、种子方向、分支停止和同材质过滤 | 缺失 | 需要能显示候选预览的场景 |
| Sweep Tool：Straight、Arc 和 S-bend 路径 | 较完整 | 现有 GIF 可用，可再补当前控制面板截图 |
| Sweep 扭曲、锥化、迭代、吸附和连续 UV | 较完整 | 需要当前控制状态图 |
| Sweep Bridge 连接两个独立面组件 | 简略 | 需要前、预览、后三个状态 |
| Path Tool 网格吸附的路径点放置和预览 | 简略 | 需要 target |
| Path Tool 预览点撤销/恢复及自动 `target`/`targetname` 链 | 简略 | 需要前后状态 |
| Command Palette 动作搜索、菜单路径、可用状态和快捷键 | 较完整 | 可直接截图 |
| Pie Menu 长按调用、上下文动作和拖动配置 | 简略 | Pie Menu 与设置对话框都需要 target |
| 可选的 Ctrl+拖动 2D 框选 | 简略 | 偏好设置可直接截，交互状态需要 target |
| Entity Template 记录并批量应用到多个独立 Brush Entity | 简略 | 需要前后状态 |
| Material Browser 可折叠分组、100%-500% 缩放、Ctrl+滚轮和缩放比例 | 较完整 | 可复用 Face Inspector target |
| 按指定材质选择全部面或 Brush | 文字较完整 | 如需教程细节，应补右键菜单截图 |

当前手册对 Path Tool 存在一处超前描述：`03-brush-editing.md` 声称可以插入现有
节点、反转方向和重新连接路径。当前 `PathTool` 实现只支持创建新的 `path_corner` 链，
以及撤销/恢复尚未提交的预览点。补图之前应先纠正文案。

### 4. GoldSrc 与视图增强

| 功能 | 手册状态 | 截图能力 |
| --- | --- | --- |
| 根据 `skyname` 在 3D 视口渲染六面天空盒 | 简略 | 需要 GoldSrc 游戏和天空资源 fixture |
| 天空盒方向修正、资源缺失回退和选中 sky 面高亮 | 简略 | 需要成功、回退和选中三种状态 |
| Outliner 内嵌 `skyname` 选择器 | 缺失 | 需要 worldspawn 属性场景 |
| world Brush 和 Brush Entity 的 2D 高可读轮廓 | 简略 | 需要高密度 2D 地图 fixture |
| 2D 轮廓中选择和锁定状态的优先级 | 简略 | 需要多个状态变体 |
| FPS Overlay | 简略 | 需要开启选项的视口 target |
| GoldSrc MDL 内嵌纹理、透明材质、方向和缩放支持 | 缺失 | 需要模型 fixture |
| Counter-Strike FGD 中的 SDHLT 模型阴影、BSP、双面、厚度和碰撞选项 | 缺失 | 需要实体属性 target |

### 5. Python v2 与插件系统

| 功能 | 手册状态 | 截图能力 |
| --- | --- | --- |
| 嵌入式 `tb2` runtime 和 manifest 插件 | 较完整 | 主要使用代码示例，不依赖截图 |
| 插件自动发现、session 和资源生命周期 | 较完整 | 如补 UI 教程，需要插件管理器状态 |
| 独立 Python Plugin Manager 和 Plugin Inspector | 简略 | Inspector 可直接截，Manager 需要 target |
| 双栏 Python Console、多行编辑器和输出日志 | 较完整 | 可直接截图 |
| Enter 执行、Shift+Enter 换行、历史、字体和自动事务 | 较完整 | Console 可直接截，字体设置可单独截 |
| 链式和索引表达式 IntelliSense | 文字较完整 | 需要补全弹窗 target |
| Document、Selection、Entity、Brush、Face、Material、变换、几何、倒角和 UV API | 较完整 | 代码示例比 UI 截图更有效 |
| Form、Table、Tree、HTML、事件回调和 session timer | 较完整 | 如需截图，应构造示例 Panel |
| Brush Builder、Chamfer、Curve Sweep、Distribute、Texture、Git 和 Blender 示例插件 | 部分覆盖 | 应选择典型工作流；这些不是内置核心面板 |
| legacy `tb` 已移除，活动插件使用 `tb2` | 较完整 | 无需截图 |

### 6. MCP 与 Agent 自动化

| 功能 | 手册状态 | 截图能力 |
| --- | --- | --- |
| 本机 HTTP `/mcp` 和可选 stdio shim | 简略 | Misc Preferences 可直接截图 |
| Off、Read-only 和 Edit 权限 | 简略 | 可直接截图 |
| Core、Modeling 和 Full 工具配置 | 简略 | 可直接截图 |
| 文档、选择、对象、实体、Brush、材质、资产、CSG、问题、编译和 leak 工具 | 缺工作流教程 | 更适合请求/响应示例 |
| 事务、对象身份、文档保护、时间线、撤销和恢复 | 缺失 | 更适合流程图和 JSON 示例 |
| 2D/3D 截图、隔离审查和几何 Review | 缺失 | 可以直接嵌入工具生成的 Review 图 |
| Blockout IR、批量几何、heightmap、楼梯、路线、selector、module 和 operation | 缺失 | 适合展示 IR、应用、验证和 Review 序列 |
| MCP workflow skill 和 recipes | 缺失 | 适合完整命令工作流示例 |

### 7. UI、主题与偏好设置

| 功能 | 手册状态 | 截图能力 |
| --- | --- | --- |
| System、Light、Dark 和 Blender 主题 | 较完整 | 可直接截图并支持完整主题矩阵 |
| 可分发 `.tbtheme` 和主题继承 | 较完整 | Preferences 可直接截图 |
| 现代 Qt 工作台和垂直 Inspector 导航栏 | 视觉覆盖较完整 | 可直接截图 |
| 英文和简体中文界面切换 | 简略 | 当前 Misc target 滚动后看不到语言控件 |
| Python Console 字体和 Material Browser 图标大小 | 简略 | 可复用偏好设置页面，需补焦点状态 |
| Prefab、Pie Menu、Python Plugin Manager 和 MCP 集中到 Misc | 简略 | 可直接截图，但单图无法同时展示顶部和底部 |
| 复制地图文本时添加 worldspawn header | 简略 | 需要 Misc 顶部 target 或剪贴板教程 |
| 13 章中英双语手册和 Python API 页面 | 较完整 | 不适用 |

## 开发与验证能力

以下也是 fork 新增的重要能力，但通常不应占用终端用户使用手册章节：

- 确定性 Qt UI snapshot runner 和主题/缩放验收矩阵。
- UI 组件样式治理和 snapshot baseline 比较。
- Release 过滤构建脚本和陈旧对象文件移除工具。
- 本地 CI preflight 和受影响测试判断。
- 性能 benchmark runner。
- MCP smoke、Review、recipe 校验和 skill 同步脚本。

这些内容应保留在开发者文档中，除非以后新增公开贡献指南。

## 尚未合并到 `main` 的功能分支

这些分支有当前 `main` 不包含的提交。部分分支已经长期分叉，合并前还需要重新确认
完成度和行为。

| 分支 | 候选功能 | 当前手册处理 |
| --- | --- | --- |
| `feat/2d-background-image` | 2D 参考图和蓝图 Overlay | 暂不写成已发布功能 |
| `Curve_generator` | Curve Generator、预设和外部工具工作流 | 暂不写成已发布功能 |
| `advanced_uv` | 多面 UV 选择及 G/S/R 位移、缩放和旋转 | 暂不写成已发布功能 |
| `better_extrude` | 增强 Extrude 和 Group 支持 | 暂不写成已发布功能 |
| `Map_Inspector`、`layerlist` | 另一套地图检查器和图层列表 | 暂不写成已发布功能 |
| `git_control`、`Plugin_dev` | 更完整的内置 Git 状态、暂存和分支界面 | 暂不写成已发布功能 |
| `multi-instance`、`FlyMove` | Shift+右键多选面及改变后的 3D 相机交互 | 暂不写成已发布功能 |
| `Dragmove2D` | 2D 绘制接近视口边缘时自动平移 | 暂不写成已发布功能 |
| `obj_importer` | OBJ 导入实验 | 先确认完成度 |
| `face_move`、`merge_verticestools` | 面和顶点编辑实验 | 先确认目标行为 |
| `pr-3932` | Texture Justify 工具实验 | 先确认完成度 |

旧的 `entity_presets` 和 `Textures_browser` 分支不作为缺失功能列出，因为它们的主要
行为后来已经移植或重新实现在 `main`。

## 现有截图基础设施评估

### 已有能力

TrenchBroom 已经提供无需显示原生窗口的确定性自截图入口：

```text
TrenchBroom.exe --ui-snapshot OUTPUT.png \
  --ui-snapshot-theme dark \
  --ui-snapshot-page TARGET \
  [--ui-snapshot-game-path GAME_DIRECTORY] \
  [MAP_FILE]
```

当前 runner 可以：

- 隔离截图设置，不污染用户日常 Preferences。
- 指定 Light、Dark、Blender 或已注册主题。
- 指定 Qt 缩放倍率。
- 生成 PNG 和包含尺寸、颜色检查、SHA-256 的 JSON manifest。
- 在材质场景中等待 GPU 资源就绪。
- 不显示原生窗口完成截图。
- 失败时生成同名 `.error.txt`。
- 通过 `scripts/ui-theme-acceptance.ps1` 生成 contact sheet 并与 baseline 比较。

现有 target：

```text
welcome
workbench
outliner
entity-browser
entity-browser-empty
face-inspector
material-browser-empty
plugin-inspector
supporting
python-console
command-palette
components
preferences
preferences-colors
preferences-mouse
preferences-keyboard
preferences-misc
```

### 实际验证结果

2026-08-20 使用当前 Release 可执行文件运行了以下命令：

```powershell
& scripts/ui-theme-acceptance.ps1 `
  -Targets @(
    'outliner',
    'supporting',
    'python-console',
    'command-palette',
    'preferences-misc') `
  -Themes @('dark') `
  -ScaleFactors @('1') `
  -SkipBuild
```

五个 target 均通过图片完整性和 manifest 校验：

| Target | 输出尺寸 | 结果 |
| --- | --- | --- |
| `outliner` | 1440x900 | 层级树和属性面板清晰，但场景只有一个 worldspawn Brush |
| `supporting` | 1440x900 | Assets 布局清晰，但只有空资源根目录 |
| `python-console` | 1440x900 | 可以直接用于手册 |
| `command-palette` | 640x480 | 可以直接用于手册 |
| `preferences-misc` | 920x560 | Prefab 和 MCP 设置清晰，但顶部控件被滚动隐藏 |

验证生成物位于
`build-release-codex/codex-logs/ui-theme-acceptance/20260820-141345-548`，属于构建日志，
不会作为仓库资源提交。

### 是否足够让我自行截图并插入手册

**结论：现有系统已经足以让我自动截取已有概览 target，并将选定图片放入手册；但还
不足以独立完成所有新增功能的教程截图。**

当前限制不是图片保存能力，而是缺少确定性的功能场景。很多功能需要特定地图、游戏
资源、选区、激活工具、弹出菜单、拖放预览或操作前后状态，现有 target 没有构造这些
条件。

现在可以直接覆盖：

- Command Palette。
- Python Console。
- Outliner 总体布局和基础内嵌属性面板。
- Assets 总体布局，但不包含真实预览。
- 主题和标准 Preferences 页面。
- Misc 中的 Prefab 目录和 MCP 设置。
- Entity Browser、Face Inspector、Material Browser 和 Plugin Inspector 概览。

要覆盖其余功能，需要补充：

1. 一张包含多个 Layer、Group、点实体、Brush Entity、worldspawn Brush、Linked Group、
   隐藏和锁定状态的仓库内层级 fixture。
2. Outliner 过滤、排序、右键菜单、属性类型和拖放前后状态 target。
3. 包含小型合法 MDL、SPR、WAV、skybox、WAD 和 Prefab 的 GoldSrc fixture。资源必须可
   追踪许可证，或由仓库脚本自行生成。
4. 等待所有可见资源预览就绪的 Asset Browser readiness 检查，不能只判断非空图片。
5. 为 Chamfer、Smart Face、Sweep、Sweep Bridge 和 Path Tool 准备带预选面、边和顶点
   的建模 fixture。
6. 为 Pie Menu、右键菜单、IntelliSense 和 Plugin Manager 增加可捕获弹窗的 target。
7. 把 Misc Preferences 拆成顶部和 MCP 两个状态，避免控件因滚动被隐藏。
8. 为 2D 可读轮廓、FPS、天空盒成功/回退以及资源或 Prefab 拖放预览增加视口 target。

### 建议新增的截图 target

```text
outliner-hierarchy
outliner-filter
outliner-layer-menu
outliner-group-menu
outliner-properties-entity
outliner-properties-worldspawn
outliner-reparent-layer-before
outliner-reparent-layer-after
outliner-brush-entity-before
outliner-brush-entity-after
asset-browser-models
asset-browser-sprites
asset-browser-sounds
asset-browser-prefabs
asset-browser-prefab-menu
chamfer-edge-preview
chamfer-vertex-preview
smart-face-strip
smart-face-parallel
sweep-controls
sweep-bridge-preview
path-tool-preview
pie-menu
pie-menu-settings
python-completion
python-plugin-manager
preferences-misc-top
preferences-misc-mcp
view-readable-outlines
view-fps
view-goldsrc-skybox
```

Outliner 拖放和 Entity Template 更适合使用操作前后两张图。指针移动过程如果确实有
说明价值，可以使用短 GIF，但起始和结束帧仍应由确定性场景生成。

## 截图插入双语手册的流程

当对应 target 完成后，可以不手工操作窗口，按以下流程补图：

1. 构建当前 Release `TrenchBroom` target。
2. 使用 Dark 主题、100% 缩放捕获主手册图片。
3. 检查图片中的内容、选区、资源、文字、重叠和用户隐私数据。
4. 将批准的 PNG 以稳定、描述性的名称放入
   `app/TrenchBroom/resources/documentation/manual/images`。
5. 在英文和中文手册中引用同一图片，并分别填写本地化 alt text。
6. 迭代期间只构建 `GenerateManual`。
7. 运行 `validate_manual.py` 检查双语结构和生成结果。
8. 在桌面宽度和窄窗口下目视检查生成手册。
9. 最后只运行一次 `ci-preflight.ps1` 作为提交前门禁。

常用命令：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build-filtered.ps1 `
  -Target GenerateManual

python app/TrenchBroom/resources/documentation/manual/validate_manual.py `
  --manual-root app/TrenchBroom/resources/documentation/manual `
  --generated-root build-release-codex/app/TrenchBroom/gen-manual
```

英文和中文页面当前共用同一图片目录。如果以后要求界面语言也分别为英文和中文，
需要为 snapshot 命令增加确定性的语言覆盖参数，并制定带语言后缀的图片命名规则。

## 建议补充顺序

1. Outliner 层级和拖放重归属教程。
2. 填充真实资源的 Assets 与 Prefab 工作流。
3. Smart Face Selection。
4. Chamfer Tool 和 Sweep Bridge。
5. 纠正 Path Tool 文案并补当前真实工作流。
6. GoldSrc 天空盒和 2D 可读轮廓。
7. Entity Template 操作前后流程。
8. Pie Menu 和 Python Plugin Manager。
9. MCP 端到端工作流示例。

这个顺序优先补齐普通制图工作流中的最大缺口，再扩展插件和自动化教程。
