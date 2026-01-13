# TrenchBroom Python 脚本（实验性）

TrenchBroom 内嵌 Python（模块名 `tb`），用于：

- 执行现有菜单/快捷键 Action（`tb.execute_action`）
- 枚举所有 Action 路径（`tb.list_actions`）
- 通过一个最小对象模型读取/修改当前选择（`Document` / `Selection` / `Entity`）

当前仍是实验阶段：API 可能变动，功能覆盖也仅限一小部分编辑器能力。

## 使用方式

在 TrenchBroom 里：

- 打开一个 map 窗口
- 菜单：`Run` → `Run Python Script...`
- 选择一个 `.py` 文件执行

### 输出在哪里

脚本运行期间，Python 的 `sys.stdout` / `sys.stderr` 会被重定向到 TrenchBroom 的日志系统：

- UI：`View` → `Toggle Info Panel` → `Python Console` 页签
- 同时也会进入 TrenchBroom 的日志文件输出（由现有日志系统处理）

所以 `print(...)` 应该能在 Python Console 里看到。

### Plugin 面板（预览）

Inspector 里新增了一个 `Plugin` 页签（占位）。

它是一个“插件面板容器”，用来存放多个插件各自初始化出来的一块 UI 区域：

- 一个插件对应其中一块区域（可折叠）
- 容器允许同时显示多个插件区域（可滚动）

现在提供了最小 API 来创建和初始化插件面板：
`tb.add_plugin_panel(title: str, content: str | None = None) -> None`
`tb.create_plugin_panel(title: str) -> PluginPanel`

- 会在 `Plugin` 页签里创建一个新的可折叠面板，标题为 `title`
- 面板内显示一段只读的文本（如果 `content` 为空则显示占位提示）
- 调用后会自动切换到 `Plugin` 页签，方便查看

注意：当前支持基础文本/HTML展示与按钮触发 Action，后续将扩展为更丰富的插件 UI 能力。

### sys.path 规则

运行脚本前，会把脚本所在目录插入 `sys.path` 的开头，因此脚本可以 `import` 同目录下的其他 `.py` 文件。

## API 参考

模块名：`tb`

### 模块级函数

#### `tb.current_document() -> Document | None`

- 返回当前活动的 map 文档对象；如果当前没有活动的 map 窗口则返回 `None`
- 推荐在脚本入口优先使用它，避免抛异常

#### `tb.document() -> Document`

- 返回当前活动的 map 文档对象
- 如果当前没有活动的 map 窗口会抛 `RuntimeError`

#### `tb.transaction(name: str = "Python Script") -> Transaction`

创建一个事务对象（可用于 `with`），作用于当前活动文档的 undo 栈。

```python
import tb

with tb.transaction("My Batch Edit"):
    ...
```

#### `tb.execute_action(path: str) -> None`

按 action 路径执行一个 TrenchBroom 动作（与菜单/快捷键相同）。

- 示例路径：
  - `"Menu/Edit/Undo"`
  - `"Menu/Run/Compile..."`
  - `"Menu/File/Preferences..."`
- 异常：
  - `KeyError`：找不到该 action
  - `RuntimeError`：action 存在但当前不可用（disabled），或当前没有活动 map 窗口

#### `tb.list_actions() -> list[str]`

返回所有已注册的 action 路径列表（字符串）。

#### `tb.add_plugin_panel(title: str, content: str | None = None) -> None`

在 Inspector 的 `Plugin` 页签里添加一个新的面板，并显示文本内容。

- 参数：
  - `title`：面板标题
  - `content`：面板正文文本；可选
- 异常：
  - `RuntimeError`：当前没有活动的 map 窗口

#### `tb.create_plugin_panel(title: str) -> PluginPanel`

创建一个新的插件面板并返回 `PluginPanel` 对象，以便进一步自定义。

- 参数：
  - `title`：面板标题
- 返回：`PluginPanel` 对象
- 异常：
  - `RuntimeError`：当前没有活动的 map 窗口

### 类型：`PluginPanel`

`PluginPanel` 表示 Inspector 的 `Plugin` 页签里的一块可折叠面板内容区，用于脚本动态创建简单 UI。

当前 UI 能力刻意保持最小：以“标签 + 按钮 + 少量输入控件”为主。面板内控件的生命周期由 TrenchBroom 管理。

#### `PluginPanel.clear() -> None`

清空面板内所有控件。

#### `PluginPanel.add_label(text: str) -> None`

添加一段只读文本（自动换行）。

#### `PluginPanel.set_text(text: str) -> None`

用一段只读文本填充整个面板。

注意：这会先清空面板，因此会移除你之前添加的按钮和输入控件。

