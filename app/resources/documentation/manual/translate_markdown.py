#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Markdown翻译脚本

这个脚本用于将Markdown文件从一种语言翻译成另一种语言，同时保留Markdown格式。
默认从英文翻译成中文，但可以通过命令行参数修改。

使用方法:
    python translate_markdown.py input.md output.md [--from_lang en] [--to_lang zh]

依赖:
    - requests
    - re
"""

import argparse
import re
import time
import os
from pathlib import Path
import requests
import sys

# AI大模型API配置
AI_API_URL = "https://api.siliconflow.cn/v1/chat/completions"
AI_API_KEY = "sk-hlgmghidkyerkaqjeonawwlhpsvizkfsspntjywqkzqehhkp"  # 替换为您的API密钥
AI_MODEL = "deepseek-ai/DeepSeek-V3"

# 配置翻译API（百度翻译API作为备用）
# 您需要在百度翻译开放平台申请APP ID和密钥
# https://fanyi-api.baidu.com/
BAIDU_APP_ID = "YOUR_APP_ID"
BAIDU_SECRET_KEY = "YOUR_SECRET_KEY"

# 不翻译的Markdown元素
NON_TRANSLATABLE_PATTERNS = [
    r"(`{1,3}.*?`{1,3})",  # Inline code
    r"(!\[.*?\]\(.*?\))",  # Images
    r"(\[.*?\]\(.*?\))",  # Links (just the URL part)
    r"(#[a-zA-Z0-9_-]+)",  # Anchors/IDs
    r"(\{\{.*?\}\})",  # Template variables
    r"(\$.*?\$)",  # Math expressions
    r"(\\.*?\{.*?\})",  # LaTeX commands
    r"(<!--.*?-->)",  # HTML comments
    r"(^```[\s\S]*?^```)",  # Code blocks
    r"(\{\#.*?\})",  # IDs
    r"(%.*?%)",  # Pandoc variables
]


def parse_arguments():
    """解析命令行参数"""
    parser = argparse.ArgumentParser(
        description="将Markdown文件从一种语言翻译成另一种语言"
    )
    parser.add_argument("input_file", help="输入Markdown文件路径")
    parser.add_argument("output_file", help="输出Markdown文件路径")
    parser.add_argument("--from_lang", default="en", help="源语言代码 (默认: en)")
    parser.add_argument("--to_lang", default="zh", help="目标语言代码 (默认: zh)")
    parser.add_argument(
        "--use_baidu", action="store_true", help="使用百度API替代AI大模型"
    )
    return parser.parse_args()


def extract_frontmatter(content):
    """提取YAML前置元数据"""
    frontmatter_match = re.match(r"^---\n(.*?)\n---\n", content, re.DOTALL)
    if frontmatter_match:
        frontmatter = frontmatter_match.group(1)
        content = content[frontmatter_match.end() :]
        return frontmatter, content
    return None, content


def extract_non_translatable(text, patterns=NON_TRANSLATABLE_PATTERNS):
    """提取不需要翻译的部分，并用占位符替换"""
    placeholders = {}
    counter = 0

    for pattern in patterns:
        matches = re.finditer(pattern, text, re.DOTALL | re.MULTILINE)
        for match in matches:
            placeholder = f"__PLACEHOLDER_{counter}__"
            placeholders[placeholder] = match.group(0)
            text = text.replace(match.group(0), placeholder)
            counter += 1

    return text, placeholders


def restore_non_translatable(text, placeholders):
    """将占位符还原为原始内容"""
    for placeholder, original in placeholders.items():
        text = text.replace(placeholder, original)
    return text


def translate_text_with_ai(text, from_lang="en", to_lang="zh"):
    """使用AI大模型翻译文本"""
    if not text.strip():
        return ""

    prompt = f"""你是一个专业的翻译师，当前项目是一个三维编辑器软件的使用手册翻译，你需要根据三维软件常见的上下文去翻译，并确保保留所有的格式和标记(比如说:专有名词"Brush","map"等不翻译)。
