import os
import sys
import xml.etree.ElementTree as ET
import requests
import time
from concurrent.futures import ThreadPoolExecutor
import argparse

# 配置翻译API（这里使用百度翻译API作为示例）
# 您需要在百度翻译开放平台申请APP ID和密钥
# https://fanyi-api.baidu.com/
BAIDU_APP_ID = "YOUR_APP_ID"
BAIDU_SECRET_KEY = "YOUR_SECRET_KEY"

# 添加AI大模型API配置
AI_API_URL = "https://api.siliconflow.cn/v1/chat/completions"
AI_API_KEY = "sk-hlgmghidkyerkaqjeonawwlhpsvizkfsspntjywqkzqehhkp"
AI_MODEL = "deepseek-ai/DeepSeek-V3"

def translate_text_with_ai(text, from_lang="en", to_lang="zh"):
    """使用AI大模型翻译文本"""
    if not text.strip():
        return ""
    
    prompt = f"""你是一个专业的翻译师，当前项目是Quake/GoldSrc引擎制作地图软件的翻译文件，你需要根据三维软件常见的关系去翻译，特定词义英文需要保留。
请将以下文本从{from_lang}翻译成{to_lang}，只需要返回翻译结果，不要有任何解释：

{text}"""
    
    payload = {
        "model": AI_MODEL,
        "stream": False,
        "max_tokens": 512,
        "temperature": 0.7,
        "top_p": 0.7,
        "top_k": 50,
        "frequency_penalty": 0.5,
        "n": 1,
        "messages": [{"role": "user", "content": prompt}],
        "stop": []
    }
    
    headers = {
        "Authorization": f"Bearer {AI_API_KEY}",
        "Content-Type": "application/json"
    }
    
    try:
        response = requests.post(AI_API_URL, json=payload, headers=headers)
        result = response.json()
        
        if 'choices' in result and len(result['choices']) > 0:
            return result['choices'][0]['message']['content'].strip()
        else:
            print(f"AI翻译错误: {result}")
            # 如果AI翻译失败，回退到百度翻译
            return translate_text_with_baidu(text, from_lang, to_lang)
    except Exception as e:
        print(f"AI API调用失败: {e}")
        # 如果AI翻译失败，回退到百度翻译
        return translate_text_with_baidu(text, from_lang, to_lang)

def translate_text_with_baidu(text, from_lang="en", to_lang="zh"):
    """使用百度翻译API翻译文本"""
    if not text.strip():
        return ""
    
    # 如果您使用的是百度翻译API
    url = "https://fanyi-api.baidu.com/api/trans/vip/translate"
    salt = str(int(time.time()))
    sign = BAIDU_APP_ID + text + salt + BAIDU_SECRET_KEY
    import hashlib
    sign = hashlib.md5(sign.encode()).hexdigest()
    
    payload = {
        'q': text,
        'from': from_lang,
        'to': to_lang,
        'appid': BAIDU_APP_ID,
        'salt': salt,
        'sign': sign
    }
    
    try:
        response = requests.post(url, data=payload)
        result = response.json()
        if 'trans_result' in result:
            return result['trans_result'][0]['dst']
        else:
            print(f"翻译错误: {result}")
            return text
    except Exception as e:
        print(f"翻译API调用失败: {e}")
        return text

def translate_text(text, from_lang="en", to_lang="zh", use_ai=True):
    """翻译文本，可选择使用AI或百度API"""
    if use_ai:
        return translate_text_with_ai(text, from_lang, to_lang)
    else:
        return translate_text_with_baidu(text, from_lang, to_lang)

def translate_ts_file(input_file, output_file=None, from_lang="en", to_lang="zh", max_workers=10, use_ai=True):
    """翻译TS文件中的未翻译字符串"""
    if output_file is None:
        output_file = input_file
    
    try:
        # 解析XML文件
        tree = ET.parse(input_file)
        root = tree.getroot()
        
        # 查找所有未翻译的消息
        untranslated_elements = []
        for context in root.findall('.//context'):
            for message in context.findall('.//message'):
                translation = message.find('translation')
                if translation is not None and translation.get('type') == 'unfinished' and not translation.text:
                    source = message.find('source')
                    if source is not None and source.text:
                        untranslated_elements.append((source.text, translation))
        
        total = len(untranslated_elements)
        print(f"找到 {total} 个未翻译的字符串")
        print(f"使用{'AI大模型' if use_ai else '百度API'}进行翻译")
        
        # 使用线程池并行翻译
        def translate_element(item):
            source_text, translation_elem = item
            translated_text = translate_text(source_text, from_lang, to_lang, use_ai)
            return translation_elem, translated_text
        
        count = 0
        with ThreadPoolExecutor(max_workers=max_workers) as executor:
            for translation_elem, translated_text in executor.map(translate_element, untranslated_elements):
                translation_elem.text = translated_text
                translation_elem.attrib.pop('type', None)  # 移除unfinished标记
                count += 1
                if count % 10 == 0 or count == total:
                    print(f"已翻译: {count}/{total}")
                # 避免API调用过于频繁
                time.sleep(0.5 if use_ai else 0.1)  # AI API可能需要更长的间隔
        
        # 保存翻译后的文件
        tree.write(output_file, encoding='utf-8', xml_declaration=True)
        print(f"翻译完成，已保存到 {output_file}")
        
    except Exception as e:
        print(f"处理文件时出错: {e}")
        return False
    
    return True

def main():
    parser = argparse.ArgumentParser(description='翻译Qt TS文件')
    parser.add_argument('input_file', help='输入TS文件路径')
    parser.add_argument('-o', '--output', help='输出TS文件路径（默认覆盖输入文件）')
    parser.add_argument('-f', '--from-lang', default='en', help='源语言（默认：en）')
    parser.add_argument('-t', '--to-lang', default='zh', help='目标语言（默认：zh）')
    parser.add_argument('-w', '--workers', type=int, default=10, help='并行工作线程数（默认：10）')
    parser.add_argument('--use-ai', action='store_true', help='使用AI大模型进行翻译（默认：False）')
    parser.add_argument('--use-baidu', action='store_true', help='使用百度API进行翻译（默认：False）')
    
    args = parser.parse_args()
    
    if not os.path.exists(args.input_file):
        print(f"错误：找不到输入文件 {args.input_file}")
        return 1
    
    # 确定使用哪种翻译方式
    use_ai = True  # 默认使用AI
    if args.use_baidu:
        use_ai = False
        if BAIDU_APP_ID == "YOUR_APP_ID" or BAIDU_SECRET_KEY == "YOUR_SECRET_KEY":
            print("警告：您需要设置有效的百度翻译API凭据")
            print("请编辑脚本，将BAIDU_APP_ID和BAIDU_SECRET_KEY替换为您的凭据")
            return 1
    
    success = translate_ts_file(
        args.input_file, 
        args.output, 
        args.from_lang, 
        args.to_lang,
        args.workers,
        use_ai
    )
    
    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())