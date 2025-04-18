/*
 Copyright (C) 2010 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "SmartPropertyEditorManager.h"

#include <QStackedLayout>
#include <QWidget>
#include <unordered_set>

#include "mdl/EntityNodeBase.h"
#include "mdl/PropertyDefinition.h"
#include "ui/MapDocument.h"
#include "ui/SmartChoiceEditor.h"
#include "ui/SmartColorEditor.h"
#include "ui/SmartDefaultPropertyEditor.h"
#include "ui/SmartFlagsEditor.h"
#include "ui/SmartPropertyEditor.h"
#include "ui/SmartWadEditor.h"
#include "ui/SmartFileBrowserEditor.h"

#include "kdl/memory_utils.h"
#include "kdl/string_compare.h"

namespace tb::ui
{
namespace
{

/**
 * 检查属性定义是否包含特定的标记（如<model>、<sprite>、<sound>）
 */
bool hasFileMarker(const std::string& propertyKey, const std::vector<mdl::EntityNodeBase*>& nodes) {
  const auto* propDef = mdl::selectPropertyDefinition(propertyKey, nodes);
  if (!propDef) {
    return false;
  }
  
  const std::string& desc = propDef->shortDescription();
  return desc.find("<model>") != std::string::npos ||
         desc.find("<sprite>") != std::string::npos ||
         desc.find("<sound>") != std::string::npos;
}

/**
 * Matches if all of the nodes have a property definition for the give property key that
 * is of the type passed to the constructor.
 */
SmartPropertyEditorMatcher makeSmartTypeEditorMatcher(
  const mdl::PropertyDefinitionType type)
{
  return [=](const auto& propertyKey, const auto& nodes) {
    return !nodes.empty() && std::ranges::all_of(nodes, [&](const auto* node) {
      const auto* propDef = mdl::propertyDefinition(node, propertyKey);
      return propDef && propDef->type() == type;
    });
  };
}

/**
 * Matches if all of the nodes have a property definition for the give property key that
 * is of the type passed to the constructor, and these property definitions are all equal.
 */
SmartPropertyEditorMatcher makeSmartTypeWithSameDefinitionEditorMatcher(
  const mdl::PropertyDefinitionType type)
{
  return [=](const auto& propertyKey, const auto& nodes) {
    const auto* propDef = mdl::selectPropertyDefinition(propertyKey, nodes);
    return propDef && propDef->type() == type;
  };
}

SmartPropertyEditorMatcher makeSmartPropertyEditorKeyMatcher(
  std::vector<std::string> patterns_)
{
  return [patterns = std::move(patterns_)](const auto& propertyKey, const auto& nodes) {
    return !nodes.empty() && std::ranges::any_of(patterns, [&](const auto& pattern) {
      return kdl::cs::str_matches_glob(propertyKey, pattern);
    });
  };
}

// 添加一个匹配文件属性的Matcher
SmartPropertyEditorMatcher makeFileBrowserPropertyMatcher()
{
  return [](const auto& propertyKey, const auto& nodes) {
    // 基本的文件属性名称
    static const std::unordered_set<std::string> exactMatchProperties = {
      "model",    // 模型文件
      "studio",   // 模型文件
      "sprite",   // 精灵文件
      "sound"     // 声音文件
    };
    
    // 如果没有节点，不匹配
    if (nodes.empty()) return false;
    
    // 1. 精确匹配常见的文件属性名
    if (exactMatchProperties.find(propertyKey) != exactMatchProperties.end()) {
      return true;
    }
    
    // 2. 检查属性定义，这是最准确的方法
    const auto* propDef = mdl::selectPropertyDefinition(propertyKey, nodes);
    if (propDef) {
      const std::string& desc = propDef->shortDescription();
      
      // 检查描述中的明确关键词
      if (desc.find("<sound>") != std::string::npos ||
          desc.find("<sprite>") != std::string::npos ||
          desc.find("<model>") != std::string::npos ||
          desc.find("Sprite Name") != std::string::npos ||
          desc.find("Model / Sprite") != std::string::npos ||
          desc.find("WAV") != std::string::npos ||
          desc.find(".wav") != std::string::npos ||
          (desc.find("Model") != std::string::npos && desc.find("property") == std::string::npos) ||
          (desc.find("Sprite") != std::string::npos && desc.find("property") == std::string::npos)) {
        return true;
      }
    }
    
    // 3. 属性名称检查（较低优先级）
    if (propertyKey.find("_name") != std::string::npos && 
        (propertyKey.find("model") != std::string::npos || 
         propertyKey.find("sprite") != std::string::npos || 
         propertyKey.find("sound") != std::string::npos)) {
      return true;
    }
    
    return false;
  };
}

} // namespace

