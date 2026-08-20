# TrenchBroom Python v2 (tb2) API 完整速查手册

本文档为 TrenchBroom 嵌入式 Python v2 (`tb2`) 模块的完整 API 规格参考手册。

---

## 模块结构速查

```text
tb2
├── 类 (Classes)
│   ├── Vec3                  # 三维向量
│   ├── Plane                 # 三维几何平面
│   ├── Document              # 地图文档句柄
│   ├── Selection             # 当前选区管理器
│   ├── Entity                # 实体对象
│   ├── Brush                 # Brush 几何体对象
│   ├── Face                  # Brush 多边形面对象
│   ├── Material              # 材质对象
│   ├── MaterialCollection    # 材质集合（如 WAD、Shader 包）
│   ├── Transaction           # Undo/Redo 事务上下文
│   └── PluginPanel           # 声明式 UI 面板容器
│
└── 模块全局函数 (Functions)
    ├── selected_brushes() -> list[Brush]
    ├── selected_entities(include_brushes=False) -> list[Entity]
    ├── selected_all_entities() -> list[Entity]
    ├── selection() -> Selection
    ├── selected_faces() -> list[Face]
    ├── translate(...) / rotate(...) / scale(...)
    ├── duplicate(target=None)
    ├── delete_selection() / deselect_all()
    ├── current_document() -> Document
    ├── document() -> Document
    ├── create_plugin_panel(title) -> PluginPanel
    ├── create_brush(points, material=None) -> Brush
    ├── list_actions() -> list[str]
    ├── execute_action(action_id)
    ├── register_callback(event, callback) -> int
    ├── unregister_callback(token)
    ├── set_timeout(callback, milliseconds) -> int
    ├── set_interval(callback, milliseconds) -> int
    └── clear_interval(timer_id)
```

---

## 1. 核心类定义

### `tb2.Vec3`
三维双精度浮点数几何向量。

#### 构造函数
- `Vec3(x: float, y: float, z: float)`

#### 属性
- `x: float` - X 轴分量（可读写）
- `y: float` - Y 轴分量（可读写）
- `z: float` - Z 轴分量（可读写）

#### 运算符与方法
- `vec + other -> Vec3` - 向量加法
- `vec - other -> Vec3` - 向量减法
- `vec * factor -> Vec3` - 标量乘法
- `vec / divisor -> Vec3` - 标量除法（除以 0 会抛出运行时异常）
- `dot(other: Vec3) -> float` - 点积
- `cross(other: Vec3) -> Vec3` - 叉积
- `length() -> float` - 计算向量模长
- `normalize() -> Vec3` - 归一化为单位向量
- `normalized() -> Vec3` - 返回单位向量副本
- `__iter__()` - 支持解包，如 `x, y, z = vec`

---

### `tb2.Plane`
三维平面，由法向量及原点到平面的距离定义。

#### 构造函数与静态方法
- `Plane(normal: Vec3, dist: float)`
- `Plane.from_points(p1: Vec3, p2: Vec3, p3: Vec3) -> Plane` - 通过不共线的三点构建平面

#### 属性
- `normal: Vec3` - 平面法向量
- `dist: float` - 距离标量

#### 方法
- `distance(point: Vec3) -> float` - 计算点到平面的带符号距离
- `project(point: Vec3) -> Vec3` - 计算点在平面上的正交投影点

---

### `tb2.Document`
表示当前在 TrenchBroom 中打开的地图文档。

#### 属性
- `path: str | None` - 当前地图路径（只读）；新建未保存文档通常为 `"unnamed.map"`，仅内部路径为空时为 `None`
- `entities: list[Entity]` - 地图中包含的所有实体列表（只读）
- `selection: Selection` - 获取当前文档的选区控制器（只读）
- `materials: list[Material]` - 当前地图已加载的全部材质对象（只读）
- `material_collections: list[MaterialCollection]` - 当前地图加载的材质集合（只读）

