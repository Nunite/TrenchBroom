# TrenchBroom Python API 现代化改进计划

本文档概述了将 TrenchBroom Python API (`tb` 模块) 现代化以符合 Blender 等现代 3D 软件标准的路线图。目标是从基于宏/受限的 API 过渡到完整的数据驱动对象模型。

## Python v2 当前架构

Python v2 目前以 `tb2` 模块作为正式实验入口；legacy `tb` 模块继续保留，不会在当前阶段被重定向或删除。v2 的目标不是立刻增加大量新能力，而是先把插件生命周期、错误处理、资源清理和可测试性做成可长期维护的基础。

### Manifest 插件模型

v2 插件是一个目录，目录中必须包含 `trenchbroom-plugin.json` 和入口脚本。首选项中的 Python 插件目录列表应指向这些插件目录。

```json
{
  "id": "example.my_plugin",
  "name": "My Plugin",
  "version": "1.0.0",
  "apiVersion": 2,
  "entry": "main.py",
  "description": "Optional description",
  "author": "Optional author"
}
```

`apiVersion` 当前只支持 `2`。manifest 解析错误、入口脚本缺失和加载错误会显示在 Preferences 的 Python 插件管理区域中。

### Session 与资源清理

每个 manifest 插件加载后都会创建独立的 `PythonPluginSession`。Session 记录插件 id、manifest、执行上下文、事件 callback、timer 和 plugin panel。插件卸载、地图窗口关闭或插件 reload 时，session 会清理它持有的 callback、timer 和 panel，避免回调继续访问已关闭的窗口。

v2 timer 由 session 持有：

```python
import tb2 as tb

token = tb.set_interval(lambda: print("tick"), 1000)
tb.clear_interval(token)
tb.set_timeout(lambda: print("once"), 500)
```

timer callback 抛异常时会写入 Python logger，不应导致主程序崩溃。

### Transaction 规则

v2 写 API 必须走 TrenchBroom 的 command/transaction 路径。当前已实现的基础写 API 包括：

```python
import tb2 as tb

doc = tb.current_document()
entity = doc.entities[0]

entity.set("key", "value")
entity.remove("key")

brush = entity.brushes[0]
face = brush.faces()[0]
face.set_material("wall01")

doc.select([brush])
doc.clear_selection()
```

如果脚本已经在 `with doc.transaction("Name"):` 中，写 API 会加入当前 v2 transaction。否则写 API 会创建一个短事务。`with` 块内发生异常时 transaction 会取消，保证 undo/redo 语义可预测。

### 示例插件

v2 manifest 示例位于 `python/examples/v2`：

* `hello_panel`: 创建简单 Plugin Inspector 面板。
* `event_callback`: 注册 `selection_changed` callback。
* `timer`: 使用 session-owned interval timer。
* `entity_property_edit`: 通过事务式 v2 API 修改 entity property。

## 优先级定义

*   **紧急 (P0)**: 阻碍基本程序化建模任务的关键缺失功能。
*   **高 (P1)**: 针对可用性和代码质量的重要架构改进。
*   **中 (P2)**: 针对 UI 和工作流集成的增强。
*   **低 (P3)**: 高级用户锦上添花的功能。

---

## 1. 紧急 (P0) - 核心数据模型访问

**问题**: 目前，`Selection` 仅提供 `brush_vertices` 作为原始元组列表。无法以结构化的方式访问单个 Brush（笔刷）、Face（面）或其属性（纹理、UV）。

### 任务

1.  **暴露 `Brush` 对象** (✅ 已完成)
    *   **状态**: 已实现 `tb.Brush` 类型及 `Entity.brushes` 属性。
    *   **可行性**: 100% (封装 `mdl::BrushNode` / `mdl::Brush`)
    *   **必要性**: 关键 (Critical)
    *   **描述**: 创建 Python 类型 `tb.Brush`。
    *   **API 目标**:
        ```python
        brush = entity.brushes[0]
        brush.delete()
        ```

2.  **暴露 `Face` 对象** (✅ 已完成)
    *   **状态**: 已实现 `tb.Face` 类型，支持纹理/UV/法线访问。
    *   **可行性**: 100% (封装 `mdl::BrushFace`)
    *   **必要性**: 关键 (Critical)
    *   **描述**: 允许访问面数据（纹理、投影、平面）。
    *   **API 目标**:
        ```python
        for face in brush.faces():
            face.texture_name = "wad/wall01"
            face.offset = (16.0, 0.0)
        ```