请将以下markdown格式文本从{from_lang}翻译成{to_lang}，只需要返回翻译结果，不要有任何解释：

{text}"""

    payload = {
        "model": AI_MODEL,
        "stream": False,
        "max_tokens": 4000,
        "temperature": 0.7,
        "top_p": 0.7,
        "top_k": 50,
        "frequency_penalty": 0.5,
        "n": 1,
        "messages": [{"role": "user", "content": prompt}],
        "stop": [],
    }

    headers = {
        "Authorization": f"Bearer {AI_API_KEY}",
        "Content-Type": "application/json",
    }

    try:
        response = requests.post(AI_API_URL, json=payload, headers=headers)
        result = response.json()

        if "choices" in result and len(result["choices"]) > 0:
            return result["choices"][0]["message"]["content"].strip()
        else:
            print(f"AI翻译错误: {result}")
            # 如果AI翻译失败，回退到百度翻译
            if BAIDU_APP_ID != "YOUR_APP_ID":
                return translate_text_with_baidu(text, from_lang, to_lang)
            return text
    except Exception as e:
        print(f"AI API调用失败: {e}")
        # 如果AI翻译失败，回退到百度翻译
        if BAIDU_APP_ID != "YOUR_APP_ID":
            return translate_text_with_baidu(text, from_lang, to_lang)
        return text


def translate_text_with_baidu(text, from_lang="en", to_lang="zh"):
    """使用百度翻译API翻译文本"""
    if not text.strip() or BAIDU_APP_ID == "YOUR_APP_ID":
        return text

    url = "https://fanyi-api.baidu.com/api/trans/vip/translate"
    salt = str(int(time.time()))
    sign = BAIDU_APP_ID + text + salt + BAIDU_SECRET_KEY
    import hashlib

    sign = hashlib.md5(sign.encode()).hexdigest()

    payload = {
        "q": text,
        "from": from_lang,
        "to": to_lang,
        "appid": BAIDU_APP_ID,
        "salt": salt,
        "sign": sign,
    }

    try:
        response = requests.post(url, data=payload)
        result = response.json()
        if "trans_result" in result:
            return result["trans_result"][0]["dst"]
        else:
            print(f"百度翻译错误: {result}")
            return text
    except Exception as e:
        print(f"百度翻译API调用失败: {e}")
        return text


def translate_text(text, src="en", dest="zh", use_baidu=False):
    """翻译文本块"""
    if not text.strip():
        return text

    # 提取不翻译的部分
    text, placeholders = extract_non_translatable(text)

    # 翻译文本
    if use_baidu:
        translated_text = translate_text_with_baidu(text, src, dest)
    else:
        translated_text = translate_text_with_ai(text, src, dest)

    # 还原不翻译的部分
    translated_text = restore_non_translatable(translated_text, placeholders)

    return translated_text


def translate_markdown_block(block, src="en", dest="zh", use_baidu=False):
    """翻译一个Markdown块（段落、标题等）"""
    # 处理代码块
    if block.startswith("```") and block.endswith("```"):
        return block

    # 处理HTML块
    if block.startswith("<") and block.endswith(">"):
        # 简单HTML标签检测
        if re.match(r"<[a-zA-Z]+.*>.*</[a-zA-Z]+>", block, re.DOTALL):
            return block

    # 处理标题行
    if re.match(r"^#{1,6}\s+", block):
        header_match = re.match(r"^(#{1,6}\s+)(.*?)(\s*\{.*\})?$", block)
        if header_match:
            header_marker = header_match.group(1)
            header_text = header_match.group(2)
            header_id = header_match.group(3) or ""
            translated_header = translate_text(header_text, src, dest, use_baidu)
            return f"{header_marker}{translated_header}{header_id}"

    # 处理列表项
    list_item_match = re.match(r"^(\s*[-*+]\s+)(.*?)$", block)
    if list_item_match:
        prefix = list_item_match.group(1)
        content = list_item_match.group(2)
        translated_content = translate_text(content, src, dest, use_baidu)
        return f"{prefix}{translated_content}"

    # 处理数字列表项
    num_list_match = re.match(r"^(\s*\d+\.\s+)(.*?)$", block)
    if num_list_match:
        prefix = num_list_match.group(1)
        content = num_list_match.group(2)
        translated_content = translate_text(content, src, dest, use_baidu)
        return f"{prefix}{translated_content}"

    # 处理引用块
    quote_match = re.match(r"^(\s*>\s+)(.*?)$", block)
    if quote_match:
        prefix = quote_match.group(1)
        content = quote_match.group(2)
        translated_content = translate_text(content, src, dest, use_baidu)
        return f"{prefix}{translated_content}"

    # 处理表格行
    if "|" in block and re.match(r"^\s*\|.*\|\s*$", block):
        cells = block.split("|")
        translated_cells = [
            (
                cell
                if re.match(r"^\s*[-:]+\s*$", cell)
                else translate_text(cell, src, dest, use_baidu)
            )
            for cell in cells
        ]
        return "|".join(translated_cells)

    # 处理普通段落
    return translate_text(block, src, dest, use_baidu)


def translate_markdown(content, src="en", dest="zh", use_baidu=False):
    """翻译Markdown内容"""
    # 提取前置元数据
    frontmatter, content = extract_frontmatter(content)

    # 分割内容为块
    blocks = re.split(r"(\n\s*\n)", content)

    # 翻译每个块
    translated_blocks = []
    for i, block in enumerate(blocks):
        # 空白块或换行符不翻译
        if not block.strip() or block.isspace():
            translated_blocks.append(block)
            continue

        print(f"正在翻译块 {i+1}/{len(blocks)}...")
        translated_block = translate_markdown_block(block, src, dest, use_baidu)
        translated_blocks.append(translated_block)

        # 添加延迟避免API限制
        time.sleep(0.5)

    # 重新组合内容
    translated_content = "".join(translated_blocks)

    # 如果有前置元数据，添加回内容
    if frontmatter:
        translated_content = f"---\n{frontmatter}\n---\n{translated_content}"

    return translated_content


def main():
    """主函数"""
    args = parse_arguments()

    input_path = Path(args.input_file)
    output_path = Path(args.output_file)

    # 检查输入文件是否存在
    if not input_path.exists():
        print(f"错误: 输入文件 '{input_path}' 不存在")
        return 1

    # 创建输出目录（如果不存在）
    output_path.parent.mkdir(parents=True, exist_ok=True)

    # 读取输入文件
    try:
        with open(input_path, "r", encoding="utf-8") as f:
            content = f.read()
    except Exception as e:
        print(f"读取文件时出错: {e}")
        return 1

    # 确定使用哪种翻译方式
    use_baidu = args.use_baidu
    if use_baidu and (
        BAIDU_APP_ID == "YOUR_APP_ID" or BAIDU_SECRET_KEY == "YOUR_SECRET_KEY"
    ):
        print("警告：您需要设置有效的百度翻译API凭据")
        print("请编辑脚本，将BAIDU_APP_ID和BAIDU_SECRET_KEY替换为您的凭据")
        return 1

    api_type = "百度API" if use_baidu else "AI大模型"
    print(
        f"开始使用{api_type}将 '{input_path}' 从 {args.from_lang} 翻译成 {args.to_lang}..."
    )

    # 翻译内容
    try:
        translated_content = translate_markdown(
            content, args.from_lang, args.to_lang, use_baidu
        )

        # 写入输出文件
        with open(output_path, "w", encoding="utf-8") as f:
            f.write(translated_content)

        print(f"翻译完成！输出文件保存为 '{output_path}'")
        return 0
    except Exception as e:
        print(f"翻译过程中出错: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
