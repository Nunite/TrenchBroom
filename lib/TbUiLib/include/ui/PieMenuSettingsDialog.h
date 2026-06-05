#pragma once

#include <QDialog>

class QListWidget;
class QMenu;
class QPushButton;

namespace tb::ui
{

class PieMenuSettingsDialog : public QDialog
{
private:
  QListWidget* m_actionList = nullptr;
  QPushButton* m_addButton = nullptr;
  QPushButton* m_removeButton = nullptr;
  QPushButton* m_clearButton = nullptr;
  QMenu* m_actionMenu = nullptr;

public:
  explicit PieMenuSettingsDialog(QWidget* parent = nullptr);

private:
  void createGui();
  void populateActionMenu();
  void addPieMenuAction(const QString& label, const QString& path);
  void loadPieMenuActions();
  void savePieMenuActions();
};

} // namespace tb::ui
