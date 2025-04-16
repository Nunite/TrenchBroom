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

#include "LanguagePreferencePane.h"

#include <QApplication>
#include <QButtonGroup>
#include <QLabel>
#include <QMessageBox>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QGroupBox>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "ui/QtUtils.h"

namespace tb::ui
{

LanguagePreferencePane::LanguagePreferencePane(QWidget* parent)
  : PreferencePane{parent}
{
  createGui();
  updateControls();
}

void LanguagePreferencePane::createGui()
{
  auto* langLabel = new QLabel(tr("界面语言 (UI Language)"));
  langLabel->setToolTip(tr("选择应用程序界面显示的语言，更改后需要重启应用程序生效"));

  m_englishRadioButton = new QRadioButton(Preferences::languageEnglish());
  m_chineseRadioButton = new QRadioButton(Preferences::languageChinese());

  m_languageButtonGroup = new QButtonGroup(this);
  m_languageButtonGroup->addButton(m_englishRadioButton, 0);
  m_languageButtonGroup->addButton(m_chineseRadioButton, 1);

  connect(m_englishRadioButton, &QRadioButton::clicked, this, [this]() {
    auto& prefs = PreferenceManager::instance();
    prefs.set(Preferences::Language, Preferences::languageEnglish());
    
    // 发出语言变更信号
    emit languageChanged();
    
    // 显示需要重启的提示
    QMessageBox::information(
      this,
      tr("需要重启"),
      tr("语言设置将在重启应用程序后生效"),
      QMessageBox::Ok);
  });
  
  connect(m_chineseRadioButton, &QRadioButton::clicked, this, [this]() {
    auto& prefs = PreferenceManager::instance();
    prefs.set(Preferences::Language, Preferences::languageChinese());
    
    // 发出语言变更信号
    emit languageChanged();
    
    // 显示需要重启的提示
    QMessageBox::information(
      this,
      tr("需要重启"),
      tr("语言设置将在重启应用程序后生效"),
      QMessageBox::Ok);
  });

  QVBoxLayout* languageLayout = new QVBoxLayout();
  languageLayout->setContentsMargins(0, 0, 0, 0);
  languageLayout->addWidget(langLabel);
  languageLayout->addWidget(m_englishRadioButton);
  languageLayout->addWidget(m_chineseRadioButton);
  
  QGroupBox* languageGroupBox = new QGroupBox(tr("语言"));
  languageGroupBox->setLayout(languageLayout);
  
  QVBoxLayout* layout = new QVBoxLayout();
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(languageGroupBox);
  layout->addStretch(1);
  setLayout(layout);
}

bool LanguagePreferencePane::canResetToDefaults()
{
  return true;
}

void LanguagePreferencePane::doResetToDefaults()
{
  auto& prefs = PreferenceManager::instance();
  prefs.resetToDefault(Preferences::Language);

  updateControls();
}

void LanguagePreferencePane::updateControls()
{
  auto& prefs = PreferenceManager::instance();
  const auto& language = prefs.get(Preferences::Language);

  if (language == Preferences::languageEnglish()) {
    m_englishRadioButton->setChecked(true);
  } else {
    m_chineseRadioButton->setChecked(true);
  }
}

bool LanguagePreferencePane::validate()
{
  return true;
}

} // namespace tb::ui 