#### 方法
- `transaction(name: str = "Python v2 Script") -> Transaction` - 创建事务上下文管理器
- `save()` - 保存当前地图文档
- `reload()` - 从磁盘重新载入地图文档
- `select(objects: Iterable[Entity | Brush])` - 选中指定的一组对象
- `clear_selection()` - 清除当前所有选中状态
- `vertex_tool_vertices() -> list[Vec3]` - 获取当前顶点工具激活状态下的顶点列表
- `set_face_uvs(updates: list[tuple[Face, ...]])` - 批量应用面的 UV 更新
- `set_face_uvs_with_split(updates: list[tuple[Face, ...]])` - 允许面分割的 UV 批量应用

---

### `tb2.Selection`
选区变换与批量操作控制器。

#### 属性
- `entity: Entity | None` - 首个相关实体（包括选中 Brush 或单独选中面的父实体；空选区为 `None`）
- `brush: Brush | None` - 首个选中的 Brush
- `properties: dict[str, str] | None` - 首个相关实体的属性快照
- `classname: str | None` - 首个相关实体的 classname
- `entities: list[Entity]` - 直接选中的实体对象（不含 Brush 所属实体）
- `all_entities: list[Entity]` - 直接选中的实体，以及选中 Brush 或单独选中面的父实体；空选区为空列表
- `brushes: list[Brush]` - 选中的所有 Brush 对象
- `brush_faces: list[Face]` - 单独选中的 Brush 面对象，不会展开选中 Brush 的全部面

#### 方法
- `set_property(key: str, value: str, create_if_missing: bool = True)` - 批量向选中实体写入属性
- `brush_vertices() -> list[list[Vec3]]` - 获取选中 Brush 的顶点列表
- `triangle_uvs() -> dict` - 获取选中面的三角形 UV 数据
- `set(objects)` - 用给定对象替换选区
- `add(objects)` - 将给定对象加入选区
- `translate(dx: float, dy: float, dz: float)` - 平移当前选区
- `rotate(axis_x: float, axis_y: float, axis_z: float, angle_degrees: float, center_x: float = None, center_y: float = None, center_z: float = None)` - 绕轴旋转选区
- `scale(scale_x: float, scale_y: float, scale_z: float, center_x: float = None, center_y: float = None, center_z: float = None)` - 缩放选区
- `duplicate()` - 复制当前选中的几何体与实体，并将选区转移到新副本上
- `chamfer_vertices(distance: float)` - 对选中 Brush 的顶点执行倒角
- `chamfer_edges(distance: float, segments: int = 1)` - 对选中 Brush 的边执行多段倒角
- `deselect_all()` / `clear()` - 取消选中

#### 实体属性下标
- `sel[key]` / `key in sel` - 从首个相关实体读取或检查属性
- `sel[key] = value` - 等同 `set_property(key, value)`，向所有选中实体写入属性
- `set_property(..., create_if_missing=False)` - 只更新已经包含该属性键的相关实体；没有匹配实体时返回 `False`

---

### `tb2.Entity`
地图实体对象。

#### 属性
- `classname: str` - 实体的 classname（例如 `"light"`, `"worldspawn"`, `"func_door"`）
- `brushes: list[Brush]` - 属于该实体的所有 Brush 几何体（点实体返回空列表）
- `properties: dict[str, str]` - 实体属性的字典快照

#### 方法
- `keys() -> list[str]` - 获取该实体已定义的所有属性键名列表
- `values() -> list[str]` - 获取所有属性值
- `items() -> list[tuple[str, str]]` - 获取所有属性键值对
- `get(key: str, default: Any = None) -> str | Any` - 获取指定属性值
- `set(key: str, value: str)` - 设置或新增指定属性键值对
- `remove(key: str)` - 删除指定属性

---

### `tb2.Brush`
凸多面体 Brush 几何体。

