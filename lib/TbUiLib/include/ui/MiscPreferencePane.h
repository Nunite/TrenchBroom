#pragma once

#include "ui/PreferencePane.h"

class QButtonGroup;
class QRadioButton;
class QCheckBox;
class QLineEdit;
class QPushButton;
class QMenu;
class QListWidget;
class QTextEdit;

namespace tb::ui
{

class MiscPreferencePane : public PreferencePane
{
  Q_OBJECT
private:
  QRadioButton* m_englishRadioButton;
  QRadioButton* m_chineseRadioButton;
  QButtonGroup* m_languageButtonGroup;
  QLineEdit* m_pluginSearchBox = nullptr;
  QCheckBox* m_pluginShowIssuesOnlyCheckBox = nullptr;
  QListWidget* m_pluginList = nullptr;
  QPushButton* m_addPluginBtn = nullptr;
  QPushButton* m_removePluginBtn = nullptr;
  QPushButton* m_clearPluginsBtn = nullptr;
  QPushButton* m_reloadPluginsBtn = nullptr;
  QListWidget* m_pluginStatusList = nullptr;
  QTextEdit* m_pluginDetails = nullptr;
  QCheckBox* m_prefixWorldspawnOnCopyCheckBox = nullptr;
  QListWidget* m_pieMenuActionList = nullptr;
  QPushButton* m_addActionBtn = nullptr;
  QPushButton* m_removeActionBtn = nullptr;
  QPushButton* m_clearActionsBtn = nullptr;
  QMenu* m_pieMenu = nullptr;

signals:
  void languageChanged();

public:
  explicit MiscPreferencePane(QWidget* parent = nullptr);

private:
  void createGui();
  bool canResetToDefaults() override;
  void doResetToDefaults() override;
  void updateControls() override;
  void showRestartRequiredMessage();
  bool validate() override;
  void savePieMenuActions();
  void addPieMenuAction(const QString& label, const QString& path);
  void savePluginPaths();
  void addPluginPath(const QString& path);
  void reloadPluginStatus();
  void updatePluginDetails();
};

} // namespace tb::ui
