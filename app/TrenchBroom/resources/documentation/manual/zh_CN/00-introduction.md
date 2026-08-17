% TrenchBroom  \_\_TB_VERSION\_\_ 参考手册
% Kristian Duske
% 11-13-2015

# 简介 {#introduction}

TrenchBroom 是一款面向 Quake、Quake 2、Hexen 2 和 GoldSrc (Half-Life) 等基于 Brush 的游戏引擎的现代化关卡编辑器。它易于使用，并提供了创建复杂、有趣关卡所需的基础和高级工具。

::: manual-hero-banner
**🚀 欢迎查阅 TrenchBroom 官方参考手册**

探索完整的使用指南、几何建模工具、材质与资产浏览器、Python 脚本扩展以及智能体自动化。

::: manual-quick-tags
[⚡ 入门基础](#getting_started){.manual-tag}
[📐 Brush 建模](#brush_editing_and_creation){.manual-tag}
[✂️ CSG 与顶点编辑](#vertex_and_csg){.manual-tag}
[🎨 材质与 UV](#materials_and_uv){.manual-tag}
[📦 资产浏览器](#assets_and_prefabs){.manual-tag}
[🐍 Python v2 插件](#python_scripting_and_plugins){.manual-tag}
[🤖 MCP 自动化](#mcp_automation){.manual-tag}
:::
:::

::: manual-card-grid
::: manual-card
**🚀 1. 入门与基础**

基础概念、地图层级结构 (World、图层、Brush、实体) 以及 3D/2D 视口相机导航。

[➔ 共 3 章](#getting_started)
:::

::: manual-card
**🧱 2. 几何建模与 CSG**

Brush 图元、拉伸、冲压、裁剪、扫掠、顶点高级编辑与 CSG 布尔几何运算。

[➔ 共 2 章](#brush_editing_and_creation)
:::

::: manual-card
**🎨 3. 材质、资产与组织**

材质浏览器、UV 编辑器、统一资产浏览器 (MDL、SPR、WAV)、预制体与 Outliner 组织。

[➔ 共 3 章](#materials_and_uv)
:::

::: manual-card
**⚙️ 4. 编译、扩展与自动化**

首选项设置、外部编译器配置、游戏引擎运行、Python v2 插件开发与 MCP 自动化接口。

[➔ 共 4 章](#preferences_and_compilation)
:::
:::

## 功能 {#features}

* **通用功能**
  - 完整支持 3D 编辑和最多三个 2D 视图
  - 支持超大地图的高性能渲染器
  - 无限撤销和重做
  - 可搜索的命令面板和可配置的饼状菜单
  - System、Light、Dark 和 Blender 主题，以及可分发的用户主题
  - 类似宏的命令重复
  - 链接组
  - 支持创建图层和内联实体属性的可搜索 Outliner（实体浏览器）
  - 带自动快速修复的问题浏览器
  - 运行外部编译器并启动游戏引擎
  - 交互式 Python 控制台和基于清单的 Python v2 插件
  - 可选的本地主机 MCP 端点，用于受保护的编辑器自动化
  - 点文件和 Portal 文件支持
  - 自动备份
  - 免费且跨平台
* **Brush 编辑**
  - 强大的顶点编辑，支持边和面分割，以及同时操作多个顶点
  - 支持两点和三点的裁剪工具
  - 缩放和剪切工具
  - CSG 操作：合并、相减、挖空、相交
  - 便于调整贴图的 UV 视图
  - 适用于所有 Brush 编辑操作的精确对齐锁定
  - 多个材质集合
  - 支持搜索的材质浏览器，提供可折叠的集合组和 100%-500% 缩略图
* **Entity 编辑**
  - 支持拖放的实体浏览器
  - 支持 GoldSrc 模型、精灵、声音和可复用预制体的统一资产浏览器
  - 支持用于实体定义的 FGD、ENT 和 DEF 文件
  - Mod 支持
  - 实体链接可视化
  - 在编辑器中显示 3D 模型（支持 mdl、md2、md3、bsp、dkm）
  - 智能实体属性编辑器
  - 用于创建链接 `path_corner` 链的路径工具

## 关于本手册 {#about-this-document}

本手册旨在帮助你学习使用 TrenchBroom 编辑器。它不负责教你如何制作地图，也不是教程。如果地图遇到技术问题，或需要了解如何为目标游戏创建特定效果或设置，请向其他制图者寻求帮助（可参阅[参考与链接](#references_and_links)查找制图社区）。本手册只介绍编辑器本身的使用方法。
