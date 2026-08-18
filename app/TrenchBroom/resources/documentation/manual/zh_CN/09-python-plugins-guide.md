# Python 脚本与插件开发 {#python_scripting_and_plugins}

## Python 控制台 {#python_console}

打开底部信息面板中的 **Python** 标签页，以针对当前活动的 TrenchBroom 会话交互式运行 Python v2 命令。

### 交互式执行与多行输入 {#console_execution}

控制台提供了具备双模式编译的交互式 REPL：

- **表达式求值**：单行表达式（如 `tb2.current_document().selection.brushes` 或 `tb2.Vec3(128, 64, 0).length()`）会被即时求值，并自动将格式化后的字符串表示（`repr`）打印至控制台输出区。
- **复合语句块**：包含 `for` 循环、函数定义或 `with doc.transaction("Action"):` 上下文的多行复合代码可直接输入或粘贴到输入框中。输入框高度会随代码行数自动自适应扩展（1 至 4 行）。

### 快捷键与历史记录 {#console_shortcuts_and_history}

- **执行指令**：按 #key(Ctrl)+#key(Return) 或点击 **Run** 按钮即可立即执行命令。
- **历史记录翻阅**：在输入框第一行或最后一行使用 #key(Up) 和 #key(Down) 可翻阅最多 100 条历史指令。若当前输入框中有未提交的代码，翻阅历史时会自动暂存为草稿，并在按 #key(Down) 翻回底部时完整恢复。
- **清除输出**：点击 **Clear** 按钮可一键清空控制台的所有历史输出。
- **外观字体设置**：可在 **Preferences > View > Fonts > Python Console** 下自定义控制台的等宽字体系列和字号大小；列表中仅列出已安装的等宽字体系列，**System Monospace** 则使用平台默认字体。

### 输出重定向与错误追踪 {#console_output_and_errors}

Python 内置 `print(...)` 函数的所有标准输出均会自动捕获并流式输出至控制台中。当发生未捕获的异常或语法错误时，控制台会自动以红色高亮打印带有源文件行号的完整 Traceback 调用栈信息。

### 实用控制台操作示例 {#console_examples}

#### 检查选区与打印 Brush 详细信息 {#example_inspect_selection}

```python
doc = tb2.current_document()
brushes = doc.selection.brushes
print(f"Selected {len(brushes)} brush(es):")
for i, brush in enumerate(brushes):
    faces = brush.faces()
    print(f"  Brush #{i}: {len(faces)} faces")
    for face in faces:
        print(f"    - Material: {face.material}, Vertices: {len(face.vertices)}")
```

#### 复制选区并平移移动 Brush {#example_duplicate_and_translate}

```python
doc = tb2.current_document()
with doc.transaction("Duplicate Selection"):
    doc.selection.duplicate()
    doc.selection.translate(0, 0, 64)
```

#### 循环生成阶梯阵列 {#example_step_array}

```python
doc = tb2.current_document()
with doc.transaction("Generate Step Array"):
    for _ in range(8):
        doc.selection.duplicate()
        doc.selection.translate(64, 0, 16)
```

#### 批量赋予表面材质 {#example_batch_face_materials}

```python
doc = tb2.current_document()
with doc.transaction("Apply Face Material"):
    for face in doc.selection.brush_faces:
        face.set_material("common/caulk")
```

#### 批量规范化实体属性 {#example_batch_entity_properties}

```python
doc = tb2.current_document()
with doc.transaction("Batch Align Entities"):
    for ent in doc.entities:
        if ent.classname == "light" and not ent.get("light"):
            ent.set("light", "300")
```

## Python 插件 {#python_plugins}

TrenchBroom 内置了嵌入式 Python v2 运行时与扩展系统。打开 **Preferences > Misc > Python Plugin Manager** 可管理基于清单的 UI 插件。管理器会列出配置的插件目录、检测到的插件、加载状态、元数据和错误。使用搜索或 **Only show issues** 可以诊断规模较大的插件集，在修改文件后点击刷新即可。

#### 插件类型与生命周期 {#plugin_types_and_lifecycle}

TrenchBroom 区分两种插件类型：