SmartPropertyEditorManager::SmartPropertyEditorManager(
  std::weak_ptr<MapDocument> document, QWidget* parent)
  : QWidget{parent}
  , m_document{std::move(document)}
  , m_stackedLayout{new QStackedLayout{this}}
{
  setLayout(m_stackedLayout);

  createEditors();
  activateEditor(defaultEditor(), "");
  connectObservers();
}

void SmartPropertyEditorManager::switchEditor(
  const std::string& propertyKey, const std::vector<mdl::Node*>& nodes)
{
  // 转换Node为EntityNodeBase
  std::vector<mdl::EntityNodeBase*> entityNodes;
  for (auto* node : nodes) {
    if (auto* entityNode = dynamic_cast<mdl::EntityNodeBase*>(node)) {
      entityNodes.push_back(entityNode);
    }
  }
  
  switchEditor(propertyKey, entityNodes);
}

void SmartPropertyEditorManager::switchEditor(
  const std::string& propertyKey, const std::vector<mdl::EntityNodeBase*>& nodes)
{
  // 如果属性键为空或没有选择实体，停用当前编辑器
  if (propertyKey.empty() || nodes.empty()) {
    deactivateEditor();
    return;
  }
  
  // 检查所有选中的实体是否都有指定的属性
  bool allHaveProperty = true;
  for (const auto* node : nodes) {
    if (!node->entity().hasProperty(propertyKey)) {
      allHaveProperty = false;
      break;
    }
  }
  
  // 如果不是所有实体都有该属性，停用编辑器
  if (!allHaveProperty) {
    deactivateEditor();
    return;
  }
  
  // 正常处理有效的属性编辑
  setVisible(true);
  
  auto* editor = selectEditor(propertyKey, nodes);
  activateEditor(editor, propertyKey);
  updateEditor();
}

SmartPropertyEditor* SmartPropertyEditorManager::activeEditor() const
{
  return static_cast<SmartPropertyEditor*>(m_stackedLayout->currentWidget());
}

bool SmartPropertyEditorManager::isDefaultEditorActive() const
{
  return activeEditor() == defaultEditor();
}

void SmartPropertyEditorManager::createEditors()
{
  assert(m_editors.empty());

  // 注册文件浏览器编辑器
  registerEditor(
    makeFileBrowserPropertyMatcher(),
    createFileBrowserEditor(FilePropertyType::AnyFile));
    
  // 添加一个新的匹配器，专门用于无亮度的颜色属性
  // 这个匹配器将匹配形如 "rgb_*" 或 "noalpha_*" 或特定的属性名
  auto* colorEditorNoBrightness = new SmartColorEditor{m_document, this};
  colorEditorNoBrightness->setBrightnessEnabled(false); // 禁用亮度控制
  registerEditor(
    makeSmartPropertyEditorKeyMatcher({"rendercolor"}),
    colorEditorNoBrightness);
    
  // 标准颜色编辑器，带亮度控制  
  registerEditor(
    makeSmartPropertyEditorKeyMatcher({"color", "*_color", "*_color2", "*_colour","_light","_diffuse_light"}),
    new SmartColorEditor{m_document, this});
    
  registerEditor(
    makeSmartTypeEditorMatcher(mdl::PropertyDefinitionType::FlagsProperty),
    new SmartFlagsEditor{m_document, this});
  registerEditor(
    makeSmartTypeWithSameDefinitionEditorMatcher(
      mdl::PropertyDefinitionType::ChoiceProperty),
    new SmartChoiceEditor{m_document, this});
  registerEditor(
    [&](const auto& propertyKey, const auto& nodes) {
      return nodes.size() == 1
             && nodes.front()->entity().classname()
                  == mdl::EntityPropertyValues::WorldspawnClassname
             && propertyKey
                  == kdl::mem_lock(m_document)->game()->config().materialConfig.property;
    },
    new SmartWadEditor{m_document, this});
  registerEditor(
    [](const auto&, const auto&) { return true; },
    new SmartDefaultPropertyEditor{m_document, this});
}

