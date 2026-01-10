# Outliner 实现计划

本文档概述了在 TrenchBroom 的 Inspector 中集成 **Outliner**（场景层级面板）的架构设计和实现步骤。

## 1. 概述

Outliner 提供了地图内容的层级视图，类似于 Unreal Engine 中的 "World Outliner" 或 Unity 中的 "Hierarchy"。它允许用户：
- 可视化实体（Entities）和组（Groups）的父子关系。
- 直接从列表中选择对象。
- 通过拖放组织场景。
- 快速搜索和过滤对象。

## 2. 集成点

Outliner 将作为新选项卡集成到现有的 `Inspector` 面板中，与 "Map"、"Entity" 和 "Face" 并列。

**文件**: `common/src/ui/Inspector.cpp`

```cpp
// 在 Inspector::Inspector(...) 中
m_outlinerInspector = new OutlinerInspector{map, contextManager};
m_tabBook->addPage(m_outlinerInspector, "Outliner");
```

## 3. 类设计

### 3.1. 数据模型 (`OutlinerModel`)

该类将 TrenchBroom 的数据结构（`mdl::Map`、`mdl::WorldNode`、`mdl::Node`）适配到 Qt 的 `QAbstractItemModel` 接口。

*   **继承**: `QAbstractItemModel`
*   **职责**:
    *   **数据源**: 包装 `mdl::WorldNode` 作为根节点。
    *   **列结构**:
        *   第 0 列：名称（实体类名、组名或 ID）。
        *   第 1 列：可见性（眼睛图标）。
        *   第 2 列：锁定状态（锁图标）。
        *   第 3 列：类型/类（例如 "func_wall"、"Group"）。
    *   **更新**: 连接到 `mdl::Map` 通知器（`nodesWereAddedNotifier`、`nodesWereRemovedNotifier` 等）以触发 `beginInsertRows`/`endInsertRows`。
*   **性能优化**:
    *   **过滤**: 默认情况下，**不要**显示单个世界画刷（world brushes/geometry），以避免界面混乱和性能下降。仅显示实体和组。如果需要，可以将世界画刷分组在虚拟的 "Geometry" 节点下，或者完全隐藏。

### 3.2. UI 视图 (`OutlinerInspector`)

位于 Inspector 选项卡内的容器组件。

*   **继承**: `tb::ui::TabBookPage`
*   **组件**:
    *   `QLineEdit` (搜索过滤器): 用于按名称/类名过滤树状视图。
    *   `QTreeView`: 主要的树状显示。
    *   `OutlinerModel`: 数据模型实例。
    *   `QSortFilterProxyModel`: (可选) 用于处理文本过滤和排序，而不修改源模型。
*   **职责**:
    *   **选择同步**:
        *   监听 `QTreeView::selectionModel()->selectionChanged`: 调用 `map.setSelection(...)`。
        *   监听 `map.selectionDidChangeNotifier`: 更新 `QTreeView` 的选择（阻止信号以防止死循环）。
    *   **拖放**: 实现 `dragEnterEvent`、`dropEvent`，允许通过 `mdl::Map` 命令重新设置节点的父级。

## 4. 关键特性与实现细节

### A. 选择同步（双向）
*   **地图 -> UI**: 当 3D 视图中的选择发生变化时，Outliner 必须突出显示相应的行。
    *   *挑战*: 展开树以显示选定的节点。使用 `QTreeView::scrollTo`。
*   **UI -> 地图**: 单击行时，必须更新 3D 视图的选择。
    *   *操作*: 构造一个 `mdl::Selection` 对象并应用它。

### B. 实时更新
*   使用 `mdl::Map` 通知器。
    *   `nodesWereAdded`: 映射到 `beginInsertRows`。
    *   `nodesWereRemoved`: 映射到 `beginRemoveRows`。
    *   `nodeDidChange`: 映射到 `dataChanged`（用于名称更新）。

### C. 拖放（重新设父）
*   允许用户将实体/组拖入另一个组。
*   **后端操作**: 执行调用 `node->setParent(newParent)` 的命令。

## 5. 实现路线图

