#pragma once

#include "mcp/McpBridgeConfig.h"
#include "ui/PreferencePane.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace tb::ui
{

class AppController;

class McpPreferencePane : public PreferencePane
{
  Q_OBJECT
private:
  AppController& m_appController;
  mcp::McpBridgeConfig m_config;
  QString m_configPath;
  QString m_error;
  QComboBox* m_modeCombo = nullptr;
  QLineEdit* m_httpUrlEdit = nullptr;
  QLineEdit* m_pipeNameEdit = nullptr;
  QLineEdit* m_tokenEdit = nullptr;
  QLineEdit* m_claudeCommandEdit = nullptr;
  QLineEdit* m_configPathEdit = nullptr;
  QLabel* m_statusLabel = nullptr;
  QLabel* m_errorLabel = nullptr;
  QPushButton* m_regenerateTokenButton = nullptr;
  QPushButton* m_copyClaudeCommandButton = nullptr;
  QPushButton* m_openConfigFolderButton = nullptr;

public:
  explicit McpPreferencePane(AppController& appController, QWidget* parent = nullptr);

private:
  void createGui();
  bool canResetToDefaults() override;
  void doResetToDefaults() override;
  void updateControls() override;
  bool validate() override;

  void loadConfig();
  bool saveConfig();
  void applyConfigChange();
  void modeChanged(int index);
  void pipeNameChanged(const QString& text);
  void regenerateToken();
  void copyClaudeCommand();
  void openConfigFolder();
};

} // namespace tb::ui