#### `PluginPanel.set_html(html: str) -> None`

用一段 HTML 富文本填充整个面板。

注意：这会先清空面板，因此会移除你之前添加的按钮和输入控件。

#### `PluginPanel.add_button(text: str, action_path: str | None = None) -> None`

添加一个按钮。

- `action_path`：可选 action 路径（例如 `"Menu/File/Preferences..."`）。提供后，点击按钮会触发对应 action。

#### `PluginPanel.add_button_callback(text: str, callback: Callable[[], Any]) -> None`

添加一个按钮；点击后调用传入的 Python 回调函数。

#### `PluginPanel.add_label_named(key: str, text: str) -> None`

添加一个“命名标签”。后续可以用 `set_label_text` 只更新该标签文本，而不重建整个面板 UI（不会丢失输入框里的值）。

`key` 在同一个 `PluginPanel` 内应唯一。

#### `PluginPanel.set_label_text(key: str, text: str) -> bool`

更新命名标签文本。

- 返回 `True`：找到并更新成功
- 返回 `False`：没有找到对应 key 的标签

#### `PluginPanel.add_int_field(key: str, label: str, value: int, min: int = 0, max: int = 999999) -> None`

添加一个整数输入框（SpinBox）。

`key` 在同一个 `PluginPanel` 内应唯一。

#### `PluginPanel.add_float_field(key: str, label: str, value: float, min: float = -1e9, max: float = 1e9, decimals: int = 3, step: float = 1.0) -> None`

添加一个浮点数输入框（DoubleSpinBox）。

`key` 在同一个 `PluginPanel` 内应唯一。

#### `PluginPanel.get_int_field(key: str) -> int`

读取整数输入框当前值。

- 异常：
  - `KeyError`：找不到对应 key 的输入框

#### `PluginPanel.get_float_field(key: str) -> float`

读取浮点数输入框当前值。

- 异常：
  - `KeyError`：找不到对应 key 的输入框

### 类型：`Document`

`Document` 表示当前 map 文档（注意：这是一个轻量包装，生命周期由 TrenchBroom UI 管理）。

#### `Document.current() -> Document | None`

与 `tb.current_document()` 相同。

#### `Document.selection -> Selection`

返回当前选择对象。

注意：`doc.selection()` 也能用（兼容旧脚本），但它并不是 `Document` 的方法；它是对 `Selection` 对象的调用，返回 `Selection` 自身。

#### `Document.get_selection() -> Selection`

返回当前选择对象（与 `Document.selection` 相同）。

#### `Document.transaction(name: str = "Python Script") -> Transaction`

创建一个事务对象（可用于 `with`），作用于此文档的 undo 栈。

#### `Document.vertex_tool_vertices() -> list[tuple[float, float, float]]`

返回当前“顶点工具（Vertex Tool）”里被选中的顶点坐标列表（vertex handles）。

- 返回值：`[(x, y, z), ...]`
- 如果当前没有选中任何顶点，返回空列表

### 类型：`Selection`

`Selection` 表示当前选择（Nodes + Face selection 的抽象）。注意 TrenchBroom 的选择语义有两层：

- `entities`：只包含“显式选中的 Entity 节点”
- `all_entities`：包含“命令实际会作用到的实体集合”，会把选中 brush/patch 的父实体、选中 group 内实体等纳入（这也是多数改属性命令的目标集合）

#### `Selection.entities -> list[Entity]` / `Selection.entities() -> list[Entity]`

返回显式选中的实体节点（可能为 0，即使你选中了 brush）。

#### `Selection.all_entities -> list[Entity]` / `Selection.all_entities() -> list[Entity]`

返回“命令目标实体集合”，包含：

- 选中的实体
- 选中 brush/patch 对应的父实体
- 选中 group 内包含的实体

#### `Selection.set_property(key: str, value: str, default_to_protected: bool = False) -> bool`

对 `Selection.all_entities` 的目标实体集合设置属性（可撤销/可重做）。

- 返回值 `bool`：表示命令是否成功应用（例如遇到选择冲突时可能返回 `False`）

#### `Selection.remove_property(key: str) -> bool`

对目标实体集合移除属性（可撤销/可重做）。

#### `Selection.rename_property(old_key: str, new_key: str) -> bool`

对目标实体集合重命名属性（可撤销/可重做）。

#### `Selection.clear() -> None`

清空当前选择（可撤销/可重做）。

#### `Selection.duplicate() -> None`

复制当前选择的 nodes（可撤销/可重做）。

#### `Selection.translate(x: float, y: float, z: float) -> bool`

平移当前选择（可撤销/可重做）。

