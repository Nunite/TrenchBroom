#pragma once

#include "ui/PreferencePane.h"

class QButtonGroup;
class QRadioButton;
class QCheckBox;
class QPushButton;
class QMenu;
class QListWidget;

namespace tb::ui
{

class MiscPreferencePane : public PreferencePane
{
  Q_OBJECT
private:
  QRadioButton* m_englishRadioButton;
  QRadioButton* m_chineseRadioButton;
  QButtonGroup* m_languageButtonGroup;
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
};

} // namespace tb::ui
