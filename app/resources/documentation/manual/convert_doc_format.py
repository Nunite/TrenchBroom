#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
文档格式标记转换脚本

这个脚本用于将Markdown文档中的特殊格式标记转换为更易读的形式：
1. #menu(菜单/视图/隐藏) -> `菜单->视图->隐藏`
2. #action(Controls/Camera/Move forward) -> [前进]
3. #key(Alt) -> [Alt]

使用方法:
    python convert_doc_format.py 输入文件.md 输出文件.md

如不指定输出文件，将直接修改输入文件。
"""

import sys
import re
from pathlib import Path


def convert_menu_format(text):
    """
    将 #menu(菜单/视图/隐藏) 格式转换为 `菜单->视图->隐藏` 格式
    """
    # 定义匹配模式
    menu_pattern = r"#menu\(([^\)]+)\)"

    # 查找所有匹配项
    def replace_menu(match):
        menu_path = match.group(1)
        # 将路径分隔符"/"替换为"->"，并添加反引号标识
        readable_format = menu_path.replace("/", "->")
        return f"`{readable_format}`"

    # 应用替换
    converted_text = re.sub(menu_pattern, replace_menu, text)

    # 返回转换后的文本和替换次数
    replacements_count = text.count("#menu(") - converted_text.count("#menu(")
    return converted_text, replacements_count


def convert_action_format(text):
    """
    将 #action(Controls/Camera/Move forward) 格式转换为 [前进] 格式

    这里需要一个映射表来将动作名称转换为对应的中文
    简单起见，这里只提供几个常见动作的映射
    实际使用时可能需要完善这个映射表
    """
    # 动作名称映射
    action_map = {
        "Controls/Camera/Move forward": "前进",
        "Controls/Camera/Move backward": "后退",
        "Controls/Camera/Move left": "左移",
        "Controls/Camera/Move right": "右移",
        "Controls/Camera/Move up": "上移",
        "Controls/Camera/Move down": "下移",
        "Controls/Map view/Cycle map view": "循环地图视图",
        # 可以根据需要添加更多映射
    }

    # 定义匹配模式
    action_pattern = r"#action\(([^\)]+)\)"

    # 查找所有匹配项
    def replace_action(match):
        action_name = match.group(1)
        # 尝试查找中文映射，如果没有则保留原名
        chinese_name = action_map.get(action_name, action_name)
        return f"[{chinese_name}]"

    # 应用替换
    converted_text = re.sub(action_pattern, replace_action, text)

    # 返回转换后的文本和替换次数
    replacements_count = text.count("#action(") - converted_text.count("#action(")
    return converted_text, replacements_count


def convert_key_format(text):
    """
    将 #key(Alt) 格式转换为 [Alt] 格式
    """
    # 定义匹配模式
    key_pattern = r"#key\(([^\)]+)\)"

    # 查找所有匹配项
    def replace_key(match):
        key_name = match.group(1)
        return f"[{key_name}]"

    # 应用替换
    converted_text = re.sub(key_pattern, replace_key, text)

    # 返回转换后的文本和替换次数
    replacements_count = text.count("#key(") - converted_text.count("#key(")
    return converted_text, replacements_count


def convert_doc_format(text):
    """
    应用所有格式转换
    """
    total_replacements = 0

    # 应用菜单格式转换
    text, count = convert_menu_format(text)
    total_replacements += count

    # 应用动作格式转换
    text, count = convert_action_format(text)
    total_replacements += count

    # 应用按键格式转换
    text, count = convert_key_format(text)
    total_replacements += count

    return text, total_replacements


def main():
    """主函数"""
    if len(sys.argv) < 2:
        print("使用方法: python convert_doc_format.py 输入文件.md [输出文件.md]")
        return 1

    input_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else input_file

    # 读取输入文件
    try:
        with open(input_file, "r", encoding="utf-8") as f:
            content = f.read()
    except Exception as e:
        print(f"读取文件时出错: {e}")
        return 1

    # 转换文档格式
    converted_content, count = convert_doc_format(content)

    # 如果没有变化，提示用户
    if converted_content == content:
        print("未发现需要转换的格式标记")
        return 0

    print(f"共转换 {count} 处格式标记")

    # 保存转换后的文件
    try:
        with open(output_file, "w", encoding="utf-8") as f:
            f.write(converted_content)
        print(f"已成功转换格式标记并保存到 {output_file}")
        return 0
    except Exception as e:
        print(f"保存文件时出错: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
