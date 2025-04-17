#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
简易Markdown翻译脚本

这个脚本用于将Markdown文件从英文翻译成中文，使用AI大模型API，操作简单。
适合不熟悉Python的用户使用。

使用方法:
    python translate_markdown_simple.py 输入文件.md 输出文件.md [分块大小] [--split-by-heading]

选项:
    --split-by-heading    按照 Markdown 标题（#、##、###等）拆分内容进行翻译

依赖:
    - requests (可通过 pip install requests 安装)
"""

import os
import sys
import requests
import time
import re
from pathlib import Path

# AI大模型API配置
AI_API_URL = "https://ark.cn-beijing.volces.com/api/v3/chat/completions"  # "https://api.siliconflow.cn/v1/chat/completions"
AI_API_KEY = "f58d2dc3-2a5b-4b25-bd69-97944a446bbf"  # "sk-hlgmghidkyerkaqjeonawwlhpsvizkfsspntjywqkzqehhkp"  # 替换为您的API密钥
AI_MODEL = "deepseek-v3-250324"  # "deepseek-ai/DeepSeek-V3"

# 模型能处理的最大字符数（大约3000个token）
MAX_CHUNK_SIZE = 8000
# 默认分块大小（以字符数计算）
DEFAULT_CHUNK_SIZE = 3000


def translate_markdown_with_ai(text, from_lang="en", to_lang="zh"):
    """使用AI大模型翻译Markdown文本"""
    if not text.strip():
        return ""

    prompt = f"""你是一个专业的翻译师，当前项目是翻译一个三维编辑器软件的使用手册，请按照以下要求进行翻译：
