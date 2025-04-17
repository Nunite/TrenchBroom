#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
菜单格式转换脚本

这个脚本用于将Markdown文档中的菜单引用格式从 #menu(菜单/视图/隐藏) 转换为 `菜单->视图->隐藏` 格式，
使文档更易于阅读，并添加反引号使其在文档中更易于识别。

使用方法:
    python convert_menu_format.py 输入文件.md 输出文件.md

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


def main():
    """主函数"""
    if len(sys.argv) < 2:
        print("使用方法: python convert_menu_format.py 输入文件.md [输出文件.md]")
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

    # 转换菜单格式
    converted_content, count = convert_menu_format(content)

    # 如果没有变化，提示用户
    if converted_content == content:
        print("未发现需要转换的菜单格式")
        return 0

    print(f"共转换 {count} 处菜单格式引用")

    # 保存转换后的文件
    try:
        with open(output_file, "w", encoding="utf-8") as f:
            f.write(converted_content)
        print(f"已成功转换菜单格式并保存到 {output_file}")
        return 0
    except Exception as e:
        print(f"保存文件时出错: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
