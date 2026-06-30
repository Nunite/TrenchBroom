#include "ui/McpPreferencePane.h"

#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QUrl>
#include <QVBoxLayout>

#include "mcp/McpMode.h"
#include "ui/AppController.h"
#include "ui/FormWithSectionsLayout.h"
#include "ui/QStyleUtils.h"
#include "ui/ViewConstants.h"

namespace tb::ui
{
namespace
{

constexpr int ModeRole = Qt::UserRole;

void addMode(QComboBox& combo, const QString& label, const mcp::McpMode mode)
{
  combo.addItem(label, mcp::modeName(mode));
}

mcp::McpMode modeFromCombo(const QComboBox& combo)
{
  const auto value = combo.currentData(ModeRole).toString();
  return mcp::parseMode(value).value_or(mcp::McpMode::Off);
}

int findModeIndex(const QComboBox& combo, const mcp::McpMode mode)
{
  return combo.findData(mcp::modeName(mode), ModeRole);
}

void addToolProfile(
  QComboBox& combo, const QString& label, const mcp::McpToolProfile profile)
{
  combo.addItem(label, mcp::toolProfileName(profile));
}

mcp::McpToolProfile toolProfileFromCombo(const QComboBox& combo)
{
  const auto value = combo.currentData(ModeRole).toString();
  return mcp::parseToolProfile(value).value_or(mcp::McpToolProfile::Modeling);
}

int findToolProfileIndex(const QComboBox& combo, const mcp::McpToolProfile profile)
{
  return combo.findData(mcp::toolProfileName(profile), ModeRole);
}

} // namespace

McpPreferencePane::McpPreferencePane(AppController& appController, QWidget* parent)
  : PreferencePane{parent}
  , m_appController{appController}
  , m_config{mcp::defaultBridgeConfig()}
  , m_configPath{mcp::defaultConfigPath()}
{
  loadConfig();
  createGui();
  updateControls();
}

void McpPreferencePane::createGui()
{
  auto* mcpPreferences = new QWidget{};

  auto* infoLabel = new QLabel{
    tr("MCP lets local agent clients inspect and control TrenchBroom through a "
       "localhost HTTP endpoint. Keep it Off unless you are actively using an MCP "
       "client. Changes on this page apply immediately.")};
  infoLabel->setWordWrap(true);
  setInfoStyle(infoLabel);

  m_modeCombo = new QComboBox{};
  addMode(*m_modeCombo, tr("Off"), mcp::McpMode::Off);
  addMode(*m_modeCombo, tr("Read-only"), mcp::McpMode::ReadOnly);
  addMode(*m_modeCombo, tr("Edit"), mcp::McpMode::Edit);
  m_modeCombo->setToolTip(
    tr("Read-only allows status, map, selection, actions, overlay, and capture tools. "
       "Edit also allows transaction-based map changes."));

  connect(
    m_modeCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    &McpPreferencePane::modeChanged);

  m_toolProfileCombo = new QComboBox{};
  addToolProfile(*m_toolProfileCombo, tr("Core"), mcp::McpToolProfile::Core);
  addToolProfile(*m_toolProfileCombo, tr("Modeling"), mcp::McpToolProfile::Modeling);
  addToolProfile(*m_toolProfileCombo, tr("Full"), mcp::McpToolProfile::Full);
  m_toolProfileCombo->setToolTip(
    tr("Core is compact discovery and smoke. Modeling is the recommended default. "
       "Full is for expert and debug tools."));
  connect(
    m_toolProfileCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    &McpPreferencePane::toolProfileChanged);

  m_httpUrlEdit = new QLineEdit{};
  m_httpUrlEdit->setReadOnly(true);

  m_pipeNameEdit = new QLineEdit{};
  m_pipeNameEdit->setToolTip(tr("Compatibility pipe name used by trenchbroom-mcp."));
  connect(m_pipeNameEdit, &QLineEdit::editingFinished, this, [this]() {
    pipeNameChanged(m_pipeNameEdit->text());
  });

  m_claudeCommandEdit = new QLineEdit{};
  m_claudeCommandEdit->setReadOnly(true);

  m_copyClaudeCommandButton = new QPushButton{tr("Copy Command")};
  connect(
    m_copyClaudeCommandButton,
    &QPushButton::clicked,
    this,
    &McpPreferencePane::copyClaudeCommand);

  auto* claudeCommandLayout = new QHBoxLayout{};
  claudeCommandLayout->setContentsMargins(0, 0, 0, 0);
  claudeCommandLayout->addWidget(m_claudeCommandEdit, 1);
  claudeCommandLayout->addWidget(m_copyClaudeCommandButton);

  m_configPathEdit = new QLineEdit{};
  m_configPathEdit->setReadOnly(true);

  m_openConfigFolderButton = new QPushButton{tr("Open Config Folder")};
  connect(
    m_openConfigFolderButton,
    &QPushButton::clicked,
    this,
    &McpPreferencePane::openConfigFolder);

  auto* configPathLayout = new QHBoxLayout{};
  configPathLayout->setContentsMargins(0, 0, 0, 0);
  configPathLayout->addWidget(m_configPathEdit, 1);
  configPathLayout->addWidget(m_openConfigFolderButton);

  m_statusLabel = new QLabel{};
  setInfoStyle(m_statusLabel);

  m_errorLabel = new QLabel{};
  m_errorLabel->setWordWrap(true);
  m_errorLabel->setStyleSheet("color: #b00020;");

  auto* layout = new FormWithSectionsLayout{};
  layout->setContentsMargins(
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin);
  layout->setVerticalSpacing(LayoutConstants::WideVMargin);
  layout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

  layout->addSection(tr("MCP Bridge"));
  layout->addRow(infoLabel);
  layout->addRow(tr("Mode"), m_modeCombo);
  layout->addRow(tr("Tool Profile"), m_toolProfileCombo);
  layout->addRow(tr("Status"), m_statusLabel);
  layout->addSection(tr("HTTP Connection"));
  layout->addRow(tr("URL"), m_httpUrlEdit);
  layout->addRow(tr("Claude Code"), claudeCommandLayout);
  layout->addSection(tr("Compatibility"));
  layout->addRow(tr("Pipe name"), m_pipeNameEdit);
  layout->addRow(tr("Config"), configPathLayout);
  layout->addRow(m_errorLabel);

  mcpPreferences->setLayout(layout);

  auto* outerLayout = new QVBoxLayout{};
  outerLayout->setContentsMargins(QMargins{});
  outerLayout->setSpacing(0);
  outerLayout->addSpacing(LayoutConstants::NarrowVMargin);
  outerLayout->addWidget(mcpPreferences, 1);
  outerLayout->addSpacing(LayoutConstants::MediumVMargin);

  createScrollableContent(outerLayout);
}

bool McpPreferencePane::canResetToDefaults()
{
  return true;
}

void McpPreferencePane::doResetToDefaults()
{
  m_config = mcp::defaultBridgeConfig();
  applyConfigChange();
}

void McpPreferencePane::updateControls()
{
  const auto modeBlocker = QSignalBlocker{m_modeCombo};
  const auto toolProfileBlocker = QSignalBlocker{m_toolProfileCombo};
  const auto pipeBlocker = QSignalBlocker{m_pipeNameEdit};
  const auto httpUrlBlocker = QSignalBlocker{m_httpUrlEdit};
  const auto claudeCommandBlocker = QSignalBlocker{m_claudeCommandEdit};

  const auto modeIndex = findModeIndex(*m_modeCombo, m_config.mode);
  m_modeCombo->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
  const auto toolProfileIndex =
    findToolProfileIndex(*m_toolProfileCombo, m_config.toolProfile);
  m_toolProfileCombo->setCurrentIndex(toolProfileIndex >= 0 ? toolProfileIndex : 1);
  const auto httpUrl =
    QString{"http://%1:%2/mcp"}.arg(m_config.httpHost).arg(m_config.httpPort);
  m_httpUrlEdit->setText(httpUrl);
  m_pipeNameEdit->setText(m_config.pipeName);
  m_claudeCommandEdit->setText(
    QString{"claude mcp add --scope user --transport http trenchbroom %1"}.arg(httpUrl));
  m_configPathEdit->setText(QDir::toNativeSeparators(m_configPath));

  const auto modeText = mcp::modeName(m_config.mode);
  const auto statusText =
    m_appController.mcpHttpServerIsListening()
      ? tr("HTTP endpoint running in %1 mode").arg(modeText)
    : m_config.mode == mcp::McpMode::Off
      ? tr("Disabled")
      : tr("Configured as %1, but the HTTP endpoint is not listening").arg(modeText);
  m_statusLabel->setText(statusText);

  m_errorLabel->setVisible(!m_error.isEmpty());
  m_errorLabel->setText(m_error);
}

bool McpPreferencePane::validate()
{
  if (m_config.pipeName.trimmed().isEmpty())
  {
    m_error = tr("Pipe name must not be empty.");
    updateControls();
    return false;
  }
  return true;
}

void McpPreferencePane::loadConfig()
{
  auto error = QString{};
  const auto config = mcp::readOrCreateBridgeConfig(m_configPath, &error);
  if (config)
  {
    m_config = *config;
    m_error.clear();
  }
  else
  {
    m_error = error;
  }
}

bool McpPreferencePane::saveConfig()
{
  auto error = QString{};
  if (mcp::writeBridgeConfig(m_config, m_configPath, &error))
  {
    m_error.clear();
    return true;
  }

  m_error = error;
  updateControls();
  return false;
}

void McpPreferencePane::applyConfigChange()
{
  if (!validate())
  {
    return;
  }

  if (saveConfig())
  {
    m_appController.restartMcpBridge();
  }
  updateControls();
}

void McpPreferencePane::modeChanged(const int /* index */)
{
  m_config.mode = modeFromCombo(*m_modeCombo);
  applyConfigChange();
}

void McpPreferencePane::toolProfileChanged(const int /* index */)
{
  m_config.toolProfile = toolProfileFromCombo(*m_toolProfileCombo);
  applyConfigChange();
}

void McpPreferencePane::pipeNameChanged(const QString& text)
{
  const auto pipeName = text.trimmed();
  if (pipeName == m_config.pipeName)
  {
    return;
  }

  m_config.pipeName = pipeName;
  applyConfigChange();
}

void McpPreferencePane::copyClaudeCommand()
{
  QGuiApplication::clipboard()->setText(m_claudeCommandEdit->text());
}

void McpPreferencePane::openConfigFolder()
{
  QDesktopServices::openUrl(QUrl::fromLocalFile(mcp::defaultConfigDirectory()));
}

} // namespace tb::ui