- **UI 插件 (`pluginType: "ui"`)**：声明了 `trenchbroom-plugin.json` 清单的常驻插件。它们在启动或刷新插件管理器时加载，并可在 **Plugins** 检查器标签页中注册自定义面板、全局动作、事件监听器和定时器。
- **脚本插件 (`pluginType: "script"`)**：通过 [Python 控制台](#python_console) 或自定义动作按需执行的独立脚本。旧版 `tb` 兼容性已不再属于活动插件路径的一部分；现有脚本应使用 `tb2` 或 `import tb2 as tb`。

#### 插件清单格式 {#plugin_manifest_format}

每个 UI 插件目录根目录下必须包含一个 `trenchbroom-plugin.json` 清单文件：

```json
{
  "id": "com.example.my_plugin",
  "name": "My Custom Plugin",
  "version": "1.0.0",
  "apiVersion": 2,
  "pluginType": "ui",
  "entry": "main.py",
  "description": "A sample plugin with a custom inspector panel.",
  "author": "Author Name"
}
```

清单字段定义如下：

| 字段 | 类型 | 描述 |
| :--- | :--- | :--- |
| `id` | string | 插件全局唯一标识符 |
| `name` | string | 界面上展示的插件名称 |
| `version` | string | 语义化版本号 |
| `apiVersion` | integer | Python v2 必须固定为 `2` |
| `pluginType` | string | `"ui"` 为常驻 UI 插件，`"script"` 为脚本 |
| `entry` | string | Python 入口脚本的相对路径 |
| `description` | string | 插件功能可选描述 |
| `author` | string | 可选作者名称或信息 |

#### tb2 Python API {#the_tb2_python_api}

所有 Python 脚本和插件均通过内置的 `tb2` 模块（`import tb2`）访问 TrenchBroom。核心组件包括：

- `tb2.current_document()`：返回代表当前打开地图的活动 `Document` 句柄。
- `doc.transaction(name)`：事务上下文管理器（`with doc.transaction("Action Name"):`），将修改合并为一个撤销/重做步骤，并在发生 Python 异常时自动回滚。
- `doc.selection`：用于查询选中对象（`brushes`、`entities`、`brush_faces`）并应用几何变换（`translate`、`rotate`、`scale`、`duplicate`、`chamfer_vertices`、`chamfer_edges`）的 `Selection` 句柄。
- `doc.entities`：地图中所有 `Entity` 对象的列表。使用 `.get(key, default)` 和 `.set(key, value)` 访问属性。
- `brush.faces()`：返回构成 Brush 的多边形 `Face` 对象列表，支持访问 `.material`、`.offset`、`.scale`、`.rotation` 和 `.vertices`。
- `tb2.Vec3(x, y, z)` 与 `tb2.Plane(normal, dist)`：三维向量与平面数学基元。

#### 构建自定义 UI 面板 {#building_custom_ui_panels}

UI 插件使用 `tb2.create_plugin_panel(panel_id, title)` 在 **Plugins** 检查器标签页中创建交互式面板。返回的 `PluginPanel` 提供声明式控件：

- **标签与文本**：`.add_label(text)`、`.add_label_named(key, text)`、`.set_label_text(key, text)` 以及 `.add_html_view(key, html, height, callback)`。
- **表单输入**：`.add_text_field(key, label, value)`、`.add_text_area(key, label, value)`、`.add_int_field(key, label, value, min, max)`、`.add_float_field(key, label, value, min, max, decimals, step)`、`.add_checkbox(key, text, checked)`、`.add_combo_box(key, label, items, callback, current)` 以及 `.add_color_field(key, label, color)`。
- **按钮**：`.add_button(text, callback)`。
- **数据视图**：`.add_table_widget(key, columns, rows, height, callback)` 以及 `.add_tree_widget(key, columns, rows, height, callback)`。
- **布局容器**：`.add_group(key, title)`、`.add_row(key)` 以及 `.add_column(key)`。

#### 完整插件示例 {#plugin_example}

以下是一个完整可运行的 UI 插件脚本，用于复制当前选区并沿偏移向量阵列平移：

```python
import tb2

panel = None

def on_generate():
    doc = tb2.current_document()
    if not doc.selection.brushes and not doc.selection.entities:
        panel.set_label_text("status", "Please select at least one brush or entity.")
        return

    count = panel.get_int_field("count")
    dx = panel.get_float_field("dx")
    dy = panel.get_float_field("dy")
    dz = panel.get_float_field("dz")

    with doc.transaction(f"Linear Array ({count} copies)"):
        for _ in range(count):
            doc.selection.duplicate()
            doc.selection.translate(dx, dy, dz)

    panel.set_label_text("status", f"Successfully created {count} copies.")

def init_plugin():
    global panel
    panel = tb2.create_plugin_panel("com.example.array_tool", "Array Generator")
    panel.add_label("Duplicate the active selection along an offset vector:")

    group = panel.add_group("config", "Parameters")
    group.add_int_field("count", "Copies", value=3, min=1, max=50)
    group.add_float_field("dx", "Step X", value=64.0, min=-2048.0, max=2048.0, step=8.0)
    group.add_float_field("dy", "Step Y", value=0.0, min=-2048.0, max=2048.0, step=8.0)
    group.add_float_field("dz", "Step Z", value=0.0, min=-2048.0, max=2048.0, step=8.0)

    panel.add_button("Generate Array", on_generate)
    panel.add_label_named("status", "Ready")

init_plugin()
```
