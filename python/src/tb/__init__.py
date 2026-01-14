"""
TrenchBroom 内嵌 Python 模块（实验性）。

模块名：tb

用途：
- 执行现有菜单/快捷键 Action：execute_action
- 枚举所有 Action 路径：list_actions
- 通过最小对象模型读取/修改当前选择：Document / Selection / Entity

使用方式（在 TrenchBroom 内）：
- 打开一个 map 窗口
- Run → Run Python Script...
- 选择一个 .py 文件执行

输出位置：
- View → Toggle Info Panel → Python Console
"""

from __future__ import annotations

from typing import Any, Callable, Protocol


class _LogWriter(Protocol):
    """
    内部使用：用于把 sys.stdout / sys.stderr 重定向到 TrenchBroom 日志。

    在 TrenchBroom 中运行脚本时，print(...) 等输出会通过该对象写入 Python Console。
    """

    def write(self, value: Any) -> int:
        """写入一段文本到日志；返回写入的字符数（按 str(value) 计算）。"""
        ...

    def flush(self) -> None:
        """刷新输出（当前为 no-op）。"""
        ...

    def isatty(self) -> bool:
        """始终返回 False。"""
        ...


class PluginPanel(Protocol):
    """Inspector 的 Plugin 页签里的一块可折叠面板内容区。"""

    def clear(self) -> None:
        """清空面板中已有的所有控件。"""
        ...

    def add_label(self, text: str) -> None:
        """在面板末尾追加一个自动换行的文本标签。"""
        ...

    def set_text(self, text: str) -> None:
        """清空面板并设置为一段纯文本内容。"""
        ...

    def set_html(self, html: str) -> None:
        """清空面板并设置为一段 HTML（富文本）内容。"""
        ...

    def add_button(self, text: str, action_path: str | None = None) -> None:
        """
        添加一个按钮。

        - action_path 不为 None 时：点击按钮会尝试执行对应 action（若不存在/禁用则忽略）
        - action_path 为 None 时：仅创建按钮，不绑定行为
        """
        ...

    def add_button_callback(self, text: str, callback: Callable[[], Any]) -> None:
        """
        添加一个按钮，并在点击时调用 callback()。

        callback 抛异常时会打印到 Python Console，同时记录一条错误日志。
        """
        ...

    def add_label_named(self, key: str, text: str) -> None:
        """添加一个带 key 的文本标签，便于之后通过 set_label_text 更新。"""
        ...

    def set_label_text(self, key: str, text: str) -> bool:
        """更新由 add_label_named 创建的标签文本；找不到 key 则返回 False。"""
        ...

    def add_int_field(self, key: str, label: str, value: int, min: int = 0, max: int = 999999) -> None:
        """添加一个整数字段（QSpinBox），可用 get_int_field 读取当前值。"""
        ...

    def add_float_field(
        self,
        key: str,
        label: str,
        value: float,
        min: float = -1e9,
        max: float = 1e9,
        decimals: int = 3,
        step: float = 1.0,
    ) -> None:
        """添加一个浮点字段（QDoubleSpinBox），可用 get_float_field 读取当前值。"""
        ...

    def get_int_field(self, key: str) -> int:
        """读取整数字段当前值；key 不存在会抛 KeyError。"""
        ...

    def get_float_field(self, key: str) -> float:
        """读取浮点字段当前值；key 不存在会抛 KeyError。"""
        ...


class Transaction(Protocol):
    """用于把一段脚本编辑合并成一次 undo/redo 的事务。"""

    def commit(self) -> bool:
        """
        提交事务并结束。

        返回值表示提交是否成功。
        """
        ...

    def cancel(self) -> None:
        """取消事务并结束（撤销本事务期间的修改）。"""
        ...

    def rollback(self) -> None:
        """回滚事务（事务需已开始）；事务对象仍保持“已开始”状态。"""
        ...

    def __enter__(self) -> Transaction:
        """开始事务并返回自身（用于 with）。"""
        ...

    def __exit__(self, exc_type: type[BaseException] | None, exc: BaseException | None, tb: Any) -> bool | None:
        """
        with 块正常退出时提交，异常退出时取消。

        返回 False（不吞掉异常）。
        """
        ...


class Entity(Protocol):
    """实体节点（worldspawn 或普通实体）。写入请走 Selection 的 undoable API。"""

    def classname(self) -> str:
        """返回实体的 classname。"""
        ...

    def keys(self) -> list[str]:
        """返回该实体当前所有 property 的 key 列表。"""
        ...

    def get(self, key: str, default: Any = None) -> Any:
        """读取 property；不存在则返回 default。"""
        ...