### 阶段 1: 骨架与集成
1.  创建 `OutlinerInspector` 类（空壳）。
2.  修改 `Inspector` 以添加 "Outliner" 选项卡。
3.  验证选项卡是否出现在 UI 中。

**编译修复记录 (2026-01-09)**:
*   **问题 1**: `C2027: use of undefined type 'tb::ui::OutlinerInspector'` 和链接错误。
    *   原因: 新的源文件 `OutlinerInspector.cpp` 和 `OutlinerModel.cpp` 未添加到 CMake 构建目标。
    *   修复: 更新 `common/CMakeLists.txt`，添加新的 CPP 文件。
*   **问题 2**: `error C2027: use of undefined type 'tb::ui::QTreeView'`。
    *   原因: 在 `OutlinerInspector.h` 中，`QTreeView` 被错误地前置声明在 `tb::ui` 命名空间内（`class QTreeView;`），但它实际上属于全局命名空间（或 Qt 命名空间，但通常前置声明在全局即可）。这导致编译器认为成员变量 `m_treeView` 是 `tb::ui::QTreeView*` 类型，而 CPP 文件中包含的是全局的 `QTreeView` 定义，两者不匹配，或者在 CPP 中使用时找不到 `tb::ui::QTreeView` 的定义。
    *   修复: 在 `OutlinerInspector.h` 中，将 `class QTreeView;` 移出 `tb::ui` 命名空间，或者使用 `QT_FORWARD_DECLARE_CLASS(QTreeView)`（如果可用），最简单的是移出命名空间。

### 阶段 2: 只读数据模型
1.  创建 `OutlinerModel`。
2.  实现 `rowCount`、`columnCount`、`parent`、`index`。
3.  实现 `data` 以显示 `Node::name()` 或 `Entity::classname()`。
4.  将模型连接到视图。
5.  *结果*: 地图的静态树状视图（需要重新加载才能更新）。

### 阶段 3: 实时更新与选择
1.  将 `mdl::Map` 通知器连接到 `OutlinerModel`（插入/删除行）。
2.  实现双向选择同步。
3.  *结果*: 一个实时的、可交互的树。

**编译修复记录 (2026-01-09 - Phase 3)**:
*   **问题 1**: `error C2039: 'connect': is not a member of 'tb::NotifierConnection'`.
    *   原因: `NotifierConnection` 使用 `operator+=` 来添加新的连接，而不是 `connect` 成员函数（它返回连接 ID）。`Notifier::connect` 返回 ID，`NotifierConnection` 包装这些 ID。
    *   修复: 使用 `m_notifierConnection += m_map.notifier.connect(...)` 模式。
*   **问题 2**: `error C2039: 'commandProcessor': is not a member of 'tb::mdl::Map'`.
    *   原因: `mdl::Map` 的 `m_commandProcessor` 是私有的。应该使用 `Map::execute` 方法来执行命令。
    *   修复: 将 `m_map.commandProcessor().perform(...)` 替换为 `m_map.execute(...)`。
*   **问题 3**: `error C2064: term does not evaluate to a function` (lambda call syntax error), `Selection::nodes` 访问错误，以及 `QItemSelectionModel::SelectionFlag` 类型转换错误。
    *   原因:
        1. 试图在 lambda 中使用错误的语法调用 `selectNode`，或者类型转换问题。
        2. `mdl::Selection::nodes` 是一个 `std::vector<Node*>` 成员变量，而不是成员函数，不能加括号 `()` 调用。
        3. `QItemSelectionModel::SelectionFlag` 是枚举，`QItemSelectionModel::SelectionFlags` 是 `QFlags` 包装类。当传递 OR 组合的标志（如 `Select | Rows`）时，结果是 `SelectionFlags`，不能隐式转换为 `SelectionFlag`。
    *   修复:
        1. 修正了 lambda 调用语法。
        2. 将 `selection.nodes()` 改为 `selection.nodes`。
        3. 将 lambda 参数类型从 `QItemSelectionModel::SelectionFlag` 改为 `QItemSelectionModel::SelectionFlags`。

### 阶段 4: 完善
1.  添加图标（眼睛/锁）。
2.  添加搜索过滤器。
3.  实现拖放。