void SmartPropertyEditorManager::registerEditor(
  SmartPropertyEditorMatcher matcher, SmartPropertyEditor* editor)
{
  m_editors.emplace_back(std::move(matcher), editor);
  m_stackedLayout->addWidget(editor);
}

void SmartPropertyEditorManager::connectObservers()
{
  auto document = kdl::mem_lock(m_document);
  m_notifierConnection += document->selectionDidChangeNotifier.connect(
    this, &SmartPropertyEditorManager::selectionDidChange);
  m_notifierConnection += document->nodesDidChangeNotifier.connect(
    this, &SmartPropertyEditorManager::nodesDidChange);
}

void SmartPropertyEditorManager::selectionDidChange(const Selection&)
{
  auto document = kdl::mem_lock(m_document);
  
  // 检查当前选择的实体
  const auto entityNodes = document->allSelectedEntityNodes();
  
  // 如果当前没有选中属性，或者没有选中实体，清除编辑器状态
  if (m_propertyKey.empty() || entityNodes.empty()) {
    deactivateEditor();
    return;
  }
  
  // 检查选中的实体是否都有当前的属性
  bool allHaveProperty = true;
  for (const auto* node : entityNodes) {
    if (!node->entity().hasProperty(m_propertyKey)) {
      allHaveProperty = false;
      break;
    }
  }
  
  // 如果不是所有选中的实体都有当前属性，重置编辑器状态
  if (!allHaveProperty) {
    deactivateEditor();
    return;
  }
  
  // 否则，正常更新编辑器
  switchEditor(m_propertyKey, entityNodes);
}

void SmartPropertyEditorManager::nodesDidChange(const std::vector<mdl::Node*>&)
{
  auto document = kdl::mem_lock(m_document);
  
  // 检查当前选择的实体
  const auto entityNodes = document->allSelectedEntityNodes();
  
  // 如果当前没有选中属性，或者没有选中实体，清除编辑器状态
  if (m_propertyKey.empty() || entityNodes.empty()) {
    deactivateEditor();
    return;
  }
  
  // 检查选中的实体是否都有当前的属性
  bool allHaveProperty = true;
  for (const auto* node : entityNodes) {
    if (!node->entity().hasProperty(m_propertyKey)) {
      allHaveProperty = false;
      break;
    }
  }
  
  // 如果不是所有选中的实体都有当前属性，重置编辑器状态
  if (!allHaveProperty) {
    deactivateEditor();
    return;
  }
  
  // 否则，正常更新编辑器
  switchEditor(m_propertyKey, entityNodes);
}

// 判断属性是否适用于文件浏览器
bool SmartPropertyEditorManager::isFileBrowserProperty(const std::string& propertyKey) const {
  // 检查基本的属性名
  static const std::unordered_set<std::string> exactMatchProperties = {
    "model",    // 模型文件
    "studio",   // 模型文件
    "sprite",   // 精灵文件
    "sound"     // 声音文件
  };
  
  // 直接匹配常见的文件属性名
  if (exactMatchProperties.find(propertyKey) != exactMatchProperties.end()) {
    return true;
  }
  
  // 根据属性名称的模式进行匹配
  // 例如：WAV Name、Sprite Name等
  if (propertyKey.find("_name") != std::string::npos) {
    return true;
  }
  
  return false;
}

