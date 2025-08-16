//Added by Lws

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
  auto* langLabel = new QLabel(tr("UI Language"));
  langLabel->setToolTip(tr("Select the display language for the application interface. Changes will take effect after restarting the application"));

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
    showRestartRequiredMessage();
  });
  
  connect(m_chineseRadioButton, &QRadioButton::clicked, this, [this]() {
    auto& prefs = PreferenceManager::instance();
    prefs.set(Preferences::Language, Preferences::languageChinese());
    
    // 发出语言变更信号
    emit languageChanged();
    
    // 显示需要重启的提示
    showRestartRequiredMessage();
  });

  auto* languageLayout = new QVBoxLayout();
  languageLayout->setContentsMargins(0, 0, 0, 0);
  languageLayout->addWidget(langLabel);
  languageLayout->addWidget(m_englishRadioButton);
  languageLayout->addWidget(m_chineseRadioButton);
  
  auto* languageGroupBox = new QGroupBox(tr("Language"));

  languageGroupBox->setLayout(languageLayout);
  
  auto* layout = new QVBoxLayout();
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

void LanguagePreferencePane::showRestartRequiredMessage()
{
  QMessageBox::information(
    this,
    tr("Restart Required"),
    tr("Language settings will take effect after restarting the application"),
    QMessageBox::Ok);
}

} // namespace tb::ui