3.  **工厂函数 (`create_*`)** (✅ 已完成)
    *   **状态**: 已实现 `tb.create_brush(points)`。
    *   **可行性**: 90% (需要安全地挂钩到 `mdl::Map` 创建逻辑)
    *   **必要性**: 关键 (Critical)
    *   **描述**: 允许从头创建新几何体，而不仅仅是修改现有选择。
    *   **API 目标**:
        ```python
        new_brush = tb.create_brush(vertices=[...])
        ```

---

## 2. 高 (P1) - 数学与类型

**问题**: 几何数据目前以原始元组 `(x, y, z)` 交换。这使得向量数学（加法、叉积）对脚本编写者来说既繁琐又容易出错。

### 任务

1.  **暴露 `Vec3` 类** (✅ 已完成)
    *   **状态**: 已实现 `tb.Vec3`，支持运算符重载及常用数学方法 (`dot`, `cross`, `length`, `normalize`)。
    *   **可行性**: 80% (封装 `math::Vec3` 或使用轻量级结构体)
    *   **必要性**: 高 (High)
    *   **描述**: 具有运算符重载的适当向量类。
    *   **API 目标**:
        ```python
        v = tb.Vec3(1, 2, 3)
        v2 = v * 2 + tb.Vec3(0, 1, 0)
        ```

2.  **暴露 `Plane` 类** (✅ 已完成)
    *   **状态**: 已实现 `tb.Plane`。
    *   **可行性**: 80%
    *   **必要性**: 高 (High)
    *   **描述**: 定义笔刷几何体所必需。

3.  **增强的 Selection API** (✅ 已完成)
    *   **状态**: 已实现 `select`, `deselect`, `add`, `set` 等方法。
    *   **可行性**: 100%
    *   **必要性**: 高 (High)
    *   **描述**: 允许除当前工具之外的程序化选择更改。
    *   **API 目标**:
        ```python
        tb.Selection.select(brush)
        tb.Selection.deselect_all()
        ```

4.  **暴露 `Material` 与 `MaterialCollection`** (✅ 已完成)
    *   **状态**: 已实现 `tb.Material`, `tb.MaterialCollection` 及 `Document.materials`。
    *   **可行性**: 100%
    *   **必要性**: 高 (High)
    *   **描述**: 允许访问项目中可用的纹理和材质集合。
    *   **API 目标**:
        ```python
        for mat in tb.Document.current().materials:
            print(mat.name, mat.width, mat.height)
        ```

---

## 3. 中 (P2) - UI 与事件

**问题**: `PluginPanel` 有用但有限。不存在对用户操作做出反应的事件系统。

### 任务

1.  **事件系统** (✅ 已完成)
    *   **状态**: 已实现 `tb.register_callback` 和 `tb.unregister_callback`，支持 `selection_changed` 事件。
    *   **可行性**: 100%
    *   **必要性**: 中 (Medium)
    *   **描述**: 用于 `on_selection_changed` 的钩子。
    *   **API 目标**:
        ```python
        def my_callback():
            print("Selection changed!")
        tb.register_callback("selection_changed", my_callback)
        ```

2.  **高级 UI 控件**
    *   **状态**: 部分完成 (ColorPicker 已实现, 材质列表可通过 API 访问，但缺少专用 UI 控件)。
    *   **可行性**: 70%
    *   **必要性**: 中 (Medium)
    *   **描述**: 向 `PluginPanel` 添加 ColorPickers (颜色选择器), TextureBrowsers (纹理浏览器)。

---

## 4. 低 (P3) - 高级几何

**问题**: CSG 操作仅限内部使用。

### 任务

1.  **暴露 CSG 操作**
    *   **可行性**: 50% (安全暴露比较复杂)
    *   **必要性**: 低 (Low)
    *   **描述**: 允许通过 Python 进行布尔运算（减去、相交）。

---

## 实施路线图（推荐顺序）

1.  **`Vec3`**: 首先实现，因为它是 `Brush` API 的依赖项。
2.  **`Brush` & `Face` 封装**: 数据模型的核心。
3.  **`create_brush`**: 启用生成式工作流。
4.  **纹理/UV 访问**: 启用纹理脚本。

## 参考资料

*   **C++ 源码**: `common/src/mdl/BrushNode.h`, `common/src/mdl/BrushFace.h`
*   **Python 存根 (Stub)**: `python/src/tb/__init__.py`