#### `Selection.rotate(axis_x: float, axis_y: float, axis_z: float, angle_degrees: float, center_x: float | None = None, center_y: float | None = None, center_z: float | None = None) -> bool`

绕给定轴旋转当前选择（可撤销/可重做）。

- `axis_*`：旋转轴向量（例如 Z 轴 `(0, 0, 1)`）
- `angle_degrees`：角度，单位“度”
- `center_*`：可选旋转中心点；如果不传，默认使用当前 selection bounds 的中心

#### `Selection.brush_vertices() -> list[list[tuple[float, float, float]]]`

返回当前选择中所有 brush 的顶点坐标。

- 返回值结构：外层 list 每个元素对应一个 brush；内层是该 brush 的顶点 `(x, y, z)`
- 选中 brush face 时，会把该 face 所在 brush 也纳入结果（自动去重）

### 类型：`Transaction`

`Transaction` 用于把一段脚本编辑行为合并成一次 undo/redo（推荐用 `with`）。

#### `with tb.transaction(name): ...`

- 无异常退出：自动 `commit()`（因此只需要一次 Undo）
- 有异常退出：自动 `cancel()`（不把半成品留在文档里）

也可以手动控制：

- `Transaction.commit() -> bool`
- `Transaction.cancel() -> None`
- `Transaction.rollback() -> None`

### 类型：`Entity`

`Entity` 代表一个“实体节点”（可能是 worldspawn 或普通实体）。当前只提供只读查询接口（写入请用 `Selection.set_property` 等 undoable 接口）。

#### `Entity.classname -> str` / `Entity.classname() -> str`

返回实体 classname。

#### `Entity.keys() -> list[str]`

返回当前实体的所有属性 key（字符串列表）。

#### `Entity.get(key: str, default: Any = None) -> Any`

读取属性值：

- 若存在：返回字符串
- 若不存在：返回 `default`

### 内部类型（不稳定）

#### `tb._LogWriter`

内部用于 stdout/stderr 重定向，不建议脚本直接依赖。

## 示例

### 1) 最小脚本模板

```python
import tb

def main() -> None:
    doc = tb.Document.current()
    if doc is None:
        print("No active document")
        return

    sel = doc.selection
    print("all_entities:", len(sel.all_entities))

if __name__ == "__main__":
    main()
```

### 2) 查看当前选择到底选中了什么实体

```python
import tb

doc = tb.document()
sel = doc.selection

print("explicit entities:", len(sel.entities))
for e in sel.entities:
    print("  explicit:", e.classname)

print("all_entities:", len(sel.all_entities))
for e in sel.all_entities:
    print("  target:", e.classname)
```

### 3) 给当前选择的目标实体批量设置属性（支持 undo）

```python
import tb

doc = tb.document()
sel = doc.selection

ok = sel.set_property("message", "hello from python")
print("set_property ok:", ok)
```

### 4) 枚举并执行 action

```python
import tb

paths = tb.list_actions()
print("action count:", len(paths))
print("first 10:")
for p in sorted(paths)[:10]:
    print(" ", p)

tb.execute_action("Menu/File/Preferences...")
```

### 5) 事务包裹批量编辑（一次撤回）

```python
import tb

doc = tb.document()
sel = doc.selection

with tb.transaction("Python: duplicate 10x"):
    for _ in range(10):
        sel.duplicate()
        sel.translate(128, 0, 0)
```

### 6) 围绕“顶点工具选中的第一个顶点”旋转

```python
import tb

def main() -> None:
    doc = tb.Document.current()
    if doc is None:
        print("No active document")
        return

    verts = doc.vertex_tool_vertices()
    if len(verts) == 0:
        print("No vertex tool selection")
        return

    pivot_x, pivot_y, pivot_z = verts[0]

    sel = doc.selection
    with tb.transaction("Python: rotate around vertex"):
        sel.rotate(0, 0, 1, 15, pivot_x, pivot_y, pivot_z)

if __name__ == "__main__":
    main()
```

### 7) 添加一个插件面板

```python
import tb

def main() -> None:
    panel = tb.create_plugin_panel("My Plugin")
    panel.set_text("Hello from Python plugin!")
    panel.add_button("Open Preferences", "Menu/File/Preferences...")

if __name__ == "__main__":
    main()
```

## 注意事项 / 约束

- 脚本在 TrenchBroom 进程内运行，拥有与 TrenchBroom 相同的本机权限（读写文件、网络等）。只运行你信任的脚本。
- API 尽量走“可撤销/可重做”的编辑路径（例如 `Selection.set_property`），不要期望直接改 `Entity` 就能安全落入 undo 栈。
- 当前对象模型刻意保持最小：只覆盖 selection + entity properties + action dispatch。
