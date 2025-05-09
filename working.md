# 项目开发记录

## 基本原则
- 请在工作开始前确保正确加载应用全部规则
- 本记录仅存放本次任务的重要信息 

## 其他完整文档
- 开发文档.md

## 需求分析
- GitHub Issue #3403: 在地图检查器侧边栏中添加对象/实体列表功能
- 用户需求背景：当地图中有超过十几个实体时，飞行寻找特定实体进行选择/编辑/跟踪变得非常麻烦
- 主要痛点：在TrenchBroom中编写大型复杂地图脚本时，缺乏实体管理工具

## 工作计划
1. ⏱️ 调研现有UI框架，确定实现方式
2. ⏱️ 设计统一的层级/实体列表UI组件和交互方式
3. ⏱️ 扩展现有LayerListBox功能，增加实体列表显示
4. ⏱️ 添加分层、分组和排序功能
5. ⏱️ 实现与3D视图的选择同步
6. ⏱️ 添加搜索、过滤功能
7. ⏱️ 测试大型地图的性能和可用性

## 规范事项
- 列表应支持分层结构，显示分组和图层
- 默认排序规则：世界刷子置顶，然后按组名>实体类名>实体目标名排序
- 支持下划线前缀命名惯例（用户强制组置顶的技巧）
- 未命名实体应排序到其部分的底部
- 需要四种不同的图标：世界刷子（实心立方体 Map_fullcube.svg）、刷子实体（线框立方体 Map_cube.svg）、点实体（线框基准 Map_entity.svg）、分组（线框文件夹 Map_folder.svg）

## 代码分析
- 从图片可以看出，已有类似的层级结构显示功能，显示了"Default Layer"层
- 每个层级显示了包含的对象数量，以及提供了可见性和锁定控制
- `MapInspector.h`中定义了MapInspector类，它是一个TabBookPage，包含了地图检查器的所有功能
- 在MapInspector的createGui方法中添加各种面板，当前已有：
  1. 图层编辑器（LayerListBox）：管理地图中的图层
  2. 地图属性编辑器（MapPropertiesEditor）：管理地图属性如软边界
  3. Mod编辑器（ModEditor）：管理游戏Mod
- 方案调整：不创建新的EntityListEditor组件，而是扩展现有的LayerListBox功能，将其转变为一个统一的大纲视图（类似Unreal Engine大纲面板）
- 这样可以避免功能重复的两个面板，提供更一致的用户体验

## 下一步任务
1. ⏱️ 扩展现有LayerListBox.cpp类：
   - 重构现有LayerEntityTreeView以支持显示整个场景层次结构（图层、实体、刷子等）
   - 添加搜索筛选和排序功能
   - 保留现有的图层管理功能
2. ⏱️ 修改图层/实体树的数据模型：
   - 修改现有树形视图以显示不仅是图层，还包括实体和其他对象
   - 实现分层结构，显示图层内的实体和组
   - 添加实体的图标和属性显示
3. ⏱️ 更新UI交互功能：
   - 添加实体右键菜单
   - 实现实体的选择、定位功能
   - 保留图层的可见性和锁定控制，扩展到实体
4. ⏱️ 更新MapInspector中的相关代码以适应改动：
   - 更新createLayerListBox方法

## 进度跟踪
⚙️ 需求分析阶段
⚙️ 代码分析阶段
⚙️ 方案调整阶段


## 重要信息
- 加载icon的方法:ResourceUtils.h 
参考PreferenceDialog.cpp:
  const auto gamesImage = io::loadSVGIcon("GeneralPreferences.svg");
  const auto viewImage = io::loadSVGIcon("ViewPreferences.svg");
  const auto colorsImage = io::loadSVGIcon("ColorPreferences.svg");
  const auto mouseImage = io::loadSVGIcon("MousePreferences.svg");
  const auto keyboardImage = io::loadSVGIcon("KeyboardPreferences.svg");
  const auto languageImage = io::loadSVGIcon("LanguagePreferences.svg");
  const auto updateImage = io::loadSVGIcon("UpdatePreferences.svg");

- 功能对比：Hammer编辑器中类似功能为Entity Report模态窗口和自动Visgroups侧边栏
- 现代标准：Max/Maya/Unity/Unreal/Godot/Blender都采用统一的场景层次结构视图
- 关键功能点：
  - 支持多选（并理想情况下，支持每个对象的可见性控制/锁定）
  - 单击列表中的对象 = 选择它
  - 双击列表中的对象 = 选择它 + 传送并在当前相机视图中显示
  - 确保列表和3D视图之间的选择同步
- 用户受益：
  - 减少"通勤"和"寻找"遗忘的东西
  - 允许用户按类型选择多个对象
  - 允许用户一目了然地计数/跟踪多个对象
  - 有助于一般错误修复/调整/实体脚本编写
  - 使地图检查器真正发挥其功能
  - 将TB提升为现代游戏编辑器工具标准
- 实现策略调整：
  - 不再创建单独的EntityListEditor组件
  - 而是扩展LayerListBox成为统一的场景大纲视图
  - 这种方式更符合现代游戏引擎的设计理念（如Unreal Engine的大纲视图）
  - 避免功能重复，提供更一致的用户体验

## 错误处理方案
- 可能问题：对于大型地图，列表也会变得很笨重，需要考虑搜索筛选功能
- 需要更好的方法来表示世界刷子
- 潜在问题：扩展现有LayerListBox可能需要大量修改，需要确保不破坏现有功能

## Next Tasks
- ⏱️ 测试LayerListBox的搜索、筛选和排序功能。
- ⏱️ 实现其他未完成的功能，如移动摄像机。
- ⏱️ 继续优化代码和用户界面。

# TrenchBroom 撤销系统和拖拽操作分析

## 基本原则
- TrenchBroom使用命令模式(Command Pattern)实现撤销/重做功能
- MapDocument作为核心类管理文档状态和事务
- Transaction作为事务包装器控制命令的提交和回滚

## 撤销系统关键组件

1. **Transaction类** (Transaction.h/cpp)
   - 提供RAII风格的事务管理
   - 自动在构造函数中调用document.startTransaction
   - 在析构函数中自动取消未完成的事务
   - 提供commit()和cancel()方法显式控制事务

2. **MapDocument类** (MapDocument.h/cpp)
   - 实现reparentNodes方法，内部创建Transaction
   - 管理命令处理和撤销栈
   - 提供startTransaction/commitTransaction/rollbackTransaction方法

3. **TransactionScope枚举** (TransactionScope.h)
   - Oneshot: 用户只能看到初始和最终状态
   - LongRunning: 用户可以看到中间状态

## reparentNodes实现分析
MapDocument::reparentNodes方法已经在内部创建了自己的Transaction：
```cpp
// MapDocument.cpp
bool MapDocument::reparentNodes(...) {
  // ...
  auto transaction = Transaction{*this, "Reparent Objects"};
  // ...执行操作...
  return transaction.commit();
}
```

## 拖拽问题解决
1. **问题根源**：LayerListBox.cpp中的dropEvent重复创建了Transaction，导致事务嵌套出现问题
2. **正确做法**：UI层不应创建Transaction，应完全依赖MapDocument的reparentNodes内部事务管理

## 修复方案
1. 移除dropEvent中的Transaction创建
2. 让所有拖拽操作处理方式保持一致
3. 统一使用MapDocument内部的事务管理机制

## 结论
TrenchBroom的事务系统设计良好，但需要小心避免在UI层创建重复的Transaction。当调用已经包含Transaction的document方法时，不应再包装额外的Transaction。

