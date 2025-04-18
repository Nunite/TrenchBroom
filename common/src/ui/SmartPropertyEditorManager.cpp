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

// 添加一个函数，用于解析属性名后面的括号内容，比如"message(sound)"中的"message"和"sound"
std::pair<std::string, std::string> parsePropertyFormat(const std::string& pattern) {
  // 检查是否包含括号
  auto openPos = pattern.find('(');
  if (openPos == std::string::npos) {
    return {pattern, ""};
  }
  
  auto closePos = pattern.find(')', openPos);
  if (closePos == std::string::npos) {
    return {pattern, ""};
  }
  
  // 提取属性名和类型
  std::string propertyName = pattern.substr(0, openPos);
  std::string typeName = pattern.substr(openPos + 1, closePos - openPos - 1);
  
  return {propertyName, typeName};
}

// 文件属性匹配器（支持括号格式）
SmartPropertyEditorMatcher makeFilePropertyMatcher(
  std::vector<std::string> patterns_, FilePropertyType fileType)
{
  return [patterns = std::move(patterns_), fileType](const auto& propertyKey, const auto& nodes) {
    if (nodes.empty()) return false;
    
    // 首先检查属性键是否直接匹配任何模式
    for (const auto& pattern : patterns) {
      if (kdl::cs::str_matches_glob(propertyKey, pattern)) {
        return true;
      }
      
      // 解析原始格式（如"message(sound)"）
      auto [baseName, typeName] = parsePropertyFormat(pattern);
      
      // 如果匹配基本名称，并且类型也匹配的话
      if (propertyKey == baseName) {
        // 检查属性定义是否存在并包含相应的类型信息
        const auto* propDef = mdl::selectPropertyDefinition(propertyKey, nodes);
        if (propDef) {
          const std::string& desc = propDef->shortDescription();
          
          // 根据文件类型和类型名称匹配适当的模式
          switch (fileType) {
            case FilePropertyType::SoundFile:
              if (typeName == "sound" || 
                  desc.find("<sound>") != std::string::npos ||
                  desc.find("WAV") != std::string::npos || 
                  desc.find(".wav") != std::string::npos) {
                return true;
              }
              break;
            case FilePropertyType::SpriteFile:
              if (typeName == "sprite" || 
                  desc.find("<sprite>") != std::string::npos ||
                  desc.find("Sprite Name") != std::string::npos) {
                return true;
              }
              break;
            case FilePropertyType::ModelFile:
              if (typeName == "studio" || 
                  desc.find("<model>") != std::string::npos ||
                  (desc.find("Model") != std::string::npos && 
                   desc.find("Sprite") == std::string::npos)) {
                return true;
              }
              break;
            default:
              break;
          }
        }
      }
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

  // // 注册精灵文件浏览器编辑器
  // registerEditor(
  //   makeFilePropertyMatcher({"model(sprite)"}, FilePropertyType::SpriteFile),
  //   createFileBrowserEditor(FilePropertyType::SpriteFile));

  // 注册模型文件浏览器编辑器
  registerEditor(
    makeFilePropertyMatcher({"model(studio)"}, FilePropertyType::ModelFile),
    createFileBrowserEditor(FilePropertyType::ModelFile));
    
  // 注册声音文件浏览器编辑器
  registerEditor(
    makeFilePropertyMatcher({"message(sound)"}, FilePropertyType::SoundFile),
    createFileBrowserEditor(FilePropertyType::SoundFile));
    
  // // 注册模型和精灵通用文件浏览器编辑器
  // registerEditor(
  //   makeModelSpriteFileMatcher({"modelorsprite", "modelsprite", "model(*)", "*model*(*)", "*model(*)"}),
  //   createFileBrowserEditor(FilePropertyType::ModelFile));
    
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

SmartPropertyEditor* SmartPropertyEditorManager::selectEditor(
  const std::string& propertyKey, const std::vector<mdl::EntityNodeBase*>& nodes) const
{
  // 如果没有节点，使用默认编辑器
  if (nodes.empty()) {
    return defaultEditor();
  }
  
  // 直接尝试所有匹配器，不再使用isFileBrowserProperty
  for (const auto& [matcher, editor] : m_editors) {
    if (matcher(propertyKey, nodes)) {
      // 文件类型已在创建编辑器时设置，无需在此处设置
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