SmartPropertyEditor* SmartPropertyEditorManager::selectEditor(
  const std::string& propertyKey, const std::vector<mdl::EntityNodeBase*>& nodes) const
{
  // 如果没有节点，使用默认编辑器
  if (nodes.empty()) {
    return defaultEditor();
  }
  
  // 检查是否为文件属性
  if (isFileBrowserProperty(propertyKey)) {
    // 获取属性定义来确定文件类型
    const auto* propDef = mdl::selectPropertyDefinition(propertyKey, nodes);
    
    for (const auto& [matcher, editor] : m_editors) {
      if (auto* fileBrowser = dynamic_cast<SmartFileBrowserEditor*>(editor)) {
        // 根据属性定义和属性名决定文件类型
        FilePropertyType fileType = FilePropertyType::AnyFile;
        
        // 首先检查属性的描述（优先级最高）
        if (propDef) {
          const std::string& desc = propDef->shortDescription();
          
          // 检查特定标记和描述短语
          if (desc.find("<sound>") != std::string::npos) {
            fileType = FilePropertyType::SoundFile;
          } else if (desc.find("<sprite>") != std::string::npos) {
            fileType = FilePropertyType::SpriteFile;
          } else if (desc.find("<model>") != std::string::npos) {
            fileType = FilePropertyType::ModelFile;
          } else if (desc.find("Sprite Name") != std::string::npos) {
            fileType = FilePropertyType::SpriteFile;
          } else if (desc.find("Model / Sprite") != std::string::npos) {
            fileType = FilePropertyType::ModelFile;
          } else if (desc.find("WAV") != std::string::npos || 
                    desc.find(".wav") != std::string::npos ||
                    desc.find("Path/filename.wav") != std::string::npos) {
            fileType = FilePropertyType::SoundFile;
          } else if (desc.find("Model") != std::string::npos && 
                    desc.find("Sprite") == std::string::npos) {
            fileType = FilePropertyType::ModelFile;
          } else if (desc.find("Sprite") != std::string::npos) {
            fileType = FilePropertyType::SpriteFile;
          }
        }
        
        // 如果无法从描述确定类型，则根据属性名判断
        if (fileType == FilePropertyType::AnyFile) {
          if (propertyKey == "model" || propertyKey == "studio") {
            fileType = FilePropertyType::ModelFile;
          } else if (propertyKey == "sprite") {
            fileType = FilePropertyType::SpriteFile;
          } else if (propertyKey == "sound") {
            fileType = FilePropertyType::SoundFile;
          } else if (propertyKey.find("sound") != std::string::npos || 
                    propertyKey.find("wav") != std::string::npos) {
            fileType = FilePropertyType::SoundFile;
          } else if (propertyKey.find("sprite") != std::string::npos) {
            fileType = FilePropertyType::SpriteFile;
          } else if (propertyKey.find("model") != std::string::npos || 
                    propertyKey.find("mdl") != std::string::npos) {
            fileType = FilePropertyType::ModelFile;
          }
        }
        
        // 设置文件类型并返回编辑器
        fileBrowser->setPropertyType(fileType);
        return editor;
      }
    }
  }

  // 尝试其他匹配器
  for (const auto& [matcher, editor] : m_editors) {
    if (matcher(propertyKey, nodes)) {
      return editor;
    }
  }

  // 返回默认编辑器
  return defaultEditor();
}

SmartPropertyEditor* SmartPropertyEditorManager::defaultEditor() const
{
  return std::get<1>(m_editors.back());
}

void SmartPropertyEditorManager::activateEditor(
  SmartPropertyEditor* editor, const std::string& propertyKey)
{
  if (
    m_stackedLayout->currentWidget() != editor
    || !activeEditor()->usesPropertyKey(propertyKey))
  {
    deactivateEditor();

    m_propertyKey = propertyKey;
    m_stackedLayout->setCurrentWidget(editor);
    editor->activate(m_propertyKey);
  }
}

void SmartPropertyEditorManager::deactivateEditor()
{
  if (activeEditor())
  {
    activeEditor()->deactivate();
    m_stackedLayout->setCurrentIndex(-1);
    m_propertyKey = "";
  }
}

void SmartPropertyEditorManager::updateEditor()
{
  if (activeEditor())
  {
    auto document = kdl::mem_lock(m_document);
    activeEditor()->update(document->allSelectedEntityNodes());
  }
}

// 创建文件浏览器编辑器
SmartFileBrowserEditor* SmartPropertyEditorManager::createFileBrowserEditor(FilePropertyType type) {
  SmartFileBrowserEditor* editor = new SmartFileBrowserEditor(m_document, this);
  editor->setPropertyType(type);
  
  return editor;
}

} // namespace tb::ui
