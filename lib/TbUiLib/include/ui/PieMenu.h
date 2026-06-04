#pragma once

#include <QPoint>
#include <QString>
#include <QWidget>

#include "kd/path_hash.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tb::ui
{
class Action;
} // namespace tb::ui

namespace tb::ui
{

struct PieMenuItem
{
  QString label;
  std::function<void()> action;
  bool enabled = true;
};

std::optional<size_t> pieMenuIndexForAngle(double angle, size_t itemCount);
std::vector<PieMenuItem> buildPieMenuItems(
  const std::string& actionPreference,
  const std::unordered_map<std::filesystem::path, Action, kdl::path_hash>& actions,
  std::function<bool(const Action&)> isActionEnabled,
  std::function<void(const Action&)> executeAction);

class PieMenu : public QWidget
{
  Q_OBJECT
public:
  explicit PieMenu(QWidget* parent = nullptr);

  void addItem(const QString& label, std::function<void()> action, bool enabled = true);
  void clearItems();

  void showAt(const QPoint& globalPos);

protected:
  void paintEvent(QPaintEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;

private:
  std::vector<PieMenuItem> m_items;
  int m_hoveredIndex = -1;
  QPoint m_center;
  const int m_innerRadius = 40;
  const int m_outerRadius = 150;

  void executeAndClose(int index);
};

} // namespace tb::ui