class Selection(Protocol):
    """
    当前选择（Nodes + Face selection 的抽象）。

    - entities()：显式选中的 Entity 节点
    - all_entities()：命令实际会作用到的实体集合（更常用）
    """

    def entities(self) -> list[Entity]:
        """返回显式选中的实体节点列表。"""
        ...

    def all_entities(self) -> list[Entity]:
        """返回“会受操作影响”的实体集合（会包含隐式关联到选择的实体）。"""
        ...

    def __call__(self) -> Selection:
        """不接受任何参数；返回自身。"""
        ...

    def set_property(self, key: str, value: str, default_to_protected: bool = False) -> bool:
        """
        为当前选择相关实体设置 property（可 undo）。

        返回是否有实际修改发生。
        """
        ...

    def remove_property(self, key: str) -> bool:
        """从当前选择相关实体移除 property（可 undo）；返回是否有实际修改发生。"""
        ...

    def rename_property(self, old_key: str, new_key: str) -> bool:
        """重命名当前选择相关实体的 property（可 undo）；返回是否有实际修改发生。"""
        ...

    def clear(self) -> None:
        """清空选择（deselect all）。"""
        ...

    def duplicate(self) -> None:
        """复制当前选中的节点。"""
        ...

    def translate(self, x: float, y: float, z: float) -> bool:
        """平移当前选择；返回是否执行成功。"""
        ...

    def rotate(
        self,
        axis_x: float,
        axis_y: float,
        axis_z: float,
        angle_degrees: float,
        center_x: float | None = None,
        center_y: float | None = None,
        center_z: float | None = None,
    ) -> bool:
        """
        围绕给定轴（axis_*）旋转当前选择。

        - angle_degrees：角度（度）
        - center_*：旋转中心；不提供时使用选择包围盒中心（若不可用会抛 RuntimeError）
        """
        ...

    def brush_vertices(self) -> list[list[tuple[float, float, float]]]:
        """
        返回当前选择相关 brush 的顶点坐标。

        返回值为二维列表：每个 brush 对应一个顶点列表。
        """
        ...

    def chamfer_vertices(self, distance: float) -> bool:
        """
        对当前“顶点工具”选中的顶点执行倒角（chamfer）。

        - distance：沿每条 incident edge 向外偏移的距离
        - 返回值：是否执行成功
        """
        ...

    def chamfer_edges(self, distance: float, segments: int = 1) -> bool:
        """
        对当前“边工具”选中的边执行倒角（chamfer）。

        - distance：沿相邻边向内偏移的距离
        - segments：倒角段数（>= 1，默认 1）
        - 返回值：是否执行成功
        """
        ...


class Document(Protocol):
    """当前活动的 map 文档对象。"""

    @classmethod
    def current(cls) -> Document | None:
        """返回当前活动的 map 文档；如果没有活动 map 窗口则返回 None。"""
        ...

    @property
    def selection(self) -> Selection:
        """返回当前选择对象。"""
        ...

    def get_selection(self) -> Selection:
        """返回当前选择对象（与 selection 属性相同）。"""
        ...

    def transaction(self, name: str = "Python Script") -> Transaction:
        """创建一个事务对象（可用于 with），作用于此文档的 undo 栈。"""
        ...

    def vertex_tool_vertices(self) -> list[tuple[float, float, float]]:
        """返回顶点工具里选中的顶点坐标列表；未选中则为空列表。"""
        ...


def current_document() -> Document | None:
    """返回当前活动的 map 文档；如果没有活动 map 窗口则返回 None。"""
    raise RuntimeError('Module "tb" is only available inside TrenchBroom.')


def document() -> Document:
    """返回当前活动的 map 文档；如果没有活动 map 窗口会抛 RuntimeError。"""
    raise RuntimeError('Module "tb" is only available inside TrenchBroom.')


def transaction(name: str = "Python Script") -> Transaction:
    """创建一个事务对象（可用于 with），作用于当前活动文档的 undo 栈。"""
    raise RuntimeError('Module "tb" is only available inside TrenchBroom.')


def execute_action(path: str) -> None:
    """
    按 action 路径执行一个 TrenchBroom 动作（与菜单/快捷键相同）。

    示例：
    - "Menu/Edit/Undo"
    - "Menu/Run/Compile..."
    - "Menu/File/Preferences..."

    可能抛出：
    - RuntimeError：没有活动 MapFrame，或 action 当前被禁用
    - KeyError：找不到该 action 路径
    """
    raise RuntimeError('Module "tb" is only available inside TrenchBroom.')


def list_actions() -> list[str]:
    """返回所有已注册的 action 路径列表。"""
    raise RuntimeError('Module "tb" is only available inside TrenchBroom.')


def add_plugin_panel(title: str, content: str | None = None) -> None:
    """
    在 Inspector 的 Plugin 页签里添加一个面板，并显示文本内容。

    没有活动 MapFrame 时会抛 RuntimeError。
    """
    raise RuntimeError('Module "tb" is only available inside TrenchBroom.')


def create_plugin_panel(title: str) -> PluginPanel:
    """
    创建一个插件面板并返回 PluginPanel 对象，以便进一步自定义。

    没有活动 MapFrame 时会抛 RuntimeError。
    """
    raise RuntimeError('Module "tb" is only available inside TrenchBroom.')


__all__ = [
    "Document",
    "Entity",
    "PluginPanel",
    "Selection",
    "Transaction",
    "_LogWriter",
    "add_plugin_panel",
    "create_plugin_panel",
    "current_document",
    "document",
    "execute_action",
    "list_actions",
    "transaction",
]
