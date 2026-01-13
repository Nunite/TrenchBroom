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


class PluginPanel(Protocol):
    """Inspector 的 Plugin 页签里的一块可折叠面板内容区。"""

    def clear(self) -> None: ...
    def add_label(self, text: str) -> None: ...
    def set_text(self, text: str) -> None: ...
    def set_html(self, html: str) -> None: ...
    def add_button(self, text: str, action_path: str | None = None) -> None: ...
    def add_button_callback(self, text: str, callback: Callable[[], Any]) -> None: ...
    def add_label_named(self, key: str, text: str) -> None: ...
    def set_label_text(self, key: str, text: str) -> bool: ...
    def add_int_field(self, key: str, label: str, value: int, min: int = 0, max: int = 999999) -> None: ...
    def add_float_field(
        self,
        key: str,
        label: str,
        value: float,
        min: float = -1e9,
        max: float = 1e9,
        decimals: int = 3,
        step: float = 1.0,
    ) -> None: ...
    def get_int_field(self, key: str) -> int: ...
    def get_float_field(self, key: str) -> float: ...


class Transaction(Protocol):
    """用于把一段脚本编辑合并成一次 undo/redo 的事务。"""

    def commit(self) -> bool: ...
    def cancel(self) -> None: ...
    def rollback(self) -> None: ...
    def __enter__(self) -> Transaction: ...
    def __exit__(self, exc_type: type[BaseException] | None, exc: BaseException | None, tb: Any) -> bool | None: ...


class Entity(Protocol):
    """实体节点（worldspawn 或普通实体）。写入请走 Selection 的 undoable API。"""

    @property
    def classname(self) -> str: ...
    def keys(self) -> list[str]: ...
    def get(self, key: str, default: Any = None) -> Any: ...


class Selection(Protocol):
    """
    当前选择（Nodes + Face selection 的抽象）。

    - entities：显式选中的 Entity 节点
    - all_entities：命令实际会作用到的实体集合（更常用）
    """

    @property
    def entities(self) -> list[Entity]: ...
    @property
    def all_entities(self) -> list[Entity]: ...
    def set_property(self, key: str, value: str, default_to_protected: bool = False) -> bool: ...
    def remove_property(self, key: str) -> bool: ...
    def rename_property(self, old_key: str, new_key: str) -> bool: ...
    def clear(self) -> None: ...
    def duplicate(self) -> None: ...
    def translate(self, x: float, y: float, z: float) -> bool: ...
    def rotate(
        self,
        axis_x: float,
        axis_y: float,
        axis_z: float,
        angle_degrees: float,
        center_x: float | None = None,
        center_y: float | None = None,
        center_z: float | None = None,
    ) -> bool: ...
    def brush_vertices(self) -> list[list[tuple[float, float, float]]]: ...


class Document(Protocol):
    """当前活动的 map 文档对象。"""

    @classmethod
    def current(cls) -> Document | None: ...

    @property
    def selection(self) -> Selection: ...

    def get_selection(self) -> Selection: ...
    def transaction(self, name: str = "Python Script") -> Transaction: ...
    def vertex_tool_vertices(self) -> list[tuple[float, float, float]]: ...


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
    """
    raise RuntimeError('Module "tb" is only available inside TrenchBroom.')


def list_actions() -> list[str]:
    """返回所有已注册的 action 路径列表。"""
    raise RuntimeError('Module "tb" is only available inside TrenchBroom.')


def add_plugin_panel(title: str, content: str | None = None) -> None:
    """在 Inspector 的 Plugin 页签里添加一个面板，并显示文本内容。"""
    raise RuntimeError('Module "tb" is only available inside TrenchBroom.')


def create_plugin_panel(title: str) -> PluginPanel:
    """创建一个插件面板并返回 PluginPanel 对象，以便进一步自定义。"""
    raise RuntimeError('Module "tb" is only available inside TrenchBroom.')


__all__ = [
    "Document",
    "Entity",
    "PluginPanel",
    "Selection",
    "Transaction",
    "add_plugin_panel",
    "create_plugin_panel",
    "current_document",
    "document",
    "execute_action",
    "list_actions",
    "transaction",
]