#### 属性
- `entity: Entity` - Brush 所属的父实体

#### 方法
- `faces() -> list[Face]` - 获取构成该 Brush 的所有多边形面列表

---

### `tb2.Face`
Brush 的单个多边形面。

#### 属性
- `vertices: list[Vec3]` - 组成该面的三维顶点坐标列表（只读）
- `uv_loops: list[...]` - UV 纹理坐标循环（只读）
- `material: str` / `texture_name: str` - 材质名称（可读写）
- `offset: tuple[float, float]` - UV 偏移量 `(u_offset, v_offset)`（可读写）
- `scale: tuple[float, float]` - UV 缩放比例 `(u_scale, v_scale)`（可读写）
- `rotation: float` - UV 旋转角度（可读写）
- `surface_contents: int | None` - 表面内容标志（可读写）
- `surface_flags: int | None` - 表面行为标志（可读写）
- `surface_value: float | None` - 表面光照/反射值（可读写）

#### 方法
- `set_material(name: str)` - 分配新材质
- `set_uv_loops(loops: list)` - 写入自定义 UV 循环坐标

---

### `tb2.Material`
已加载的单个材质资源。

#### 属性
- `name: str` - 材质名称/相对路径
- `collection_name: str` - 所属集合名称
- `width: int` - 贴图像素宽度
- `height: int` - 贴图像素高度

---

### `tb2.MaterialCollection`
材质资源集合（如 WAD、PAK、目录集合等）。

#### 属性
- `name: str` - 集合名称
- `path: str` - 集合文件路径
- `material_count: int` - 包含的材质数量
- `materials: list[Material]` - 包含的所有材质列表

---

### `tb2.Transaction`
Undo 事务管理器，用于将多步操作打包为单个撤销步骤。

#### 使用方式
```python
with doc.transaction("My Custom Edit"):
    # 在此执行修改
    pass
```

#### 方法
- `commit()` - 显式提交事务
- `cancel()` - 显式放弃并回滚事务

---

### 句柄生命周期
`Document`、`Entity`、`Brush` 和 `Face` 是指向当前编辑器状态的句柄，不是对象快照。关闭或重新加载文档、删除节点后，相关句柄会失效；改变 Brush 几何也会使此前取得的 `Face` 句柄失效。访问失效句柄会抛出 `RuntimeError`。跨回调或跨几何修改保存对象时，应从 `current_document()`、当前选区或父对象重新获取句柄。

---

### `tb2.PluginPanel`
声明式 UI 控件面板容器。

#### 标签与展示
- `add_label(text: str)` - 添加自动换行文本
- `add_label_named(key: str, text: str)` - 添加带 key 标识的文本
- `set_label_text(key: str, text: str)` - 更新指定 key 标签的文本
- `add_html_view(key: str, html: str, height: int = 200, callback: Callable[[str], None] = None)` - 添加富文本 HTML 视图
- `set_html_view(key: str, html: str)` - 动态更新 HTML 视图内容

#### 表单与输入
- `add_line_edit(text: str, callback: Callable[[str], None])` - 添加无命名键的即时回调文本框（兼容接口）
- `add_text_field(key: str, label: str, value: str = "", placeholder: str = "")`
- `get_text_field(key: str) -> str`
- `set_text_field(key: str, value: str)`
- `add_text_area(key: str, label: str, value: str = "", height: int = 120, placeholder: str = "")`
- `get_text_area(key: str) -> str`
- `set_text_area(key: str, value: str)`
- `add_int_field(key: str, label: str, value: int = 0, min: int = ..., max: int = ...)`
- `get_int_field(key: str) -> int`
- `add_float_field(key: str, label: str, value: float = 0.0, min: float = ..., max: float = ..., decimals: int = 2, step: float = 1.0)`
- `get_float_field(key: str) -> float`
- `add_checkbox(key: str, text: str, checked: bool = False)`
- `get_checkbox(key: str) -> bool`
- `add_combo_box(key: str, label: str, items: list[str], callback: Callable[[str], None] = None, current: int | str = None)`
- `get_combo_box_text(key: str) -> str`
- `add_color_field(key: str, label: str, color: tuple[int, int, int])`
- `get_color_field(key: str) -> tuple[int, int, int]`

