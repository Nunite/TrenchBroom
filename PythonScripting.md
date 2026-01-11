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

- UI：`View` → `Toggle Info Panel` → `Console` 页签
- 同时也会进入 TrenchBroom 的日志文件输出（由现有日志系统处理）

所以 `print(...)` 应该能在 Console 里看到。

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

## 注意事项 / 约束

- 脚本在 TrenchBroom 进程内运行，拥有与 TrenchBroom 相同的本机权限（读写文件、网络等）。只运行你信任的脚本。
- API 尽量走“可撤销/可重做”的编辑路径（例如 `Selection.set_property`），不要期望直接改 `Entity` 就能安全落入 undo 栈。
- 当前对象模型刻意保持最小：只覆盖 selection + entity properties + action dispatch。
