#include "ui/PieMenu.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "ui/Action.h"

#include "kd/string_utils.h"

#include <cmath>

namespace tb::ui
{

std::optional<size_t> pieMenuIndexForAngle(double angle, const size_t itemCount)
{
  if (itemCount == 0u)
  {
    return std::nullopt;
  }

  angle = std::fmod(angle, 360.0);
  if (angle < 0.0)
  {
    angle += 360.0;
  }

  const auto angleStep = 360.0 / static_cast<double>(itemCount);
  const auto startAngle = 90.0 - (angleStep / 2.0);

  auto relAngle = angle - startAngle;
  while (relAngle < 0.0)
  {
    relAngle += 360.0;
  }

  const auto index = static_cast<size_t>(relAngle / angleStep);
  return index < itemCount ? std::optional<size_t>{index} : std::nullopt;
}

std::vector<PieMenuItem> buildPieMenuItems(
  const std::string& actionPreference,
  const std::unordered_map<std::filesystem::path, Action, kdl::path_hash>& actions,
  std::function<bool(const Action&)> isActionEnabled,
  std::function<void(const Action&)> executeAction)
{
  auto result = std::vector<PieMenuItem>{};
  for (const auto& pathStr : kdl::str_split(actionPreference, "|"))
  {
    if (pathStr.empty())
    {
      continue;
    }

    const auto it = actions.find(std::filesystem::path{pathStr});
    if (it == std::end(actions))
    {
      continue;
    }

    const auto& action = it->second;
    result.push_back(PieMenuItem{
      QString::fromStdString(action.label()),
      [executeAction, &action]() { executeAction(action); },
      isActionEnabled(action)});
  }
  return result;
}

PieMenu::PieMenu(QWidget* parent)
  : QWidget(parent)
{
  setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setMouseTracking(true);
}

void PieMenu::addItem(const QString& label, std::function<void()> action, bool enabled)
{
  m_items.push_back({label, action, enabled});
}

void PieMenu::clearItems()
{
  m_items.clear();
}

void PieMenu::showAt(const QPoint& globalPos)
{
  m_center = globalPos;
  const auto size = m_outerRadius * 2 + 50;
  resize(size, size);
  move(globalPos.x() - width() / 2, globalPos.y() - height() / 2);
  m_hoveredIndex = -1;
  show();
  setFocus();
}

void PieMenu::paintEvent(QPaintEvent* /*event*/)
{
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  const auto center = QPoint{width() / 2, height() / 2};

  if (m_items.empty())
  {
    return;
  }

  const auto angleStep = 360.0 / static_cast<double>(m_items.size());
  const auto startAngle = 90.0 - (angleStep / 2.0);

  for (size_t i = 0; i < m_items.size(); ++i)
  {
    QPainterPath path;
    path.moveTo(center);
    path.arcTo(
      center.x() - m_outerRadius,
      center.y() - m_outerRadius,
      m_outerRadius * 2,
      m_outerRadius * 2,
      startAngle + static_cast<double>(i) * angleStep,
      angleStep);
    path.closeSubpath();

    QPainterPath innerPath;
    innerPath.addEllipse(center, m_innerRadius, m_innerRadius);
    const auto sector = path.subtracted(innerPath);

    if (static_cast<int>(i) == m_hoveredIndex)
    {
      painter.fillPath(
        sector, m_items[i].enabled ? QColor{60, 140, 220, 200} : QColor{80, 80, 80, 200});
    }
    else
    {
      painter.fillPath(sector, QColor{40, 40, 40, 180});
    }

    painter.setPen(QPen{QColor{200, 200, 200}, 1});
    painter.drawPath(sector);

    const auto midAngle =
      startAngle + static_cast<double>(i) * angleStep + angleStep / 2.0;
    const auto radians = midAngle * M_PI / 180.0;
    const auto textRadius = (m_innerRadius + m_outerRadius) / 2;
    const auto tx = center.x() + static_cast<int>(textRadius * std::cos(radians));
    const auto ty = center.y() - static_cast<int>(textRadius * std::sin(radians));

    const auto textRect = QRect{tx - 60, ty - 15, 120, 30};
    painter.setPen(m_items[i].enabled ? Qt::white : Qt::gray);
    painter.drawText(textRect, Qt::AlignCenter, m_items[i].label);
  }
}

void PieMenu::mouseMoveEvent(QMouseEvent* event)
{
  const auto center = QPoint{width() / 2, height() / 2};
  const auto diff = event->pos() - center;
  const auto dist = std::sqrt(diff.x() * diff.x() + diff.y() * diff.y());

  if (dist < m_innerRadius || dist > m_outerRadius)
  {
    m_hoveredIndex = -1;
  }
  else
  {
    const auto angle =
      std::atan2(center.y() - event->pos().y(), event->pos().x() - center.x()) * 180.0
      / M_PI;
    const auto index = pieMenuIndexForAngle(angle, m_items.size());
    m_hoveredIndex = index ? static_cast<int>(*index) : -1;
  }
  update();
}

void PieMenu::mouseReleaseEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton)
  {
    if (m_hoveredIndex != -1)
    {
      executeAndClose(m_hoveredIndex);
    }
    else
    {
      close();
    }
  }
}

void PieMenu::keyPressEvent(QKeyEvent* event)
{
  if (event->key() == Qt::Key_Escape)
  {
    close();
    return;
  }
  if (event->key() == Qt::Key_QuoteLeft)
  {
    event->accept();
    return;
  }
}

void PieMenu::keyReleaseEvent(QKeyEvent* event)
{
  if (event->isAutoRepeat())
  {
    event->ignore();
    return;
  }
  if (event->key() == Qt::Key_QuoteLeft)
  {
    if (m_hoveredIndex != -1)
    {
      executeAndClose(m_hoveredIndex);
    }
    else
    {
      close();
    }
  }
}

void PieMenu::focusOutEvent(QFocusEvent* /*event*/)
{
  close();
}

void PieMenu::executeAndClose(const int index)
{
  if (index >= 0 && index < static_cast<int>(m_items.size()))
  {
    if (!m_items[static_cast<size_t>(index)].enabled)
    {
      close();
      return;
    }
    const auto action = m_items[static_cast<size_t>(index)].action;
    close();
    if (action)
    {
      action();
    }
  }
}

} // namespace tb::ui
