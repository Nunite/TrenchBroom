#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Markdown标题ID标签修复脚本

这个脚本用于修复已翻译的Markdown文件中的标题ID标签，确保所有标题都有正确的ID标签。
主要用于修复之前翻译过程中可能丢失的ID标签。

使用方法:
    python fix_heading_ids.py 原始文件.md 翻译文件.md 输出文件.md

如不指定输出文件，将直接修改翻译文件。
"""

import sys
import re
from pathlib import Path
from difflib import SequenceMatcher


def extract_headings(text):
    """从文本中提取所有标题及其ID标签"""
    headings = []
    for match in re.finditer(
        r"^(#{1,6})\s+(.+?)(\s*\{#([^}]+)\})?\s*$", text, re.MULTILINE
    ):
        heading_level = len(match.group(1))
        heading_text = match.group(2).strip()
        heading_id = match.group(4) if match.group(3) else None
        original_heading = match.group(0)
        start_pos = match.start()

        headings.append(
            {
                "level": heading_level,
                "text": heading_text,
                "id": heading_id,
                "original": original_heading,
                "position": start_pos,
            }
        )

    return headings


def similar(a, b):
    """计算两个字符串的相似度"""
    return SequenceMatcher(None, a, b).ratio()


def match_headings(original_headings, translated_headings):
    """匹配原始标题和翻译标题"""
    matches = []

    # 首先按照顺序匹配相同级别的标题
    for i, trans_heading in enumerate(translated_headings):
        best_match = None
        best_score = 0
        best_index = -1

        level = trans_heading["level"]

        # 找出同一级别、带有ID的原始标题
        for j, orig_heading in enumerate(original_headings):
            if orig_heading["level"] == level and orig_heading["id"]:
                # 考虑标题在文档中的相对位置
                position_factor = 1 - abs(
                    i / len(translated_headings) - j / len(original_headings)
                )

                # 匹配分数 = 位置因子
                match_score = position_factor

                if match_score > best_score:
                    best_match = orig_heading
                    best_score = match_score
                    best_index = j

        if best_match and best_score > 0.5:
            matches.append((trans_heading, best_match))

    return matches


def fix_heading_ids(original_text, translated_text):
    """修复翻译文本中的标题ID标签"""
    original_headings = extract_headings(original_text)
    translated_headings = extract_headings(translated_text)

    # 匹配标题
    matched_headings = match_headings(original_headings, translated_headings)

    # 创建修复后的文本
    fixed_text = translated_text

    # 记录所有需要替换的内容
    replacements = []

    for trans_heading, orig_heading in matched_headings:
        # 只处理没有ID标签的翻译标题
        if not trans_heading["id"] and orig_heading["id"]:
            old_heading = trans_heading["original"]
            new_heading = f"{trans_heading['level']*'#'} {trans_heading['text']} {{#{orig_heading['id']}}}"
            replacements.append((old_heading, new_heading))

    # 执行所有替换
    for old, new in replacements:
        fixed_text = fixed_text.replace(old, new)

    return fixed_text, len(replacements)


def fix_id_references(original_text, translated_text):
    """修复翻译文本中的ID引用链接"""
    # 提取原文中的ID引用 [链接文本](#id)
    id_references = {}
    for match in re.finditer(r"\[([^\]]+)\]\(#([^)]+)\)", original_text):
        link_text = match.group(1)
        link_id = match.group(2)
        id_references[link_id] = link_text

    # 在翻译文本中找到缺少ID引用的链接
    replacements = []
    for match in re.finditer(r"\[([^\]]+)\](?!\()", translated_text):
        link_text = match.group(1)
        link_pos = match.end()
        next_char = (
            translated_text[link_pos : link_pos + 1]
            if link_pos < len(translated_text)
            else ""
        )

        # 如果链接后面不是引用格式
        if next_char != "(":
            # 尝试在原文ID引用中匹配
            for link_id, orig_link_text in id_references.items():
                if similar(link_text, orig_link_text) > 0.6:
                    old_text = f"[{link_text}]"
                    new_text = f"[{link_text}](#{link_id})"
                    replacements.append((old_text, new_text))
                    break

    # 执行所有替换
    fixed_text = translated_text
    for old, new in replacements:
        fixed_text = fixed_text.replace(old, new)

    return fixed_text, len(replacements)


def main():
    """主函数"""
    if len(sys.argv) < 3:
        print(
            "使用方法: python fix_heading_ids.py 原始文件.md 翻译文件.md [输出文件.md]"
        )
        return 1

    original_file = sys.argv[1]
    translated_file = sys.argv[2]
    output_file = sys.argv[3] if len(sys.argv) > 3 else translated_file

    # 读取原始文件
    try:
        with open(original_file, "r", encoding="utf-8") as f:
            original_text = f.read()
    except Exception as e:
        print(f"读取原始文件时出错: {e}")
        return 1

    # 读取翻译文件
    try:
        with open(translated_file, "r", encoding="utf-8") as f:
            translated_text = f.read()
    except Exception as e:
        print(f"读取翻译文件时出错: {e}")
        return 1

    # 修复标题ID标签
    fixed_text, headings_fixed = fix_heading_ids(original_text, translated_text)

    # 修复ID引用链接
    fixed_text, references_fixed = fix_id_references(original_text, fixed_text)

    # 如果没有变化，提示用户
    if fixed_text == translated_text:
        print("未发现需要修复的标题ID标签或引用链接")
        return 0

    print(f"共修复 {headings_fixed} 个标题ID标签")
    print(f"共修复 {references_fixed} 个引用链接")

    # 保存修复后的文件
    try:
        with open(output_file, "w", encoding="utf-8") as f:
            f.write(fixed_text)
        print(f"已成功修复标题ID标签并保存到 {output_file}")
        return 0
    except Exception as e:
        print(f"保存修复后的文件时出错: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
