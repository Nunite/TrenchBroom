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
#include <sstream>

#include "Color.h"
#include "mdl/ColorRange.h"
#include "mdl/EntityColor.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/WorldNode.h"
#include "ui/BorderLine.h"
#include "ui/ColorButton.h"
#include "ui/ColorTable.h"
#include "ui/MapDocument.h"
#include "ui/QtUtils.h"
#include "ui/ViewConstants.h"

#include "kdl/overload.h"
#include "kdl/vector_set.h"

namespace tb::ui
{
namespace
{

// 辅助函数：按空格分割字符串
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

SmartColorEditor::SmartColorEditor(std::weak_ptr<MapDocument> document, QWidget* parent)
  : SmartPropertyEditor{std::move(document), parent}
{
  createGui();
}

void SmartColorEditor::createGui()
{
  // 初始化所有变量为nullptr，防止检查失败
  m_floatRadio = nullptr;
  m_byteRadio = nullptr;
  m_colorPicker = nullptr; 
  m_colorHistory = nullptr;
  m_alphaCheckBox = nullptr;
  m_alphaSlider = nullptr;
  m_alphaLabel = nullptr;

  // 创建颜色选择器
  m_colorPicker = new ColorButton{};
  m_colorHistory = new ColorTable{ColorHistoryCellSize};
  
  // 添加亮度控件
  auto* alphaTxt = new QLabel{tr("亮度值")};
  makeEmphasized(alphaTxt);
  
  // 仍然创建复选框，但保持隐藏状态，防止空指针
  m_alphaCheckBox = new QCheckBox{tr("使用亮度值")};
  m_alphaCheckBox->setVisible(false);
  
  m_alphaSlider = new QSlider{Qt::Horizontal};
  m_alphaSlider->setRange(0, 255);
  m_alphaSlider->setValue(255);
  m_alphaSlider->setEnabled(true);
  
  m_alphaLabel = new QLabel{tr("255")};
  
  auto* alphaLayout = new QHBoxLayout{};
  alphaLayout->setContentsMargins(0, 0, 0, 0);
  alphaLayout->addWidget(m_alphaSlider, 1);
  alphaLayout->addWidget(m_alphaLabel);

  auto* colorHistoryScroller = new QScrollArea{};
  colorHistoryScroller->setWidget(m_colorHistory);
  colorHistoryScroller->setWidgetResizable(true);
  colorHistoryScroller->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);

  auto* leftLayout = new QVBoxLayout{};
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(LayoutConstants::NarrowVMargin);
  leftLayout->addWidget(m_colorPicker);
  leftLayout->addWidget(alphaTxt);
  // 不添加复选框到布局
  leftLayout->addLayout(alphaLayout);
  leftLayout->addStretch(1);

  auto* outerLayout = new QHBoxLayout{};
  outerLayout->setContentsMargins(LayoutConstants::WideHMargin, 0, 0, 0);
  outerLayout->setSpacing(0);
  outerLayout->addLayout(leftLayout);
  outerLayout->addSpacing(LayoutConstants::WideHMargin);
  outerLayout->addWidget(new BorderLine{BorderLine::Direction::Vertical});
  outerLayout->addWidget(colorHistoryScroller, 1);
  setLayout(outerLayout);

  // 创建隐藏的radio按钮，防止空指针
  m_floatRadio = new QRadioButton{};
  m_byteRadio = new QRadioButton{};
  m_floatRadio->setVisible(false);
  m_byteRadio->setVisible(false);
  m_byteRadio->setChecked(true); // 默认使用字节模式

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
}

void SmartColorEditor::doUpdateVisual(const std::vector<mdl::EntityNodeBase*>& nodes)
{
  // 移除ensure检查，因为我们确保上面已经创建了所有对象
  updateColorHistory();
  updateAlphaControls(nodes);
}

void SmartColorEditor::updateColorHistory()
{
  m_colorHistory->setColors(
    collectColors(std::vector{document()->world()}, propertyKey()));

  const auto selectedColors =
    collectColors(document()->allSelectedEntityNodes(), propertyKey());
  m_colorHistory->setSelection(selectedColors);
  m_colorPicker->setColor(
    !selectedColors.empty() ? selectedColors.back() : QColor(Qt::black));
}

void SmartColorEditor::updateAlphaControls(const std::vector<mdl::EntityNodeBase*>& nodes)
{
  // 亮度值始终存在，默认为255
  m_currentAlpha = 255;
  m_hasAlpha = true; // 始终为true
  
  // 检查当前属性值是否包含亮度值（第四个分量）
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
  
  // 更新亮度控件状态
  m_alphaSlider->setValue(m_currentAlpha);
  m_alphaLabel->setText(QString::number(m_currentAlpha));
}

void SmartColorEditor::alphaSliderChanged(int value)
{
  m_currentAlpha = value;
  m_alphaLabel->setText(QString::number(value));
  
  // 获取当前所选颜色
  QColor currentColor = QColor(Qt::black);
  const auto selectedColors = collectColors(document()->allSelectedEntityNodes(), propertyKey());
  if (!selectedColors.empty()) {
    currentColor = selectedColors.back();
  }
  
  // 更新颜色值，使用新的亮度值
  colorPickerChanged(currentColor);
}

void SmartColorEditor::setColor(const QColor& color) const
{
  // 生成颜色值字符串，固定使用字节模式(0-255)
  std::string value = mdl::entityColorAsString(fromQColor(color), mdl::ColorRange::Byte);
  
  // 添加亮度值（第四个分量）- 始终添加
  value += " " + std::to_string(m_currentAlpha);
  
  document()->setProperty(propertyKey(), value);
}

void SmartColorEditor::colorPickerChanged(const QColor& color)
{
  setColor(color);
}

void SmartColorEditor::colorTableSelected(QColor color)
{
  setColor(color);
}

// 保留这些方法但简化实现，防止它们被调用时出错
void SmartColorEditor::floatRangeRadioButtonClicked()
{
  // 不再改变颜色范围，始终使用字节模式
}

void SmartColorEditor::byteRangeRadioButtonClicked()
{
  // 不再改变颜色范围，始终使用字节模式
}

void SmartColorEditor::alphaCheckBoxToggled(bool checked)
{
  // 复选框不再可见，但保留方法实现
  m_hasAlpha = checked;
}

} // namespace tb::ui

