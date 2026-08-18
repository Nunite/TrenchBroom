# TrenchBroom Python API 参考手册 {#python_api_reference}

欢迎查阅 TrenchBroom Python API 参考文档。TrenchBroom 内置了高性能的 Python v2（`tb2`）运行时，允许开发者与关卡设计师自动化生成几何体、操作实体、检查地图结构，并构建声明式的原生交互 UI 面板。

::: {.api-grid}
[**1. 快速入门与核心架构**\
事务上下文、原子撤销/重做机制与 TrenchBroom 世界坐标系体系。](#quickstart){.api-card}

[**2. tb2 核心根模块**\
活动文档句柄获取、插件面板工厂函数与三维向量/平面数学基元。](#the_tb2_root_module){.api-card}

[**3. Document 地图文档访问**\
地图数据块访问、实体检索与生成、图层组织与逻辑对象组。](#tb2_document){.api-card}

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
with doc.transaction("Create Light Grid"):
    for x in range(-256, 384, 128):
        for y in range(-256, 384, 128):
            ent = doc.create_entity("light", tb2.Vec3(x, y, 64))
            ent.set("light", "200")
            ent.set("_color", "1 0.8 0.6")

print(f"Generated {len(doc.entities)} total entities in map.")
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

- **返回值**：<span class="type-badge">Document</span> 活动地图文档对象；若未打开任何地图则返回 `None`。
- **返回类型**：`tb2.Document`

```python
doc = tb2.current_document()
if doc is None:
    print("No map is currently open.")
```

#### `tb2.create_plugin_panel(panel_id, title)` {#tb2_create_plugin_panel}

在 **Plugins** 检查器标签页中创建并注册一个声明式交互面板。

- **参数**：
  - `panel_id` (*str*) – 面板的全局唯一标识符（例如 `"com.author.my_tool"`）。
  - `title` (*str*) – 检查器面板顶部显示的标题名称。
- **返回值**：<span class="type-badge">PluginPanel</span> 创建的面板实例对象。
- **返回类型**：`tb2.PluginPanel`

```python
panel = tb2.create_plugin_panel("com.example.align_tool", "Surface Aligner")
panel.add_label("Align selected faces to the world grid.")
```

#### `tb2.selected_brushes()` / `tb2.selectedBrushes()` {#tb2_selected_brushes}

返回当前活动选区中的所有 `Brush` 句柄列表。

- **返回值**：`list[tb2.Brush]`

#### `tb2.selected_entities()` / `tb2.selectedEntities()` {#tb2_selected_entities}

返回当前活动选区中的所有 `Entity` 句柄列表。

- **返回值**：`list[tb2.Entity]`

#### `tb2.selected_faces()` / `tb2.selectedFaces()` {#tb2_selected_faces}

返回当前所有选中 Brush 表面包含的所有 `Face` 句柄列表。

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

### 数学与几何基元 {#math_primitives}

#### `tb2.Vec3(x=0.0, y=0.0, z=0.0)` {#tb2_vec3}

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

`Document` 类代表当前打开的地图文件，负责管理撤销事务、选择状态、实体列表以及 Brush 几何体。

### 属性列表 {#document_properties}

| 属性 | 类型 | 描述 |
| :--- | :--- | :--- |
| `selection` | <span class="type-badge">Selection</span> | 活动选区容器，用于查询和空间变换选中的对象。 |
| `entities` | <span class="type-badge">list[Entity]</span> | 文档中的所有点实体与 Brush 实体列表。 |
| `brushes` | <span class="type-badge">list[Brush]</span> | 地图中的所有结构与细节 Brush 列表。 |
| `layers` | <span class="type-badge">list[Layer]</span> | 地图组织图层。 |
| `groups` | <span class="type-badge">list[Group]</span> | 逻辑对象组与链接组预制件。 |

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

#### `doc.create_entity(classname, origin)` {#doc_create_entity}

在世界坐标的指定原点处生成一个新的点实体。

- **参数**：
  - `classname` (*str*) – 实体类型名称（例如 `"info_player_start"`、`"light"`）。
  - `origin` (*tb2.Vec3*) – 世界坐标位置。
- **返回值**：<span class="type-badge">Entity</span> 新创建的实体对象。

#### `doc.find_entity(targetname)` {#doc_find_entity}

在文档中查找 `targetname` 属性匹配指定字符串的第一个实体。

- **参数**：
  - `targetname` (*str*) – 要匹配的目标名称字符串。
- **返回值**：<span class="type-badge">Entity | None</span> 匹配的实体对象；若未找到则返回 `None`。

#### `doc.delete_selection()` {#doc_delete_selection}

从地图中删除当前选中的所有 Brush、实体与多边形面。

---

## 选择集与几何变换：Selection {#tb2_selection}

`Selection` 对象提供对高亮选中几何体的直接查询与高级空间几何变换操作。

### 查询属性 {#selection_queries}

- `sel.brushes` (*list[Brush]*)：选中的 Brush 列表。
- `sel.entities` (*list[Entity]*)：选中的点实体列表。
- `sel.brush_faces` (*list[Face]*)：单独选中的 Brush 表面列表。
- `sel.vertices` (*list[Vec3]*)：顶点编辑模式下选中的顶点坐标列表。

### 几何变换方法 {#selection_transforms}

#### `sel.translate(dx, dy, dz)` {#sel_translate}

按给定的坐标增量平移所有选中的对象。

- **参数**：
  - `dx`, `dy`, `dz` (*float*) – 沿 X、Y、Z 轴的位移增量。

#### `sel.rotate(center, axis, angle_degrees)` {#sel_rotate}

围绕指定旋转中心和轴向量旋转选中对象。

- **参数**：
  - `center` (*tb2.Vec3*) – 旋转中心点。
  - `axis` (*tb2.Vec3*) – 旋转轴单位向量。
  - `angle_degrees` (*float*) – 旋转角度（角度制，顺时针方向）。

#### `sel.scale(center, scale_vector)` {#sel_scale}

相对于中心点缩放选中的对象。

- **参数**：
  - `center` (*tb2.Vec3*) – 缩放中心基准点。
  - `scale_vector` (*tb2.Vec3*) – 各轴向的缩放倍率向量。

#### `sel.duplicate()` {#sel_duplicate}

复制所有选中的 Brush 和实体，并自动将新建的副本设置为当前选区。

#### `sel.chamfer_vertices(distance)` {#sel_chamfer_vertices}

在指定距离处切角倒角选中的顶点。

- **参数**：
  - `distance` (*float*) – 距原顶点的切削内缩距离。

#### `sel.chamfer_edges(distance)` {#sel_chamfer_edges}

对选中的 Brush 棱边执行倒角操作。

---

## 地图图元与几何对象 {#geometry_and_elements}

### Brush {#tb2_brush}

表示由半空间平面围成的凸三维多面体。

- `brush.faces() -> list[Face]`：返回构成该 Brush 的所有多边形面列表。
- `brush.bounding_box() -> tuple[Vec3, Vec3]`：返回 `(min_bounds, max_bounds)` 包围盒。

### 面 Face {#tb2_face}

表示 Brush 的单个平面多边形边界表面。

- `face.material` (*str*)：赋予该面的材质/纹理名称。
- `face.vertices` (*list[Vec3]*)：按序排列的表面边界多边形顶点。
- `face.plane` (*Plane*)：该表面所在的空间几何平面。
- `face.offset` (*Vec3*)：UV 纹理平移偏移（U, V）。
- `face.scale` (*Vec3*)：UV 纹理缩放比例因子。
- `face.rotation` (*float*)：UV 纹理旋转角度（角度制）。
- `face.set_material(name: str)`：为该表面赋予新的材质。
- `face.align_to_world()`：将表面纹理坐标重置对齐到世界网格。
- `face.align_to_face(reference_face: Face)`：将 UV 贴图与相邻参考面匹配对齐。

### 实体 Entity {#tb2_entity}

代表点实体（如光源、生成点、怪物）和 Brush 实体（如 `func_door`、`trigger_multiple`）。

- `entity.classname` (*str*)：实体类定义名称。
- `entity.origin` (*tb2.Vec3*)：实体的世界坐标原点位置。
- `entity.get(key: str, default: str = "") -> str`：获取指定的属性键值字符串。
- `entity.set(key: str, value: str)`：设置或更新属性键值对。
- `entity.remove(key: str)`：移除指定的属性键值。
- `entity.is_brush_entity` (*bool*)：若实体包含 Brush 几何体则为 True。
- `entity.is_point_entity` (*bool*)：若实体为独立点标记则为 True。

---

## 插件界面与控件：PluginPanel {#tb2_pluginpanel}

`PluginPanel` 类允许 Python 插件在 **Plugins** 检查器标签页中构建交互式原生控件。

### 表单输入与控件 {#pluginpanel_controls}

| 方法 | 参数 | 描述 |
| :--- | :--- | :--- |
| `add_label(text)` | `text: str` | 添加静态提示文本。 |
| `add_label_named(key, text)` | `key: str, text: str` | 添加动态标签，其内容后续可通过 `set_label_text(key, text)` 动态更新。 |
| `add_text_field(key, label, value)` | `key: str, label: str, value: str` | 单行文本输入框。 |
| `add_int_field(key, label, value, min, max)` | `key, label, value: int, min: int, max: int` | 带有上下限的整数微调输入框。 |
| `add_float_field(key, label, value, min, max, decimals, step)` | `key, label, value: float, min, max, decimals: int, step: float` | 浮点数数值输入框。 |
| `add_checkbox(key, text, checked)` | `key: str, text: str, checked: bool` | 布尔值复选切换框。 |
| `add_combo_box(key, label, items, callback, current)` | `key, label, items: list[str], callback: callable, current: int` | 下拉选项选择框。 |
| `add_color_field(key, label, color)` | `key: str, label: str, color: str` | 颜色拾取选择器（`"R G B"` 格式）。 |
| `add_button(text, callback)` | `text: str, callback: callable` | 触发 Python 回调函数的按钮。 |

### 数据视图与布局容器 {#pluginpanel_containers}

- `add_table_widget(key, columns, rows, height, callback)`：显示支持多行选中的多列表格数据视图。
- `add_tree_widget(key, columns, rows, height, callback)`：显示支持节点折叠与展开的树形视图。
- `add_group(key, title)`：创建可折叠的可视化分组容器。
- `add_row(key)` / `add_column(key)`：横向与纵向排版布局容器。

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
    panel = tb2.create_plugin_panel("com.tb.array_gen", "Array Generator")
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
