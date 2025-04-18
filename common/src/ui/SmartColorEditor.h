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

#pragma once

#include "ui/SmartPropertyEditor.h"

#include <memory>
#include <vector>

class QColor;
class QWidget;
class QPushButton;
class QRadioButton;
class QSlider;
class QCheckBox;
class QLabel;

namespace tb::ui
{
class ColorButton;
class ColorTable;
class MapDocument;

class SmartColorEditor : public SmartPropertyEditor
{
  Q_OBJECT
private:
  static const size_t ColorHistoryCellSize = 15;
  using wxColorList = std::vector<QColor>;

  QRadioButton* m_floatRadio = nullptr;
  QRadioButton* m_byteRadio = nullptr;
  ColorButton* m_colorPicker = nullptr;
  ColorTable* m_colorHistory = nullptr;
  
  // 亮度相关控件
  QCheckBox* m_alphaCheckBox = nullptr;
  QSlider* m_alphaSlider = nullptr;
  QLabel* m_alphaLabel = nullptr;
  int m_currentAlpha = 200;
  bool m_hasAlpha = true;
  
  // 是否启用亮度控制
  bool m_brightnessEnabled = true;
  
  // 添加成员变量来跟踪当前颜色
  QColor m_currentColor;

public:
  explicit SmartColorEditor(
    std::weak_ptr<MapDocument> document, QWidget* parent = nullptr);
    
  // 设置是否启用亮度控制
  void setBrightnessEnabled(bool enabled);

protected:
  // 添加事件过滤器以支持双击编辑功能
  bool eventFilter(QObject* obj, QEvent* event) override;

private:
  void createGui();
  void doUpdateVisual(const std::vector<mdl::EntityNodeBase*>& nodes) override;

  void updateColorRange(const std::vector<mdl::EntityNodeBase*>& nodes);
  void updateColorHistory();
  void updateAlphaControls(const std::vector<mdl::EntityNodeBase*>& nodes);
  void updateGuiState();  // 更新控件的可见性

  void setColor(const QColor& wxColor) const;

  void floatRangeRadioButtonClicked();
  void byteRangeRadioButtonClicked();
  void colorPickerChanged(const QColor& color);
  void colorTableSelected(QColor color);
  void alphaCheckBoxToggled(bool checked);
  void alphaSliderChanged(int value);
};

} // namespace tb::ui
