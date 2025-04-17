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
  m_currentColor = QColor(0, 0, 0); // 初始化当前颜色为黑色

  // 创建颜色选择器 - 颜色块
  m_colorPicker = new ColorButton{};
  m_colorPicker->setMinimumSize(40, 40); // 增大颜色选择按钮尺寸
  
  // 添加标签以明确颜色块的用途 - 使用英文
  auto* colorLabel = new QLabel{tr("Color:")};
  makeEmphasized(colorLabel);
  
  // 添加亮度控件 - 使用英文
  auto* alphaTxt = new QLabel{tr("Brightness:")};
  makeEmphasized(alphaTxt);
  
  // 隐藏的控件，但保持变量以支持现有代码
  m_alphaCheckBox = new QCheckBox{tr("Hidden Control")};
  m_alphaCheckBox->setVisible(false);
  m_colorHistory = new ColorTable{ColorHistoryCellSize};
  m_colorHistory->setVisible(false);
  
  // 设置亮度滑块范围为0-1000
  m_alphaSlider = new QSlider{Qt::Horizontal};
  m_alphaSlider->setRange(0, 1000);
  m_alphaSlider->setValue(200); // 默认亮度值
  m_alphaSlider->setEnabled(true);
  
  // 创建可编辑的亮度值标签 - 使用英文提示
  m_alphaLabel = new QLabel{tr("200")};
  m_alphaLabel->setMinimumWidth(40); // 为数值预留足够空间
  m_alphaLabel->setCursor(Qt::PointingHandCursor); // 设置鼠标样式以提示可点击
  
  // 安装事件过滤器以处理双击事件
  m_alphaLabel->installEventFilter(this);
  
  // 创建小字体提示标签
  auto* tipLabel = new QLabel{tr("Double-click to edit brightness value")};
  QFont tipFont = tipLabel->font();
  tipFont.setPointSize(tipFont.pointSize()); // 减小字体大小
  tipLabel->setFont(tipFont);
  tipLabel->setStyleSheet("color: white;");
  
  // 创建简化的布局
  
  // 当前颜色布局
  auto* currentColorLayout = new QHBoxLayout{};
  currentColorLayout->setContentsMargins(0, 0, 0, 0);
  currentColorLayout->addWidget(colorLabel);
  currentColorLayout->addWidget(m_colorPicker);
  currentColorLayout->addStretch(1);
  
  // 亮度值布局
  auto* alphaLayout = new QHBoxLayout{};
  alphaLayout->setContentsMargins(0, 0, 0, 0);
  alphaLayout->addWidget(alphaTxt);
  alphaLayout->addWidget(m_alphaSlider, 1);
  alphaLayout->addWidget(m_alphaLabel);
  
  // 提示标签布局（居中对齐）
  auto* tipLayout = new QHBoxLayout{};
  tipLayout->setContentsMargins(0, 0, 0, 5);
  tipLayout->addStretch(1);
  tipLayout->addWidget(tipLabel);
  tipLayout->addStretch(1);
  
  // 主布局 - 只有左侧面板
  auto* mainLayout = new QVBoxLayout{};
  mainLayout->setContentsMargins(10, 10, 10, 10);
  mainLayout->setSpacing(15);
  mainLayout->addLayout(currentColorLayout);
  mainLayout->addLayout(alphaLayout);
  mainLayout->addLayout(tipLayout); // 添加提示文本布局
  mainLayout->addStretch(1);
  
  setLayout(mainLayout);

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
  
  // 仍然保留这个连接，只是控件被隐藏了
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
  // 虽然颜色历史被隐藏，但仍然保持其更新
  // 这样可以确保代码的兼容性，避免需要修改大量依赖它的代码
  m_colorHistory->setColors(
    collectColors(std::vector{document()->world()}, propertyKey()));

  // 获取当前选中实体的颜色
  const auto selectedColors =
    collectColors(document()->allSelectedEntityNodes(), propertyKey());
    
  // 更新历史中的选择
  m_colorHistory->setSelection(selectedColors);
  
  // 确保颜色选择器显示当前选中的颜色
  m_currentColor = !selectedColors.empty() ? selectedColors.back() : QColor(Qt::black);
  
  // 如果当前选中的属性值包含RGB值，解析并设置
  if (!document()->allSelectedEntityNodes().empty()) {
    const auto* node = document()->allSelectedEntityNodes().front();
    if (const auto* value = node->entity().property(propertyKey())) {
      // 解析RGB值
      auto components = splitString(*value);
      if (components.size() >= 3) {
        // 尝试解析RGB值
        bool rOk = false, gOk = false, bOk = false;
        int r = QString::fromStdString(components[0]).toInt(&rOk);
        int g = QString::fromStdString(components[1]).toInt(&gOk);
        int b = QString::fromStdString(components[2]).toInt(&bOk);
        
        if (rOk && gOk && bOk) {
          // 设置为当前RGB值
          m_currentColor = QColor(r, g, b);
        }
      }
    }
  }
  
  // 设置当前颜色
  m_colorPicker->setColor(m_currentColor);
}

void SmartColorEditor::updateAlphaControls(const std::vector<mdl::EntityNodeBase*>& nodes)
{
  // 亮度值始终存在，默认为200
  m_currentAlpha = 200;
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
  
  // 使用存储的当前颜色，不再尝试从ColorButton获取
  // 更新颜色值，使用新的亮度值
  setColor(m_currentColor);
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
  // 保存当前选择的颜色
  m_currentColor = color;
  setColor(color);
}

void SmartColorEditor::colorTableSelected(QColor color)
{
  // 保存当前选择的颜色
  m_currentColor = color;
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

// 添加事件过滤器处理双击事件 - 使用英文对话框
bool SmartColorEditor::eventFilter(QObject* obj, QEvent* event)
{
  if (obj == m_alphaLabel && event->type() == QEvent::MouseButtonDblClick) {
    // 打开输入对话框 - 使用英文
    bool ok;
    int newValue = QInputDialog::getInt(
      this, 
      tr("Enter Brightness Value"), 
      tr("Brightness (0-10000):"), 
      m_currentAlpha, // 当前值
      0,              // 最小值
      10000,          // 最大值提高到10000
      1,              // 步长
      &ok
    );
    
    if (ok) {
      // 用户确认了新值
      m_currentAlpha = newValue;
      m_alphaLabel->setText(QString::number(newValue));
      
      // 滑块最大值仍为1000，当输入值超过1000时，滑块保持在最大位置
      m_alphaSlider->setValue(std::min(newValue, 1000));
      
      // 更新颜色值
      setColor(m_currentColor);
    }
    
    return true; // 事件已处理
  }
  
  // 对于其他事件，让基类处理
  return QWidget::eventFilter(obj, event);
}

} // namespace tb::ui

