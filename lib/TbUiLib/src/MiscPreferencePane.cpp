#include "ui/MiscPreferencePane.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "ui/AppController.h"
#include "ui/PieMenuSettingsDialog.h"
#include "ui/QStyleUtils.h"
#include "ui/ViewConstants.h"

namespace tb::ui
{
namespace
{
QGroupBox* createGroupBox(const QString& title, QLayout* contentLayout)
{
  auto* groupBox = new QGroupBox{title};
  contentLayout->setContentsMargins(
    LayoutConstants::WideHMargin,
    LayoutConstants::WideVMargin,
    LayoutConstants::WideHMargin,
    LayoutConstants::WideVMargin);
  contentLayout->setSpacing(LayoutConstants::WideVMargin);
  groupBox->setLayout(contentLayout);
  return groupBox;
}
} // namespace

MiscPreferencePane::MiscPreferencePane(AppController& appController, QWidget* parent)
  : PreferencePane{parent}
  , m_appController{appController}
{
  createGui();
  updateControls();
}

void MiscPreferencePane::createGui()
{
  auto* languageLabel = new QLabel{tr("Select the display language.")};
  setInfoStyle(languageLabel);

  m_englishRadioButton =
    new QRadioButton{QString::fromStdString(Preferences::languageEnglish())};
  m_chineseRadioButton =
    new QRadioButton{QString::fromStdString(Preferences::languageChinese())};

  m_languageButtonGroup = new QButtonGroup{this};
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

  auto* languageChoicesLayout = new QHBoxLayout{};
  languageChoicesLayout->setContentsMargins(0, 0, 0, 0);
  languageChoicesLayout->setSpacing(LayoutConstants::WideHMargin);
  languageChoicesLayout->addWidget(m_englishRadioButton);
  languageChoicesLayout->addWidget(m_chineseRadioButton);
  languageChoicesLayout->addStretch(1);

  auto* languageLayout = new QVBoxLayout{};
  languageLayout->addWidget(languageLabel);
  languageLayout->addLayout(languageChoicesLayout);

  m_prefixWorldspawnOnCopyCheckBox =
    new QCheckBox{tr("Prefix worldspawn header on copy")};
  connect(
    m_prefixWorldspawnOnCopyCheckBox, &QCheckBox::toggled, this, [](const bool checked) {
      auto& prefs = PreferenceManager::instance();
      prefs.set(Preferences::PrefixWorldspawnHeaderOnCopy, checked);
    });

  auto* editorLayout = new QVBoxLayout{};
  editorLayout->addWidget(m_prefixWorldspawnOnCopyCheckBox);

  m_pieMenuSettingsButton = new QPushButton{tr("Pie Menu Settings...")};
  m_pythonPluginManagerButton = new QPushButton{tr("Python Plugin Manager...")};

  connect(m_pieMenuSettingsButton, &QPushButton::clicked, this, [this]() {
    auto dialog = PieMenuSettingsDialog{this};
    dialog.exec();
  });

  connect(m_pythonPluginManagerButton, &QPushButton::clicked, this, [this]() {
    m_appController.showPythonPluginManager();
  });

  auto* toolLayout = new QVBoxLayout{};
  toolLayout->addWidget(m_pieMenuSettingsButton);
  toolLayout->addWidget(m_pythonPluginManagerButton);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin);
  layout->setSpacing(LayoutConstants::WideVMargin);
  layout->addWidget(createGroupBox(tr("Language"), languageLayout));
  layout->addWidget(createGroupBox(tr("Editor"), editorLayout));
  layout->addWidget(createGroupBox(tr("Tools"), toolLayout));
  layout->addStretch(1);
  setLayout(layout);
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
  const auto englishBlocker = QSignalBlocker{m_englishRadioButton};
  const auto chineseBlocker = QSignalBlocker{m_chineseRadioButton};
  const auto prefixBlocker = QSignalBlocker{m_prefixWorldspawnOnCopyCheckBox};

  const auto& language = pref(Preferences::Language);
  m_englishRadioButton->setChecked(language == Preferences::languageEnglish());
  m_chineseRadioButton->setChecked(language == Preferences::languageChinese());
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
    tr("Language settings will take effect after applying preferences and restarting the "
       "application"),
    QMessageBox::Ok);
}

} // namespace tb::ui
