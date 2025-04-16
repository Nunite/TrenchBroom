/*
 Copyright (C) 2023 Kristian Duske

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

#include "ui/PreferencePane.h"

class QButtonGroup;
class QRadioButton;

namespace tb::ui
{

class LanguagePreferencePane : public PreferencePane
{
  Q_OBJECT
private:
  QRadioButton* m_englishRadioButton;
  QRadioButton* m_chineseRadioButton;
  QButtonGroup* m_languageButtonGroup;

signals:
  /**
   * 当语言设置更改时发出信号
   */
  void languageChanged();

public:
  explicit LanguagePreferencePane(QWidget* parent = nullptr);

private:
  void createGui();
  bool canResetToDefaults() override;
  void doResetToDefaults() override;
  void updateControls() override;
  bool validate() override;
};

} // namespace tb::ui 