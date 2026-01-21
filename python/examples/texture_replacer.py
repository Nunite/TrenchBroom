
import tb

def replace_textures():
    """
    替换纹理函数：
    1. 获取 "find" 和 "replace" 输入框的内容。
    2. 创建一个 undoable 事务。
    3. 遍历所有选中 brush 的所有 face。
    4. 如果 face 的纹理名与 "find" 匹配，则替换为 "replace"。
    5. 统计并输出替换数量。
    """
    try:
        doc = tb.Document.current()
        if not doc:
            print("Error: No active document.")
            return

        panel_find = panel.get_text_field("find")
        panel_replace = panel.get_text_field("replace")
        
        # 简单的输入校验
        if not panel_find:
            print("Error: Please enter a texture name to find.")
            return
        if not panel_replace:
            print("Error: Please enter a replacement texture name.")
            return

        print(f"Replacing '{panel_find}' with '{panel_replace}' in selection...")

        count = 0
        
        # 使用事务包裹修改操作，支持撤销 (Undo)
        with doc.transaction("Replace Texture"):
            # 遍历当前选择中的所有 Brush
            for brush in doc.selection.brushes:
                # 遍历 Brush 的所有 Face
                for face in brush.faces():
                    # 检查纹理名（不区分大小写比较通常更友好，但这里我们先做精确匹配，或者根据需求调整）
                    if face.texture_name.lower() == panel_find.lower():
                        face.texture_name = panel_replace
                        count += 1
        
        if count > 0:
            print(f"Success: Replaced {count} faces.")
        else:
            print("No matching textures found in selection.")

    except Exception as e:
        print(f"An error occurred: {e}")

# 创建插件面板
# 注意：每次运行脚本都会尝试创建面板。如果已存在同名面板，行为取决于 TrenchBroom 实现（通常会新建或覆盖）。
# 建议在开发时多次运行前先关闭旧面板，或者 TrenchBroom 会处理。
panel = tb.create_plugin_panel("Texture Replacer")

# 添加说明标签
panel.add_label("Select brushes and enter texture names to replace.")

# 添加输入字段
# key, label, default_value
panel.add_text_field("find", "Find Texture:", "")
panel.add_text_field("replace", "Replace With:", "")

# 添加执行按钮
panel.add_button_callback("Replace All in Selection", replace_textures)

print("Texture Replacer panel created. Check the 'Inspector' -> 'Plugin' tab.")
