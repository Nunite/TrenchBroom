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
  int m_currentAlpha = 255;
  bool m_hasAlpha = true;

public:
  explicit SmartColorEditor(
    std::weak_ptr<MapDocument> document, QWidget* parent = nullptr);

private:
  void createGui();
  void doUpdateVisual(const std::vector<mdl::EntityNodeBase*>& nodes) override;

  void updateColorRange(const std::vector<mdl::EntityNodeBase*>& nodes);
  void updateColorHistory();
  void updateAlphaControls(const std::vector<mdl::EntityNodeBase*>& nodes);

  void setColor(const QColor& wxColor) const;

  void floatRangeRadioButtonClicked();
  void byteRangeRadioButtonClicked();
  void colorPickerChanged(const QColor& color);
  void colorTableSelected(QColor color);
  void alphaCheckBoxToggled(bool checked);
  void alphaSliderChanged(int value);
};

} // namespace tb::ui