1. 从{from_lang}翻译成{to_lang}
2. 保留所有Markdown格式，包括标题符号、列表符号、链接、图片等
3. 代码块和代码片段中的内容不要翻译
4. 保留原始的HTML标签和特殊格式
5. 专业术语要根据上下文正确翻译,比如说:专有名词"Brush","map"等不翻译
6. 保留所有标题中的ID标签，如 {{#introduction}}
7. 只返回翻译结果，不要添加任何解释

以下是需要翻译的Markdown文本：

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
        print("正在发送翻译请求...")
        response = requests.post(AI_API_URL, json=payload, headers=headers)
        result = response.json()

        if "choices" in result and len(result["choices"]) > 0:
            return result["choices"][0]["message"]["content"].strip()
        else:
            print(f"AI翻译错误: {result}")
            return text
    except Exception as e:
        print(f"AI API调用失败: {e}")
        return text


def split_text_by_headings(text, heading_pattern=r"^(#{1,6})\s+.+?(\{#.*?\})?$"):
    """
    按照指定的标题模式拆分文本
    默认按照所有级别的标题（#、##、###等）拆分
    同时保留标题中的ID标签
    """
    # 查找所有符合模式的标题
    headings = []
    for match in re.finditer(heading_pattern, text, re.MULTILINE):
        headings.append((match.start(), match.group()))

    if not headings:
        return [text]

    # 拆分文本
    chunks = []
    for i, (pos, heading) in enumerate(headings):
        if i == 0:
            # 处理第一个标题前的内容（如果有）
            if pos > 0:
                chunks.append(text[:pos])

        # 确定当前块的结束位置
        end_pos = headings[i + 1][0] if i + 1 < len(headings) else len(text)
        chunks.append(text[pos:end_pos])

    return chunks


def split_text_into_chunks(text, chunk_size):
    """
    智能分块函数，尝试在段落边界分割文本
    """
    if len(text) <= chunk_size:
        return [text]

    chunks = []
    current_pos = 0

    while current_pos < len(text):
        # 如果剩余文本不足一个块，直接添加
        if current_pos + chunk_size >= len(text):
            chunks.append(text[current_pos:])
            break

        # 寻找分割点：优先在段落边界（双换行），其次在单个段落边界，最后在句子边界
        split_pos = text.rfind("\n\n", current_pos, current_pos + chunk_size)

        if split_pos == -1 or split_pos <= current_pos:
            # 如果找不到段落边界，尝试寻找单个换行
            split_pos = text.rfind("\n", current_pos, current_pos + chunk_size)

            if split_pos == -1 or split_pos <= current_pos:
                # 如果找不到换行，尝试在句子边界分割
                for sep in [". ", "? ", "! ", "; ", "。", "？", "！", "；"]:
                    sp = text.rfind(sep, current_pos, current_pos + chunk_size)
                    if sp != -1 and sp > current_pos:
                        split_pos = sp + len(sep) - 1
                        break

                # 如果仍然找不到合适的分割点，就在单词边界分割
                if split_pos == -1 or split_pos <= current_pos:
                    split_pos = text.rfind(" ", current_pos, current_pos + chunk_size)

                    # 实在找不到，就强制分割
                    if split_pos == -1 or split_pos <= current_pos:
                        split_pos = current_pos + chunk_size

        # 添加当前块
        chunks.append(text[current_pos : split_pos + 1])
        current_pos = split_pos + 1

    return chunks


def ensure_fenced_code_blocks_integrity(chunks):
    """
    确保Markdown代码块的完整性，避免在代码块中间分割
    """
    result_chunks = []
    code_block_content = ""
    in_code_block = False

    for chunk in chunks:
        # 检查是否有未闭合的代码块
        fenced_starts = len(re.findall(r"```[a-zA-Z0-9]*\n", chunk))
        fenced_ends = len(re.findall(r"```\n", chunk)) + len(re.findall(r"```$", chunk))

        if not in_code_block:
            # 如果这个块包含完整的代码块或没有代码块，直接添加
            if fenced_starts == fenced_ends:
                result_chunks.append(chunk)
            else:
                # 有未闭合的代码块，开始收集
                in_code_block = True
                code_block_content = chunk
        else:
            # 继续收集代码块
            code_block_content += chunk

            # 检查是否代码块已闭合
            if fenced_starts <= fenced_ends:
                result_chunks.append(code_block_content)
                code_block_content = ""
                in_code_block = False

    # 处理最后一个未闭合的代码块
    if code_block_content:
        result_chunks.append(code_block_content)

    return result_chunks


def ensure_heading_id_intact(translated_text, original_text):
    """
    确保标题ID标签在翻译后保持完整
    例如：# 介绍 {#introduction} 中的 {#introduction} 应保持不变
    """
    # 查找原文中的标题ID标签
    id_tags = re.findall(r"(\{#[^}]+\})", original_text)

    # 查找翻译文本中的标题
    headings = re.finditer(
        r"^(#{1,6}\s+.+?)(\{#[^}]+\})?$", translated_text, re.MULTILINE
    )

    # 创建新的文本来替换修正后的翻译
    result = translated_text

    # 保存所有标题的ID标签
    heading_ids = {}
    for match in re.finditer(
        r"^(#{1,6}\s+.+?)(\{#([^}]+)\})?$", original_text, re.MULTILINE
    ):
        if match.group(2):  # 如果有ID标签
            heading_text = match.group(1).strip()
            heading_id = match.group(3)
            heading_ids[heading_text] = heading_id

    # 修复翻译文本中的标题ID
    for match in re.finditer(
        r"^(#{1,6}\s+.+?)(\{#[^}]+\})?$", translated_text, re.MULTILINE
    ):
        # 如果翻译的标题缺少ID标签或ID标签不正确
        if not match.group(2):
            # 尝试在原文找到对应的标题ID
            for orig_heading, heading_id in heading_ids.items():
                if heading_id:
                    # 添加ID标签到翻译后的标题
                    new_heading = f"{match.group(1)} {{#{heading_id}}}"
                    result = result.replace(match.group(0), new_heading)
                    break

    return result


def main():
    """主函数"""
    # 命令行参数处理
    if len(sys.argv) < 3:
        print(
            "使用方法: python translate_markdown_simple.py 输入文件.md 输出文件.md [分块大小] [--split-by-heading]"
        )
        return 1

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    # 检查是否按标题拆分
    split_by_heading = "--split-by-heading" in sys.argv
    if split_by_heading:
        print("已启用按标题拆分模式...")
        # 如果指定了按标题拆分，需要从参数列表中移除该选项
        sys.argv.remove("--split-by-heading")

    # 可选的分块大小参数
    chunk_size = DEFAULT_CHUNK_SIZE
    if len(sys.argv) > 3 and sys.argv[3].isdigit():
        chunk_size = int(sys.argv[3])
        # 确保分块大小不超过最大限制
        if chunk_size > MAX_CHUNK_SIZE:
            print(f"警告: 分块大小超过最大限制 {MAX_CHUNK_SIZE}，已自动调整")
            chunk_size = MAX_CHUNK_SIZE

    input_path = Path(input_file)
    output_path = Path(output_file)

    # 检查输入文件是否存在
    if not input_path.exists():
        print(f"错误: 输入文件 '{input_path}' 不存在")
        return 1

    # 创建输出目录（如果不存在）
    output_path.parent.mkdir(parents=True, exist_ok=True)

    # 读取输入文件
    try:
        print(f"正在读取文件 '{input_path}'...")
        with open(input_path, "r", encoding="utf-8") as f:
            content = f.read()
    except Exception as e:
        print(f"读取文件时出错: {e}")
        return 1

    # 添加进度显示
    print(f"开始翻译文件 '{input_path}'...")
    print(f"文件大小: {len(content)} 字符")
    print(f"分块大小: {chunk_size} 字符")

    # 初始化翻译内容
    translated_content = ""

    # 根据拆分模式处理文件
    if split_by_heading:
        # 按标题拆分
        heading_chunks = split_text_by_headings(content)
        print(f"按标题拆分为 {len(heading_chunks)} 个块")

        translated_chunks = []

        for i, chunk in enumerate(heading_chunks):
            if not chunk.strip():
                translated_chunks.append(chunk)
                continue

            print(
                f"正在翻译第 {i+1}/{len(heading_chunks)} 个标题块 (长度: {len(chunk)} 字符)..."
            )

            # 如果标题块太大，继续按大小拆分
            if len(chunk) > chunk_size:
                print(f"标题块 {i+1} 较大，继续拆分...")
                sub_chunks = split_text_into_chunks(chunk, chunk_size)
                sub_chunks = ensure_fenced_code_blocks_integrity(sub_chunks)

                sub_translated = []
                for j, sub_chunk in enumerate(sub_chunks):
                    if not sub_chunk.strip():
                        sub_translated.append(sub_chunk)
                        continue

                    print(
                        f"  翻译子块 {j+1}/{len(sub_chunks)} (长度: {len(sub_chunk)} 字符)..."
                    )
                    sub_translated_chunk = translate_markdown_with_ai(sub_chunk)
                    # 确保标题ID标签完整
                    sub_translated_chunk = ensure_heading_id_intact(
                        sub_translated_chunk, sub_chunk
                    )
                    sub_translated.append(sub_translated_chunk)

                    if j < len(sub_chunks) - 1:
                        delay = 0.5
                        print(f"  等待 {delay} 秒...")
                        time.sleep(delay)

                translated_chunk = "".join(sub_translated)
            else:
                # 直接翻译整个标题块
                translated_chunk = translate_markdown_with_ai(chunk)
                # 确保标题ID标签完整
                translated_chunk = ensure_heading_id_intact(translated_chunk, chunk)

            translated_chunks.append(translated_chunk)

            # 添加延迟避免API限制
            if i < len(heading_chunks) - 1:
                delay = 0.5
                print(f"等待 {delay} 秒...")
                time.sleep(delay)

        translated_content = "".join(translated_chunks)
    else:
        # 使用原来的方法
        if len(content) > chunk_size:
            print("文件较大，将分块翻译...")

            # 智能分块处理
            chunks = split_text_into_chunks(content, chunk_size)
            print(f"初始分块: {len(chunks)} 块")

            # 确保代码块完整性
            chunks = ensure_fenced_code_blocks_integrity(chunks)
            print(f"调整后分块: {len(chunks)} 块")

            translated_chunks = []

            for i, chunk in enumerate(chunks):
                if not chunk.strip():
                    translated_chunks.append(chunk)
                    continue

                print(f"正在翻译第 {i+1}/{len(chunks)} 块 (长度: {len(chunk)} 字符)...")
                translated_chunk = translate_markdown_with_ai(chunk)
                # 尝试修复ID标签
                translated_chunk = ensure_heading_id_intact(translated_chunk, chunk)
                translated_chunks.append(translated_chunk)

                # 添加延迟避免API限制
                if i < len(chunks) - 1:
                    delay = 0.5
                    print(f"等待 {delay} 秒...")
                    time.sleep(delay)

            translated_content = "".join(translated_chunks)
        else:
            print("文件较小，整体翻译...")
            translated_content = translate_markdown_with_ai(content)
            # 尝试修复ID标签
            translated_content = ensure_heading_id_intact(translated_content, content)

    # 写入输出文件
    try:
        print(f"正在保存翻译结果到 '{output_path}'...")
        with open(output_path, "w", encoding="utf-8") as f:
            f.write(translated_content)

        print(f"翻译完成！输出文件已保存为 '{output_path}'")
        return 0
    except Exception as e:
        print(f"保存文件时出错: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
