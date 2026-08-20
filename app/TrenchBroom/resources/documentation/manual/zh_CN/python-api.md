# TrenchBroom Python API 参考手册 {#python_api_reference}

欢迎查阅 TrenchBroom Python API 参考文档。TrenchBroom 内置了高性能的 Python v2（`tb2`）运行时，允许开发者与关卡设计师自动化生成几何体、操作实体、检查地图结构，并构建声明式的原生交互 UI 面板。

::: {.api-grid}
[**1. 快速入门与核心架构**\
事务上下文、原子撤销/重做机制与 TrenchBroom 世界坐标系体系。](#quickstart){.api-card}

[**2. tb2 核心根模块**\
活动文档句柄获取、插件面板工厂函数与三维向量/平面数学基元。](#the_tb2_root_module){.api-card}

[**3. Document 地图文档访问**\
地图查询、选区、材质、事务与 UV 更新。](#tb2_document){.api-card}

[**4. Selection 选区与几何变换**\
几何空间变换（平移/旋转/缩放）、多边形克隆与顶点/棱边倒角。](#tb2_selection){.api-card}

[**5. 地图图元与几何对象**\
凸多面体 Brush、多边形 Face 表面与 UV 对齐、Entity 键值属性读写。](#geometry_and_elements){.api-card}

[**6. PluginPanel 插件界面系统**\
声明式原生表单、微调输入框、调色板、多列表格与树形视图。](#tb2_pluginpanel){.api-card}
:::

## 快速入门与核心架构 {#quickstart}

TrenchBroom 中的所有脚本均通过内置的 `tb2` 模块与编辑器进行交互。你可以在 **Python 控制台** 中交互式执行命令，也可以编写基于清单的常驻插件。

```python
import tb2

# Access the active map document
doc = tb2.current_document()

# Wrap modifications in a named transaction for atomic undo/redo
with doc.transaction("Normalize Selected Lights"):
    for ent in doc.selection.all_entities:
        if ent.classname == "light":
            ent["light"] = "200"

print(f"Map contains {len(doc.entities)} entities.")
```

### 核心概念 {#key_concepts}

- **事务完整性**：所有对地图文档的修改均应包裹在 `with doc.transaction("Action Name"):` 语句块中。如果在块内发生未捕获的 Python 异常，所有已做出的更改将自动回滚。
- **即时选择响应**：对 `doc.selection` 的操作会立即同步更新 2D/3D 视口和检查器面板。
- **坐标系体系**：TrenchBroom 采用标准的 Quake/GoldSrc 世界坐标系：X 轴为右（东），Y 轴为前（北），Z 轴为上。

---

## 核心根模块：tb2 {#the_tb2_root_module}

根模块 `tb2` 提供了对活动文档的顶层访问、插件 UI 工厂函数以及三维向量数学基元。

### 顶层函数 {#tb2_functions}

#### `tb2.current_document()` {#tb2_current_document}

返回编辑器中当前打开的活动地图文档句柄。

- **返回值**：<span class="type-badge">Document</span> 活动地图文档对象。
- **返回类型**：`tb2.Document`
- **异常**：没有活动地图文档时抛出 `RuntimeError`。

```python
doc = tb2.current_document()
print(doc.path)
```

#### `tb2.create_plugin_panel(title)` {#tb2_create_plugin_panel}

在 **Plugins** 检查器标签页中创建并注册一个声明式交互面板。

- **参数**：
  - `title` (*str*) – 检查器面板顶部显示的标题名称。
- **返回值**：<span class="type-badge">PluginPanel</span> 创建的面板实例对象。
- **返回类型**：`tb2.PluginPanel`

```python
panel = tb2.create_plugin_panel("Surface Aligner")
panel.add_label("Align selected faces to the world grid.")
```

#### `tb2.selected_brushes()` / `tb2.selectedBrushes()` {#tb2_selected_brushes}

返回当前活动选区中的所有 `Brush` 句柄列表。

- **返回值**：`list[tb2.Brush]`

#### `tb2.selected_entities(include_brushes=False)` / `tb2.selectedEntities(include_brushes=False)` {#tb2_selected_entities}

返回直接选中的 `Entity` 句柄。传入 `include_brushes=True` 时，还会包含选中 Brush 和单独选中面的父实体。

- **返回值**：`list[tb2.Entity]`

#### `tb2.selected_faces()` / `tb2.selectedFaces()` {#tb2_selected_faces}

返回单独选中的 `Face` 句柄列表。选中整个 Brush 不会自动展开为该 Brush 的全部面。

- **返回值**：`list[tb2.Face]`

#### `tb2.translate(...)` {#tb2_translate}

按指定偏移向量平移活动选区或目标对象（自动事务保护）。

#### `tb2.rotate(...)` {#tb2_rotate}

旋转活动选区或目标对象。支持欧拉角 `rotate(rx, ry, rz)` 或轴角 `rotate(ax, ay, az, angle)`。

#### `tb2.scale(...)` {#tb2_scale}

统一或非统一缩放活动选区或目标对象（自动事务保护）。

#### `tb2.duplicate(target=None)` {#tb2_duplicate}

复制活动选区（或指定目标对象），并将活动选区更新为新克隆的副本。

#### `tb2.delete_selection()` / `tb2.deleteSelection()` {#tb2_delete_selection}

从活动地图中删除当前选中的所有几何体和实体。

#### `tb2.deselect_all()` / `tb2.deselectAll()` {#tb2_deselect_all}

清除当前活动地图中的所有选区。

#### 选区与事件辅助函数 {#tb2_selection_and_event_helpers}

- `tb2.selection() -> Selection`：返回当前选区句柄。
- `tb2.selected_all_entities()` / `tb2.selectedAllEntities()`：返回直接选中的实体，以及选中 Brush 和面的父实体。
- `tb2.register_callback(event, callback) -> int`：为 `selection_changed`、`document_loaded` 或 `document_saved` 注册无参数回调。
- `tb2.unregister_callback(token)`：注销事件回调。
- `tb2.set_timeout(callback, milliseconds)` / `tb2.set_interval(callback, milliseconds)`：创建插件会话定时器；定时器要求常驻 UI 插件会话。
- `tb2.clear_interval(timer_id)`：取消任一类型的定时器。

### 数学与几何基元 {#math_primitives}

#### `tb2.Vec3(x, y, z)` {#tb2_vec3}

表示坐标、偏移量和方向的三维笛卡尔向量。

- **属性**：
  - `x` (*float*)：X 轴坐标分量。
  - `y` (*float*)：Y 轴坐标分量。
  - `z` (*float*)：Z 轴坐标分量。
- **方法**：
  - `length() -> float`：欧几里得向量长度（模）。
  - `normalized() -> Vec3`：同方向的单位向量。
  - `dot(other: Vec3) -> float`：向量点积。
  - `cross(other: Vec3) -> Vec3`：向量叉积。

```python
pos = tb2.Vec3(128.0, 64.0, 32.0)
offset = tb2.Vec3(0.0, 0.0, 16.0)
target = pos + offset
```

#### `tb2.Plane(normal, dist)` {#tb2_plane}

黑塞法线式平面定义（$N \cdot P - D = 0$）。

- **参数**：
  - `normal` (*tb2.Vec3*) – 归一化的平面法线向量。
  - `dist` (*float*) – 原点沿法线方向到平面的距离。

---

## 地图文档访问：Document {#tb2_document}

`Document` 类代表当前打开的地图文件，提供地图查询、事务、选区控制和 UV 更新。

### 属性列表 {#document_properties}

| 属性 | 类型 | 描述 |
| :--- | :--- | :--- |
| `selection` | <span class="type-badge">Selection</span> | 活动选区容器，用于查询和空间变换选中的对象。 |
| `entities` | <span class="type-badge">list[Entity]</span> | 文档中的所有点实体与 Brush 实体列表。 |
| `path` | <span class="type-badge">str \| None</span> | 当前地图路径；新建未保存地图通常为 `"unnamed.map"`，仅内部路径为空时返回 `None`。 |
| `materials` | <span class="type-badge">list[Material]</span> | 当前地图加载的材质。 |
| `material_collections` | <span class="type-badge">list[MaterialCollection]</span> | 当前加载的材质集合。 |

### 核心方法 {#document_methods}

#### `doc.transaction(name)` {#doc_transaction}

上下文管理器，将内部的所有文档修改合并为单个撤销/重做步骤。

- **参数**：
  - `name` (*str*) – 显示在撤销历史记录中的人类可读动作名称。

```python
with doc.transaction("Duplicate and Move"):
    doc.selection.duplicate()
    doc.selection.translate(0, 0, 64)
```

其他文档方法包括 `save()`、`reload()`、`select(objects)`、`clear_selection()`、`vertex_tool_vertices()`、`set_triangle_uvs(triangles)`、`set_face_uvs(updates)` 和 `set_face_uvs_with_split(updates)`。

### 句柄生命周期 {#python_handle_lifetime}

`Document`、`Entity`、`Brush` 和 `Face` 对象是实时句柄，而不是数据快照。关闭或重新加载文档、删除节点会使相关句柄失效；改变 Brush 几何也会使此前取得的 `Face` 句柄失效。访问失效句柄会抛出 `RuntimeError`。发生这些变化后，应从 `tb2.current_document()`、当前选区或父对象重新获取需要长期使用的对象。

---

## 选择集与几何变换：Selection {#tb2_selection}

`Selection` 对象提供对高亮选中几何体的直接查询与高级空间几何变换操作。

### 查询属性 {#selection_queries}

- `sel.entity` (*Entity | None*)：首个相关实体，包括选中 Brush 或面的父实体；空选区返回 `None`。
- `sel.brush` (*Brush | None*)：首个选中的 Brush。
- `sel.properties` (*dict[str, str] | None*)：首个相关实体的属性快照。
- `sel.classname` (*str | None*)：首个相关实体的 classname。
- `sel.entities` (*list[Entity]*)：直接选中的实体。
- `sel.all_entities` (*list[Entity]*)：直接选中的实体，以及选中 Brush 和单独选中面的父实体；空选区返回空列表。
- `sel.brushes` (*list[Brush]*)：选中的 Brush。
- `sel.brush_faces` (*list[Face]*)：单独选中的 Brush 表面列表。

`sel[key]` 和 `key in sel` 从首个相关实体读取。`sel[key] = value` 等价于 `sel.set_property(key, value)`，会写入所有相关实体。只有面被选中时，目标是该面所属 Brush 的父实体。对 `set_property()` 使用 `create_if_missing=False` 可只更新已经包含该属性键的实体；没有匹配实体时返回 `False`。

### 几何变换方法 {#selection_transforms}

#### `sel.translate(dx, dy, dz)` {#sel_translate}

按给定的坐标增量平移所有选中的对象。

- **参数**：
  - `dx`, `dy`, `dz` (*float*) – 沿 X、Y、Z 轴的位移增量。

#### `sel.rotate(axis_x, axis_y, axis_z, angle_degrees, center_x=None, center_y=None, center_z=None)` {#sel_rotate}

围绕指定旋转中心和轴向量旋转选中对象。

- **参数**：旋转轴分量、角度值和可选的旋转中心分量。

#### `sel.scale(scale_x, scale_y, scale_z, center_x=None, center_y=None, center_z=None)` {#sel_scale}

相对于中心点缩放选中的对象。

- **参数**：各轴缩放倍率和可选的缩放中心分量。

#### `sel.duplicate()` {#sel_duplicate}

复制所有选中的 Brush 和实体，并自动将新建的副本设置为当前选区。

#### `sel.chamfer_vertices(distance)` {#sel_chamfer_vertices}

在指定距离处切角倒角选中的顶点。

- **参数**：
  - `distance` (*float*) – 距原顶点的切削内缩距离。

#### `sel.chamfer_edges(distance, segments=1)` {#sel_chamfer_edges}

对选中的 Brush 棱边执行倒角操作。

---

## 地图图元与几何对象 {#geometry_and_elements}

### Brush {#tb2_brush}

表示由半空间平面围成的凸三维多面体。

- `brush.entity` (*Entity*)：返回拥有该 Brush 的父实体。
- `brush.faces()` (*list[Face]*)：返回构成该 Brush 的所有多边形面列表。

### 面 Face {#tb2_face}

表示 Brush 的单个平面多边形边界表面。

- `face.material` (*str*)：赋予该面的材质/纹理名称。
- `face.texture_name` (*str*)：材质/纹理名称的别名。
- `face.vertices` (*list[Vec3]*)：按序排列的表面边界多边形顶点。
- `face.uv_loops` (*list*)：UV 循环数据。
- `face.offset` (*tuple[float, float]*)：UV 纹理平移偏移（U, V）。
- `face.scale` (*tuple[float, float]*)：UV 纹理缩放比例因子。
- `face.rotation` (*float*)：UV 纹理旋转角度（角度制）。
- `face.surface_contents` (*int | None*)：表面 contents 值。
- `face.surface_flags` (*int | None*)：表面 flags 值。
- `face.surface_value` (*float | None*)：表面 value 值。
- `face.set_material(name: str)`：为该表面赋予新的材质。
- `face.set_uv_loops(loops)`：写入 UV 循环数据。

### 实体 Entity {#tb2_entity}

代表点实体（如光源、生成点、怪物）和 Brush 实体（如 `func_door`、`trigger_multiple`、`worldspawn`）。支持标准 Python 字典操作与遍历。

- `entity.classname` (*str*)：实体类定义名称。
- `entity.properties` (*dict[str, str]*)：包含该实体所有键值对属性的 Python 字典。
- `entity.brushes` (*list[Brush]*)：该实体所拥有的所有 Brush 几何体。
- `entity[key]` / `entity[key] = value`：使用下标语法读写实体属性。
- `key in entity` (*bool*)：判断实体是否包含指定属性键。
- `entity.keys()` (*list[str]*)：所有属性键名列表。
- `entity.values()` (*list[str]*)：所有属性值列表。
- `entity.items()` (*list[tuple[str, str]]*)：所有 `(key, value)` 元组列表。
- `entity.get(key: str, default: str = None) -> str`：获取指定属性值，缺失时返回默认值。
- `entity.set(key: str, value: str)`：设置或更新属性键值对。
- `entity.remove(key: str)`：移除指定的属性键值。
- `len(entity)` (*int*)：实体包含的属性数量。

---

## 插件界面与控件：PluginPanel {#tb2_pluginpanel}

`PluginPanel` 类允许 Python 插件在 **Plugins** 检查器标签页中构建交互式原生控件。

### 表单输入与控件 {#pluginpanel_controls}

| 方法 | 参数 | 描述 |
| :--- | :--- | :--- |
| `add_label(text)` | `text: str` | 添加静态提示文本。 |
| `add_label_named(key, text)` | `key: str, text: str` | 添加动态标签，其内容后续可通过 `set_label_text(key, text)` 动态更新。 |
| `add_html_view(key, html, height, callback)` | `key, html, height, callback` | 添加富 HTML 内容，可用 `set_html_view(key, html)` 更新。 |
| `add_line_edit(text, callback)` | `text: str, callback: callable` | 文本变化时调用回调的兼容文本框。 |
| `add_text_field(key, label, value)` | `key: str, label: str, value: str` | 单行文本输入框。 |
| `add_text_area(key, label, value, height)` | `key, label, value, height` | 多行文本输入框。 |
| `add_int_field(key, label, value, min, max)` | `key, label, value: int, min: int, max: int` | 带有上下限的整数微调输入框。 |
| `add_float_field(key, label, value, min, max, decimals, step)` | `key, label, value: float, min, max, decimals: int, step: float` | 浮点数数值输入框。 |
| `add_checkbox(key, text, checked)` | `key: str, text: str, checked: bool` | 布尔值复选切换框。 |
| `add_combo_box(key, label, items, callback, current)` | `key, label, items: list[str], callback: callable, current: int` | 下拉选项选择框。 |
| `add_color_field(key, label, color)` | `key: str, label: str, color: tuple[int, int, int]` | RGB 颜色拾取器。 |
| `add_button(text, callback)` | `text: str, callback: callable` | 触发 Python 回调函数的按钮。 |
| `add_button_callback(text, callback)` | `text: str, callback: callable` | `add_button` 的兼容别名。 |

命名字段提供对应的读取方法，包括 `get_text_field`、`get_text_area`、`get_int_field`、`get_float_field`、`get_checkbox`、`get_combo_box_text` 和 `get_color_field`。文本字段与文本区域还提供 `set_text_field` 和 `set_text_area`。

### 数据视图与布局容器 {#pluginpanel_containers}

- `add_table_widget(key, columns, rows, height, callback)`：显示支持多行选中的多列表格数据视图。
- `set_table_widget_rows(key, rows)`：替换表格行。
- `add_tree_widget(key, columns, rows, height, callback)`：显示支持节点折叠与展开的树形视图。
- `set_tree_widget_items(key, rows)`：替换树形列表项。
- `add_group(key, title)`：创建可折叠的可视化分组容器。
- `add_row(key)` / `add_column(key)`：横向与纵向排版布局容器。
- `set_widget_visible(key, visible)`：显示或隐藏命名控件。
- `clear()`：清空当前面板容器中的所有控件。

---

## 完整代码示例 {#runnable_examples}

### 示例 1：线性阵列复制生成器 {#example_linear_array}

```python
import tb2

panel = None

def on_generate():
    doc = tb2.current_document()
    if not doc or (not doc.selection.brushes and not doc.selection.entities):
        panel.set_label_text("status", "Error: Please select objects to duplicate.")
        return

    count = panel.get_int_field("count")
    dx = panel.get_float_field("dx")
    dy = panel.get_float_field("dy")
    dz = panel.get_float_field("dz")

    with doc.transaction(f"Linear Array ({count} copies)"):
        for _ in range(count):
            doc.selection.duplicate()
            doc.selection.translate(dx, dy, dz)

    panel.set_label_text("status", f"Success: Created {count} copies.")

def init_plugin():
    global panel
    panel = tb2.create_plugin_panel("Array Generator")
    panel.add_label("Duplicate active selection along a vector:")

    group = panel.add_group("params", "Parameters")
    group.add_int_field("count", "Count", value=4, min=1, max=100)
    group.add_float_field("dx", "Step X", value=128.0, min=-4096.0, max=4096.0, decimals=1, step=16.0)
    group.add_float_field("dy", "Step Y", value=0.0, min=-4096.0, max=4096.0, decimals=1, step=16.0)
    group.add_float_field("dz", "Step Z", value=0.0, min=-4096.0, max=4096.0, decimals=1, step=16.0)

    panel.add_button("Generate Array", on_generate)
    panel.add_label_named("status", "Ready")

init_plugin()
```

### 示例 2：批量光源属性规范化工具 {#example_batch_lights}

```python
import tb2

def randomize_light_colors():
    doc = tb2.current_document()
    if not doc:
        return

    lights = [e for e in doc.entities if e.classname == "light"]
    with doc.transaction("Normalize Light Values"):
        for light in lights:
            # Ensure standard brightness value
            if not light.get("light"):
                light.set("light", "300")

    print(f"Updated {len(lights)} lights.")

randomize_light_colors()
```