#### 按钮
- `add_button(text: str, callback: Callable[[], None])`
- `add_button_callback(text: str, callback: Callable[[], None])` - `add_button` 的兼容别名

#### 表格与树形组件
- `add_table_widget(key: str, columns: list[str], rows: list[list[str]], height: int = 200, callback: Callable[[int, int], None] = None)`
- `set_table_widget_rows(key: str, rows: list[list[str]])`
- `add_tree_widget(key: str, columns: list[str], rows: list[list[str]], height: int = 200, callback: Callable[[int], None] = None)`
- `set_tree_widget_items(key: str, rows: list[list[str]])`

#### 容器与布局
- `add_group(key: str, title: str) -> PluginPanel` - 分组框（垂直布局）
- `add_row(key: str) -> PluginPanel` - 水平行布局
- `add_column(key: str) -> PluginPanel` - 垂直列布局
- `set_widget_visible(key: str, visible: bool)` - 控制控件显隐
- `clear()` - 清空当前面板内所有子元素

---

## 2. 全局模块函数

- `tb2.current_document() -> Document` - 获取当前激活的地图文档（若无打开地图则抛出异常）
- `tb2.document() -> Document` - `current_document()` 的兼容别名
- `tb2.selected_brushes()` / `tb2.selectedBrushes() -> list[Brush]` - 获取直接选中的 Brush
- `tb2.selected_entities(include_brushes=False)` / `tb2.selectedEntities(...) -> list[Entity]` - 获取直接选中的实体；传入 `True` 时还包含选中 Brush 或单独选中面的父实体
- `tb2.selected_all_entities()` / `tb2.selectedAllEntities() -> list[Entity]` - 获取所有相关实体，语义与 `selection().all_entities` 相同
- `tb2.selection() -> Selection` - 获取当前选区句柄
- `tb2.selected_faces()` / `tb2.selectedFaces() -> list[Face]` - 获取单独选中的 Brush 面
- `tb2.translate(...)` / `tb2.rotate(...)` / `tb2.scale(...)` - 变换当前选区，或先将传入对象设为选区后变换
- `tb2.duplicate(target=None)` - 复制当前选区或指定对象
- `tb2.delete_selection()` / `tb2.deleteSelection()` - 删除当前选区
- `tb2.deselect_all()` / `tb2.deselectAll()` - 清空当前选区
- `tb2.create_plugin_panel(title: str) -> PluginPanel` - 在 Plugins 检查器中创建可视化面板
- `tb2.create_brush(points: list[Vec3], material: str = None) -> Brush` - 根据凸包顶点集合生成 Brush
- `tb2.list_actions() -> list[str]` - 列出当前所有已注册的编辑器 Action 标识符
- `tb2.execute_action(action_id: str)` - 触发执行指定 Action
- `tb2.register_callback(event: str, callback: Callable[[], None]) -> int` - 注册 `selection_changed`、`document_loaded` 或 `document_saved` 事件回调并返回 token
- `tb2.unregister_callback(token: int)` - 注销事件回调
- `tb2.set_timeout(callback: Callable[[], None], milliseconds: int) -> int` - 注册单次定时器
- `tb2.set_interval(callback: Callable[[], None], milliseconds: int) -> int` - 注册循环定时器
- `tb2.clear_interval(timer_id: int)` - 取消定时器

事件回调与定时器用于常驻 UI 插件。插件卸载时会自动清理其回调和定时器；`set_timeout`、`set_interval` 和 `clear_interval` 在没有活动插件会话时会抛出 `RuntimeError`。
