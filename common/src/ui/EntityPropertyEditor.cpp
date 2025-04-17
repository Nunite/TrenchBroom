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
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "EntityPropertyEditor.h"

#include <QChar>
#include <QStringBuilder>
#include <QTextEdit>
#include <QTextStream>
#include <QVBoxLayout>

#include "mdl/EntityDefinition.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/PropertyDefinition.h"
#include "ui/EntityPropertyGrid.h"
#include "ui/MapDocument.h"
#include "ui/QtUtils.h"
#include "ui/SmartPropertyEditorManager.h"
#include "ui/SmartFileBrowserEditor.h"
#include "ui/Splitter.h"

#include "kdl/memory_utils.h"

#include <algorithm>
#include <unordered_set>

namespace tb::ui
{
// 判断属性是否适用于文件浏览器
bool isFileBrowserProperty(const std::string& propertyKey) {
  // 定义一组特定的属性名，这些属性通常对应文件路径
  static const std::unordered_set<std::string> exactMatchProperties = {
    "model",       // 模型文件
    "studio",      // 另一种模型文件
    "sprite",      // 精灵文件
    "sound"        // 单个声音文件
  };
  
  // 精确匹配常见的文件属性名
  if (exactMatchProperties.find(propertyKey) != exactMatchProperties.end()) {
    return true;
  }
  
  // 检查属性名称是否包含_name并且也包含关键词
  if (propertyKey.find("_name") != std::string::npos) {
    if (propertyKey.find("model") != std::string::npos || 
        propertyKey.find("sprite") != std::string::npos || 
        propertyKey.find("sound") != std::string::npos) {
      return true;
    }
  }
  
  return false;
}

EntityPropertyEditor::EntityPropertyEditor(
  std::weak_ptr<MapDocument> document, QWidget* parent)
  : QWidget{parent}
  , m_document{std::move(document)}
{
  createGui(m_document);
  connectObservers();
}

EntityPropertyEditor::~EntityPropertyEditor()
{
  saveWindowState(m_splitter);
}

void EntityPropertyEditor::OnCurrentRowChanged()
{
  updateDocumentationAndSmartEditor();
}

void EntityPropertyEditor::connectObservers()
{
  auto document = kdl::mem_lock(m_document);
  m_notifierConnection += document->selectionDidChangeNotifier.connect(
    this, &EntityPropertyEditor::selectionDidChange);
  m_notifierConnection +=
    document->nodesDidChangeNotifier.connect(this, &EntityPropertyEditor::nodesDidChange);
}

void EntityPropertyEditor::selectionDidChange(const Selection&)
{
  updateIfSelectedEntityDefinitionChanged();
}

void EntityPropertyEditor::nodesDidChange(const std::vector<mdl::Node*>&)
{
  updateIfSelectedEntityDefinitionChanged();
}

void EntityPropertyEditor::updateIfSelectedEntityDefinitionChanged()
{
  auto document = kdl::mem_lock(m_document);
  const auto* entityDefinition =
    mdl::selectEntityDefinition(document->allSelectedEntityNodes());

  if (entityDefinition != m_currentDefinition)
  {
    m_currentDefinition = entityDefinition;
    updateDocumentationAndSmartEditor();
  }
}

void EntityPropertyEditor::updateDocumentationAndSmartEditor()
{
  auto document = kdl::mem_lock(m_document);
  const auto& propertyKey = m_propertyGrid->selectedRowName();

  // 无论是否为文件浏览属性，都使用switchEditor方法
  m_smartEditorManager->switchEditor(propertyKey, document->allSelectedEntityNodes());

  updateDocumentation(propertyKey);

  // collapse the splitter if needed
  m_documentationText->setHidden(m_documentationText->document()->isEmpty());
  m_smartEditorManager->setHidden(m_smartEditorManager->isDefaultEditorActive());

  updateMinimumSize();
}

QString EntityPropertyEditor::optionDescriptions(
  const mdl::PropertyDefinition& definition)
{
  static const auto bullet = QString{" "} + QChar{0x2022} + QString{" "};

  switch (definition.type())
  {
  case mdl::PropertyDefinitionType::ChoiceProperty: {
    const auto& choiceDef =
      dynamic_cast<const mdl::ChoicePropertyDefinition&>(definition);

    auto result = QString{};
    auto stream = QTextStream{&result};
    for (const auto& option : choiceDef.options())
    {
      stream << bullet << option.value().c_str();
      if (!option.description().empty())
      {
        stream << " (" << option.description().c_str() << ")";
      }
      stream << "\n";
    }
    return result;
  }
  case mdl::PropertyDefinitionType::FlagsProperty: {
    const auto& flagsDef = dynamic_cast<const mdl::FlagsPropertyDefinition&>(definition);

    // The options are not necessarily sorted by value, so we sort the descriptions here
    // by inserting into a map sorted by the flag value.
    auto flagDescriptors = std::map<int, QString>{};
    for (const auto& option : flagsDef.options())
    {
      auto line = QString{};
      auto stream = QTextStream{&line};
      stream << bullet << option.value() << " = " << option.shortDescription().c_str();
      if (!option.longDescription().empty())
      {
        stream << " (" << option.longDescription().c_str() << ")";
      }
      flagDescriptors[option.value()] = line;
    }

    // Concatenate the flag descriptions and return.
    auto result = QString{};
    auto stream = QTextStream{&result};
    for (const auto& [value, description] : flagDescriptors)
    {
      stream << description << "\n";
    }
    return result;
  }
  // 为文件类型添加描述，更智能地检测文件类型
  default: {
    const std::string& shortDesc = definition.shortDescription();
    const std::string& key = definition.key();
    
    // 根据短描述确定文件类型
    if (shortDesc.find("<sound>") != std::string::npos || 
        shortDesc.find("WAV") != std::string::npos || 
        shortDesc.find(".wav") != std::string::npos) {
      return "Expected value: Sound file path (*.wav)";
    } else if (shortDesc.find("<sprite>") != std::string::npos || 
              shortDesc.find("Sprite Name") != std::string::npos) {
      return "Expected value: Sprite file path (*.spr)";
    } else if (shortDesc.find("<model>") != std::string::npos || 
              (shortDesc.find("Model") != std::string::npos && 
               shortDesc.find("Sprite") == std::string::npos)) {
      return "Expected value: Model file path (*.mdl)";
    } else if (shortDesc.find("Model / Sprite") != std::string::npos) {
      return "Expected value: Model file path (*.mdl) or Sprite file path (*.spr)";
    } 
    // 如果无法从短描述确定，则回退到属性名
    else if (isFileBrowserProperty(key)) {
      if (key.find("model") != std::string::npos) {
        return "Expected value: Model file path (*.mdl)";
      } else if (key.find("sound") != std::string::npos) {
        return "Expected value: Sound file path (*.wav)";
      } else if (key.find("sprite") != std::string::npos) {
        return "Expected value: Sprite file path (*.spr)";
      }
    }
    
    return {};
  }
  }
}

void EntityPropertyEditor::updateDocumentation(const std::string& propertyKey)
{
  m_documentationText->clear();

  auto document = kdl::mem_lock(m_document);
  if (
    const auto* entityDefinition =
      mdl::selectEntityDefinition(document->allSelectedEntityNodes()))
  {
    auto normalFormat = QTextCharFormat{};
    auto boldFormat = QTextCharFormat{};
    boldFormat.setFontWeight(QFont::Bold);

    // add property documentation, if available
    if (
      const auto* propertyDefinition = entityDefinition->propertyDefinition(propertyKey))
    {
      const auto optionsDescription = optionDescriptions(*propertyDefinition);

      const auto propertyHasDocs = !propertyDefinition->longDescription().empty()
                                   || !propertyDefinition->shortDescription().empty()
                                   || !optionsDescription.isEmpty();

      if (propertyHasDocs)
      {
        // e.g. "Property "delay" (Attenuation formula)", in bold
        {
          auto title =
            tr("Property \"%1\"").arg(QString::fromStdString(propertyDefinition->key()));
          if (!propertyDefinition->shortDescription().empty())
          {
            title += tr(" (%1)").arg(
              QString::fromStdString(propertyDefinition->shortDescription()));
          }

          m_documentationText->setCurrentCharFormat(boldFormat);
          m_documentationText->append(title);
          m_documentationText->setCurrentCharFormat(normalFormat);
        }

        if (!propertyDefinition->longDescription().empty())
        {
          m_documentationText->append("");
          m_documentationText->append(propertyDefinition->longDescription().c_str());
        }

        if (!optionsDescription.isEmpty())
        {
          m_documentationText->append("");
          m_documentationText->append("Options:");
          m_documentationText->append(optionsDescription);
        }
      }
    }
    // 对于文件类型属性，但没有属性定义时添加通用描述
    else if (isFileBrowserProperty(propertyKey)) {
      m_documentationText->setCurrentCharFormat(boldFormat);
      m_documentationText->append(tr("Property \"%1\"").arg(QString::fromStdString(propertyKey)));
      m_documentationText->setCurrentCharFormat(normalFormat);
      
      m_documentationText->append("");
      
      // 获取属性定义以检查短描述
      const auto* propDef = mdl::selectPropertyDefinition(propertyKey, document->allSelectedEntityNodes());
      
      if (propDef) {
        const std::string& desc = propDef->shortDescription();
        
        // 根据短描述中的标记确定文件类型
        if (desc.find("<sound>") != std::string::npos || 
            desc.find("WAV") != std::string::npos || 
            desc.find(".wav") != std::string::npos) {
          m_documentationText->append("Expected value: Sound file path (*.wav)");
        } else if (desc.find("<sprite>") != std::string::npos || 
                  desc.find("Sprite Name") != std::string::npos) {
          m_documentationText->append("Expected value: Sprite file path (*.spr)");
        } else if (desc.find("<model>") != std::string::npos || 
                  (desc.find("Model") != std::string::npos && 
                   desc.find("Sprite") == std::string::npos)) {
          m_documentationText->append("Expected value: Model file path (*.mdl)");
        } else if (desc.find("Model / Sprite") != std::string::npos) {
          m_documentationText->append("Expected value: Model file path (*.mdl) or Sprite file path (*.spr)");
        } else {
          // 回退到基于属性名称的检测，仅在无法从描述确定类型时使用
          if (propertyKey.find("model") != std::string::npos) {
            m_documentationText->append("Expected value: Model file path (*.mdl)");
          } else if (propertyKey.find("sound") != std::string::npos) {
            m_documentationText->append("Expected value: Sound file path (*.wav)");
          } else if (propertyKey.find("sprite") != std::string::npos) {
            m_documentationText->append("Expected value: Sprite file path (*.spr)");
          } else {
            m_documentationText->append("File path value. Use the browser button to select a file.");
          }
        }
      } else {
        // 没有属性定义时，回退到基于名称的检测
        if (propertyKey.find("model") != std::string::npos) {
          m_documentationText->append("Expected value: Model file path (*.mdl)");
        } else if (propertyKey.find("sound") != std::string::npos) {
          m_documentationText->append("Expected value: Sound file path (*.wav)");
        } else if (propertyKey.find("sprite") != std::string::npos) {
          m_documentationText->append("Expected value: Sprite file path (*.spr)");
        } else {
          m_documentationText->append("File path value. Use the browser button to select a file.");
        }
      }
    }

    // add class description, if available
    if (!entityDefinition->description().empty())
    {
      // add space after property text
      if (!m_documentationText->document()->isEmpty())
      {
        m_documentationText->append("");
      }

      // e.g. "Class "func_door"", in bold
      {
        m_documentationText->setCurrentCharFormat(boldFormat);
        m_documentationText->append(
          tr("Class \"%1\"").arg(QString::fromStdString(entityDefinition->name())));
        m_documentationText->setCurrentCharFormat(normalFormat);
      }

      m_documentationText->append("");
      m_documentationText->append(entityDefinition->description().c_str());
      m_documentationText->append("");
    }
  }

  // Scroll to the top
  m_documentationText->moveCursor(QTextCursor::MoveOperation::Start);
}

void EntityPropertyEditor::createGui(std::weak_ptr<MapDocument> document)
{
  m_splitter = new Splitter{Qt::Vertical};

  // This class has since been renamed, but we leave the old name so as not to reset the
  // users' view settings.
  m_splitter->setObjectName("EntityAttributeEditor_Splitter");

  m_propertyGrid = new EntityPropertyGrid{document};
  m_smartEditorManager = new SmartPropertyEditorManager{document};
  m_documentationText = new QTextEdit{};
  m_documentationText->setReadOnly(true);

  m_splitter->addWidget(m_propertyGrid);
  m_splitter->addWidget(m_smartEditorManager);
  m_splitter->addWidget(m_documentationText);

  // give most space to the property grid
  m_splitter->setSizes({1'000'000, 1, 1});

  // NOTE: this should be done before setChildrenCollapsible() and setMinimumSize()
  // otherwise it can override them.
  restoreWindowState(m_splitter);

  // should have enough vertical space for at least one row
  m_propertyGrid->setMinimumSize(100, 100);
  m_smartEditorManager->setMinimumSize(100, 80);
  m_documentationText->setMinimumSize(100, 50);
  updateMinimumSize();

  // don't allow the user to collapse the panels, it's hard to see them
  m_splitter->setChildrenCollapsible(false);

  // resize only the property grid when the container resizes
  m_splitter->setStretchFactor(0, 1);
  m_splitter->setStretchFactor(1, 0);
  m_splitter->setStretchFactor(2, 0);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_splitter, 1);
  setLayout(layout);

  connect(
    m_propertyGrid,
    &EntityPropertyGrid::currentRowChanged,
    this,
    &EntityPropertyEditor::OnCurrentRowChanged);
}

void EntityPropertyEditor::updateMinimumSize()
{
  auto size = QSize{};
  size.setWidth(m_propertyGrid->minimumWidth());
  size.setHeight(m_propertyGrid->minimumHeight());

  size.setWidth(std::max(size.width(), m_smartEditorManager->minimumSizeHint().width()));
  size.setHeight(size.height() + m_smartEditorManager->minimumSizeHint().height());

  size.setWidth(std::max(size.width(), m_documentationText->minimumSizeHint().width()));
  size.setHeight(size.height() + m_documentationText->minimumSizeHint().height());

  setMinimumSize(size);
  updateGeometry();
}

} // namespace tb::ui
