#include "MiscPreferencePane.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QRadioButton>
#include <QVBoxLayout>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "ui/QtUtils.h"

namespace tb::ui
{

MiscPreferencePane::MiscPreferencePane(QWidget* parent)
  : PreferencePane{parent}
{
  createGui();
  updateControls();
}

void MiscPreferencePane::createGui()
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
    emit languageChanged();
    showRestartRequiredMessage();
  });

  connect(m_chineseRadioButton, &QRadioButton::clicked, this, [this]() {
    auto& prefs = PreferenceManager::instance();
    prefs.set(Preferences::Language, Preferences::languageChinese());
    emit languageChanged();
    showRestartRequiredMessage();
  });

  auto* languageLayout = new QVBoxLayout();
  languageLayout->setContentsMargins(0, 0, 0, 0);
  languageLayout->addWidget(langLabel);
  languageLayout->addWidget(m_englishRadioButton);
  languageLayout->addWidget(m_chineseRadioButton);

  auto* languageGroupBox = new QGroupBox(tr("Language"));
  languageGroupBox->setLayout(languageLayout);

  m_prefixWorldspawnOnCopyCheckBox =
    new QCheckBox(tr("Prefix worldspawn header on copy"));

  auto* miscLayout = new QVBoxLayout();
  miscLayout->setContentsMargins(0, 0, 0, 0);
  miscLayout->addWidget(m_prefixWorldspawnOnCopyCheckBox);

  auto* miscGroupBox = new QGroupBox(tr("Editor"));
  miscGroupBox->setLayout(miscLayout);

  auto* layout = new QVBoxLayout();
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(languageGroupBox);
  layout->addWidget(miscGroupBox);
  layout->addStretch(1);
  setLayout(layout);

  connect(
    m_prefixWorldspawnOnCopyCheckBox,
    &QCheckBox::toggled,
    this,
    [](const bool checked) {
      auto& prefs = PreferenceManager::instance();
      prefs.set(Preferences::PrefixWorldspawnHeaderOnCopy, checked);
    });
}

bool MiscPreferencePane::canResetToDefaults()
{
  return true;
}

void MiscPreferencePane::doResetToDefaults()
{
  auto& prefs = PreferenceManager::instance();
  prefs.resetToDefault(Preferences::Language);
  prefs.resetToDefault(Preferences::PrefixWorldspawnHeaderOnCopy);

  updateControls();
}

void MiscPreferencePane::updateControls()
{
  auto& prefs = PreferenceManager::instance();
  const auto& language = prefs.get(Preferences::Language);

  if (language == Preferences::languageEnglish())
  {
    m_englishRadioButton->setChecked(true);
  }
  else
  {
    m_chineseRadioButton->setChecked(true);
  }

  m_prefixWorldspawnOnCopyCheckBox->setChecked(
    pref(Preferences::PrefixWorldspawnHeaderOnCopy));
}

bool MiscPreferencePane::validate()
{
  return true;
}

void MiscPreferencePane::showRestartRequiredMessage()
{
  QMessageBox::information(
    this,
    tr("Restart Required"),
    tr("Language settings will take effect after restarting the application"),
    QMessageBox::Ok);
}

} // namespace tb::ui