## 6. 代码结构建议

```
common/src/ui/
  outliner/
    OutlinerInspector.h/cpp
    OutlinerModel.h/cpp
    OutlinerView.h/cpp  (可选，如果需要自定义视图逻辑)
```

## 7. 资源集成 (2026-01-09)

已从参考项目 `TrenchBroom-Map_Inspector` 导入以下 Outliner 专用图标到 `app/resources/graphics/images/`：
*   `Map_cube.svg`: 用于表示 Brush。
*   `Map_entity.svg`: 用于表示 Entity。
*   `Map_folder.svg`: 用于表示 Group。
*   `Map_fullcube.svg`: 备用/变体。
*   `object_hidden.svg` / `object_show.svg`: 用于表示可见性状态。

## 8. 架构重构计划 (基于 LayerTreeWidget)

鉴于使用 `QSortFilterProxyModel` 进行递归过滤时遇到的稳定性问题（索引映射崩溃），以及对参考项目 `TrenchBroom-Map_Inspector` 中 `LayerTreeWidget` 实现的研究，决定对 Outliner 的实现策略进行重大调整。

### 核心变更
*   **废弃**: `QAbstractItemModel` + `QSortFilterProxyModel` + `QTreeView` 的组合。
*   **采用**: 直接继承 `QTreeWidget` 的自定义控件（类似于 `LayerTreeWidget`）。

### 新类设计
**`OutlinerTreeWidget`** (继承 `QTreeWidget`)

#### 1. 数据结构
不再依赖 Model 的 `index()` 和 `parent()` 的复杂计算，而是手动管理 `QTreeWidgetItem` 树。
*   每个 `QTreeWidgetItem` 将通过 `setData(0, Qt::UserRole, ...)` 绑定对应的 `mdl::Node*` 指针。
*   维护一个 `std::map<mdl::Node*, QTreeWidgetItem*>` 以便快速查找（用于选择同步）。

#### 2. 列定义
*   **第 0 列**: 节点名称与图标 (Entity/Group/Brush)。
*   **第 1 列**: 可见性切换 (使用 `object_show.svg` / `object_hidden.svg`)。
*   **第 2 列**: 锁定切换 (使用 `Lock_on.svg` / `Lock_off.svg`)。

#### 3. 关键功能实现
*   **构建树 (`updateTree`)**:
    *   清空树。
    *   递归遍历 `mdl::WorldNode`。
    *   根据节点类型 (`mdl::GroupNode`, `mdl::EntityNode`, `mdl::BrushNode`) 创建 Item 并设置对应的 SVG 图标。
*   **选择同步**:
    *   **Map -> UI**: 监听 `mdl::Map` 选择变更通知，通过 Map 查找对应的 Item 并设置 `setSelected(true)`。确保调用 `scrollToItem`。
    *   **UI -> Map**: 监听 `itemSelectionChanged` 信号，收集所有选中 Item 的 `mdl::Node*`，构建 `mdl::Selection` 并更新地图。
*   **拖放 (Drag & Drop)**:
    *   重写 `dropEvent`。
    *   计算目标位置（Target Item）。
    *   使用 `ReparentNodesCommand` 执行移动操作。

#### 4. 优势
*   **稳定性**: 消除 Proxy Model 索引映射错误导致的崩溃。
*   **简单性**: `QTreeWidget` API 更直观，易于控制 Item 的展开/折叠和图标设置。
*   **一致性**: 与项目中现有的 `LayerTreeWidget` 实现保持一致，便于维护。

## 9. 编译修复记录 (2026-01-09 - Phase 4)

*   **问题 1**: `error C2039: 'setNodesLocked': is not a member of 'tb::mdl'`
    *   原因: 试图调用不存在的 `setNodesLocked` 函数。
    *   修复: 替换为 `mdl::lockNodes` 和 `mdl::unlockNodes`，并包含头文件 `mdl/Map_NodeLocking.h`。
*   **问题 2**: `warning C4100: 'change': unreferenced parameter`
    *   原因: `onDocumentSelectionChanged` 回调中的参数未使用。
    *   修复: 注释掉参数名称 `/*change*/`。

