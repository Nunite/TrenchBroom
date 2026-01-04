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

#include "SmartColorEditor.h"

#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QScrollArea>
#include <QCheckBox>
#include <QSlider>
#include <QInputDialog>
#include <QEvent>
#include <sstream>

#include "Color.h"
#include "mdl/ColorRange.h"
#include "mdl/EntityColor.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "mdl/WorldNode.h"
#include "ui/BorderLine.h"
#include "ui/ColorButton.h"
#include "ui/ColorTable.h"
#include "ui/QtUtils.h"
#include "ui/ViewConstants.h"

#include "kdl/overload.h"
#include "kdl/vector_set.h"

namespace tb::ui
{
namespace
{

std::vector<std::string> splitString(const std::string& str) {
  std::istringstream stream(str);
  std::vector<std::string> result;
  std::string part;
  
  while (stream >> part) {
    result.push_back(part);
  }
  
  return result;
}

template <typename Node>
std::vector<QColor> collectColors(
  const std::vector<Node*>& nodes, const std::string& propertyKey)
{
  const auto cmp = [](const auto& lhs, const auto& rhs) {
    const auto lr = float(lhs.red()) / 255.0f;
    const auto lg = float(lhs.green()) / 255.0f;
    const auto lb = float(lhs.blue()) / 255.0f;
    const auto rr = float(rhs.red()) / 255.0f;
    const auto rg = float(rhs.green()) / 255.0f;
    const auto rb = float(rhs.blue()) / 255.0f;

    float lh, ls, lbr, rh, rs, rbr;
    Color::rgbToHSB(lr, lg, lb, lh, ls, lbr);
    Color::rgbToHSB(rr, rg, rb, rh, rs, rbr);

    return lh < rh     ? true
           : lh > rh   ? false
           : ls < rs   ? true
           : ls > rs   ? false
           : lbr < rbr ? true
                       : false;
  };

  auto colors = kdl::vector_set<QColor, decltype(cmp)>{cmp};

  const auto visitEntityNode = [&](const auto* node) {
    if (const auto* value = node->entity().property(propertyKey))
    {
      QColor color = toQColor(mdl::parseEntityColor(*value));
      
      auto components = splitString(*value);
      if (components.size() >= 4) {
        bool ok = false;
        int alpha = QString::fromStdString(components[3]).toInt(&ok);
        if (ok) {
          color.setAlpha(alpha);
        }
      }
      
      colors.insert(color);
    }
  };

  for (const auto* node : nodes)
  {
    node->accept(kdl::overload(
      [&](auto&& thisLambda, const mdl::WorldNode* world) {
        world->visitChildren(thisLambda);
        visitEntityNode(world);
      },
      [](auto&& thisLambda, const mdl::LayerNode* layer) {
        layer->visitChildren(thisLambda);
      },
      [](auto&& thisLambda, const mdl::GroupNode* group) {
        group->visitChildren(thisLambda);
      },
      [&](const mdl::EntityNode* entity) { visitEntityNode(entity); },
      [](const mdl::BrushNode*) {},
      [](const mdl::PatchNode*) {}));
  }

  return colors.get_data();
}

} // namespace

SmartColorEditor::SmartColorEditor(MapDocument& document, QWidget* parent)
  : SmartPropertyEditor{document, parent}
  , m_brightnessEnabled(true)  {
  createGui();
}

void SmartColorEditor::setBrightnessEnabled(bool enabled)
{
  if (m_brightnessEnabled != enabled) {
    m_brightnessEnabled = enabled;
    updateGuiState();
  }
}

void SmartColorEditor::updateGuiState()
{
    if (m_alphaSlider) {
    m_alphaSlider->setVisible(m_brightnessEnabled);
  }
  
  if (m_alphaLabel) {
    m_alphaLabel->setVisible(m_brightnessEnabled);
  }
  
    QList<QLabel*> labels = findChildren<QLabel*>();
  for (QLabel* label : labels) {
    if (label->text().contains("Brightness")) {
      label->setVisible(m_brightnessEnabled);
      break;
    }
  }
  
    for (QLabel* label : labels) {
    if (label->text().contains("Double-click")) {
      label->setVisible(m_brightnessEnabled);
      break;
    }
  }
}

void SmartColorEditor::createGui()
{
    m_floatRadio = nullptr;
  m_byteRadio = nullptr;
  m_colorPicker = nullptr; 
  m_colorHistory = nullptr;
  m_alphaCheckBox = nullptr;
  m_alphaSlider = nullptr;
  m_alphaLabel = nullptr;
  m_currentColor = QColor(0, 0, 0); 
    m_colorPicker = new ColorButton{};
  m_colorPicker->setMinimumSize(40, 40);   
    auto* colorLabel = new QLabel{tr("Color:")};
  makeEmphasized(colorLabel);
  
    auto* alphaTxt = new QLabel{tr("Brightness:")};
  makeEmphasized(alphaTxt);
  
    m_alphaCheckBox = new QCheckBox{tr("Hidden Control")};
  m_alphaCheckBox->setVisible(false);
  m_colorHistory = new ColorTable{ColorHistoryCellSize};
  m_colorHistory->setVisible(false);
  
    m_alphaSlider = new QSlider{Qt::Horizontal};
  m_alphaSlider->setRange(0, 1000);
  m_alphaSlider->setValue(200);   m_alphaSlider->setEnabled(true);
  
    m_alphaLabel = new QLabel{tr("200")};
  m_alphaLabel->setMinimumWidth(40);   m_alphaLabel->setCursor(Qt::PointingHandCursor);   
    m_alphaLabel->installEventFilter(this);
  
    auto* tipLabel = new QLabel{tr("Double-click to edit brightness value")};
  QFont tipFont = tipLabel->font();
  tipFont.setPointSize(tipFont.pointSize());   tipLabel->setFont(tipFont);
  tipLabel->setStyleSheet("color: white;");
  
    
    auto* currentColorLayout = new QHBoxLayout{};
  currentColorLayout->setContentsMargins(0, 0, 0, 0);
  currentColorLayout->addWidget(colorLabel);
  currentColorLayout->addWidget(m_colorPicker);
  currentColorLayout->addStretch(1);
  
    auto* alphaLayout = new QHBoxLayout{};
  alphaLayout->setContentsMargins(0, 0, 0, 0);
  alphaLayout->addWidget(alphaTxt);
  alphaLayout->addWidget(m_alphaSlider, 1);
  alphaLayout->addWidget(m_alphaLabel);
  
    auto* tipLayout = new QHBoxLayout{};
  tipLayout->setContentsMargins(0, 0, 0, 5);
  tipLayout->addStretch(1);
  tipLayout->addWidget(tipLabel);
  tipLayout->addStretch(1);
  
    auto* mainLayout = new QVBoxLayout{};
  mainLayout->setContentsMargins(10, 10, 10, 10);
  mainLayout->setSpacing(15);
  mainLayout->addLayout(currentColorLayout);
  mainLayout->addLayout(alphaLayout);
  mainLayout->addLayout(tipLayout);   mainLayout->addStretch(1);
  
  setLayout(mainLayout);

    m_floatRadio = new QRadioButton{};
  m_byteRadio = new QRadioButton{};
  m_floatRadio->setVisible(false);
  m_byteRadio->setVisible(false);
  m_byteRadio->setChecked(true); 
  connect(
    m_colorPicker,
    &ColorButton::colorChangedByUser,
    this,
    &SmartColorEditor::colorPickerChanged);
  
    connect(
    m_colorHistory,
    &ColorTable::colorTableSelected,
    this,
    &SmartColorEditor::colorTableSelected);
    
  connect(
    m_alphaSlider,
    &QSlider::valueChanged,
    this,
    &SmartColorEditor::alphaSliderChanged);
    
    updateGuiState();
}

void SmartColorEditor::doUpdateVisual(const std::vector<mdl::EntityNodeBase*>& nodes)
{
    updateColorHistory();
  updateAlphaControls(nodes);
  updateGuiState(); }

void SmartColorEditor::updateColorHistory()
{
      m_colorHistory->setColors(collectColors(std::vector{map().world()}, propertyKey()));

    const auto selectedColors =
    collectColors(map().selection().allEntities(), propertyKey());
    
    m_colorHistory->setSelection(selectedColors);
  
    m_currentColor = !selectedColors.empty() ? selectedColors.back() : QColor(Qt::black);
  
    if (!map().selection().allEntities().empty()) {
    const auto* entity = map().selection().allEntities().front();
    if (const auto* value = entity->entity().property(propertyKey())) {
            auto components = splitString(*value);
      if (components.size() >= 3) {
                bool rOk = false, gOk = false, bOk = false;
        int r = QString::fromStdString(components[0]).toInt(&rOk);
        int g = QString::fromStdString(components[1]).toInt(&gOk);
        int b = QString::fromStdString(components[2]).toInt(&bOk);
        
        if (rOk && gOk && bOk) {
                    m_currentColor = QColor(r, g, b);
        }
      }
    }
  }
  
    m_colorPicker->setColor(m_currentColor);
}

void SmartColorEditor::updateAlphaControls(const std::vector<mdl::EntityNodeBase*>& nodes)
{
    m_currentAlpha = 200;
  m_hasAlpha = true;   
    if (!nodes.empty()) {
    if (const auto* value = nodes.front()->entity().property(propertyKey())) {
      auto components = splitString(*value);
      if (components.size() >= 4) {
        bool ok = false;
        int alpha = QString::fromStdString(components[3]).toInt(&ok);
        if (ok) {
          m_currentAlpha = alpha;
        }
      }
    }
  }
  
    m_alphaSlider->setValue(m_currentAlpha);
  m_alphaLabel->setText(QString::number(m_currentAlpha));
}

void SmartColorEditor::alphaSliderChanged(int value)
{
  m_currentAlpha = value;
  m_alphaLabel->setText(QString::number(value));
  
      setColor(m_currentColor);
}

void SmartColorEditor::setColor(const QColor& color)
{
    std::string value = mdl::entityColorAsString(fromQColor(color), mdl::ColorRange::Byte);
  
    if (m_brightnessEnabled) {
    value += " " + std::to_string(m_currentAlpha);
  }
  
  setEntityProperty(map(), propertyKey(), value);
}
void SmartColorEditor::colorPickerChanged(const QColor& color)
{
    m_currentColor = color;
  setColor(color);
}

void SmartColorEditor::colorTableSelected(QColor color)
{
    m_currentColor = color;
  setColor(color);
}

void SmartColorEditor::floatRangeRadioButtonClicked()
{
  }

void SmartColorEditor::byteRangeRadioButtonClicked()
{
  }

void SmartColorEditor::alphaCheckBoxToggled(bool checked)
{
    m_hasAlpha = checked;
}

bool SmartColorEditor::eventFilter(QObject* obj, QEvent* event)
{
  if (obj == m_alphaLabel && event->type() == QEvent::MouseButtonDblClick) {
        bool ok;
    int newValue = QInputDialog::getInt(
      this, 
      tr("Enter Brightness Value"), 
      tr("Brightness (0-10000):"), 
      m_currentAlpha,       0,                    10000,                1,                    &ok
    );
    
    if (ok) {
            m_currentAlpha = newValue;
      m_alphaLabel->setText(QString::number(newValue));
      
            m_alphaSlider->setValue(std::min(newValue, 1000));
      
            setColor(m_currentColor);
    }
    
    return true;   }
  
    return QWidget::eventFilter(obj, event);
}

} // namespace tb::ui

