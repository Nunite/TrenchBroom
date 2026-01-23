#include "PieMenu.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QDebug>
#include <cmath>

namespace tb::ui {

PieMenu::PieMenu(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
}

void PieMenu::addItem(const QString& label, std::function<void()> action, bool enabled) {
    m_items.push_back({label, action, enabled});
}

void PieMenu::clearItems() {
    m_items.clear();
}

void PieMenu::showAt(const QPoint& globalPos) {
    qDebug() << "PieMenu::showAt" << globalPos;
    m_center = globalPos;
    // Center the widget around the mouse
    int size = m_outerRadius * 2 + 50;
    resize(size, size);
    move(globalPos.x() - width() / 2, globalPos.y() - height() / 2);
    m_hoveredIndex = -1;
    show();
    setFocus();
}

void PieMenu::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QPoint center(width() / 2, height() / 2);

    if (m_items.empty()) return;

    double angleStep = 360.0 / static_cast<double>(m_items.size());
    // Start from top (90 degrees, but Qt handles angles differently)
    // 0 is 3 o'clock. Top is 90 (counter-clockwise) or 270.
    // We want the first item to be at the TOP.
    // If we have 4 items: Top, Left, Bottom, Right.
    // Item 0: Top. Angle range: 45 to 135. Center: 90.
    
    double startAngle = 90.0 - (angleStep / 2.0);

    for (size_t i = 0; i < m_items.size(); ++i) {
        // Draw Sector
        QPainterPath path;
        path.moveTo(center);
        path.arcTo(center.x() - m_outerRadius, center.y() - m_outerRadius, 
                   m_outerRadius * 2, m_outerRadius * 2, startAngle + static_cast<double>(i) * angleStep, angleStep);
        path.closeSubpath();

        // Cut out inner circle
        QPainterPath innerPath;
        innerPath.addEllipse(center, m_innerRadius, m_innerRadius);
        QPainterPath sector = path.subtracted(innerPath);

        if (static_cast<int>(i) == m_hoveredIndex) {
            if (m_items[i].enabled) {
                painter.fillPath(sector, QColor(60, 140, 220, 200));
            } else {
                painter.fillPath(sector, QColor(80, 80, 80, 200));
            }
        } else {
            painter.fillPath(sector, QColor(40, 40, 40, 180));
        }
        
        painter.setPen(QPen(QColor(200, 200, 200), 1));
        painter.drawPath(sector);

        // Draw Text
        double midAngle = startAngle + static_cast<double>(i) * angleStep + angleStep / 2.0;
        double rad = midAngle * M_PI / 180.0;
        // Qt coordinates: Y is down.
        // math coordinates: Y is up.
        // We used Qt angles for arcTo which are degrees counter-clockwise from 3 o'clock.
        // So cos(rad), -sin(rad) for Y up logic? No, Qt coordinate system Y is down.
        // But QPainter angles: 0 is 3 o'clock, 90 is 12 o'clock (Up).
        // So y should be -sin(rad).
        
        int textRadius = (m_innerRadius + m_outerRadius) / 2;
        int tx = center.x() + static_cast<int>(textRadius * std::cos(rad * M_PI / 180.0)); // Wait, rad is already radians
        int ty = center.y() - static_cast<int>(textRadius * std::sin(rad)); // 90 deg -> sin(90)=1 -> y - r -> Up. Correct.

        // Re-calc using radians properly
        double radians = midAngle * M_PI / 180.0;
        tx = center.x() + static_cast<int>(textRadius * std::cos(radians));
        ty = center.y() - static_cast<int>(textRadius * std::sin(radians));

        QRect textRect(tx - 60, ty - 15, 120, 30);
        if (m_items[i].enabled) {
            painter.setPen(Qt::white);
        } else {
            painter.setPen(Qt::gray);
        }
        painter.drawText(textRect, Qt::AlignCenter, m_items[i].label);
    }
}

void PieMenu::mouseMoveEvent(QMouseEvent* event) {
    QPoint center(width() / 2, height() / 2);
    QPoint diff = event->pos() - center;
    double dist = std::sqrt(diff.x() * diff.x() + diff.y() * diff.y());

    if (dist < m_innerRadius || dist > m_outerRadius) {
        m_hoveredIndex = -1;
    } else {
        // Calculate angle
        // atan2(y, x). Y is down, so we flip y to make it standard cartesian for calculation?
        // Actually we want 0 at 3 o'clock, 90 at 12 o'clock.
        // event->y() increases downwards.
        // So (center.y - event.y) is positive when mouse is above.
        double angle = std::atan2(center.y() - event->pos().y(), event->pos().x() - center.x()) * 180.0 / M_PI;
        if (angle < 0) angle += 360.0;
        
        // angle is now 0..360 counter-clockwise from 3 o'clock.
        // We need to match the item slots.
        // Item 0 centered at 90. Range [90 - step/2, 90 + step/2].
        
        if (m_items.empty()) {
            m_hoveredIndex = -1;
        } else {
            double angleStep = 360.0 / static_cast<double>(m_items.size());
            double startAngle = 90.0 - (angleStep / 2.0);
            
            // Normalize angle relative to startAngle
            double relAngle = angle - startAngle;
            while (relAngle < 0) relAngle += 360.0;
            
            int index = static_cast<int>(relAngle / angleStep);
            if (index >= 0 && index < static_cast<int>(m_items.size())) {
                m_hoveredIndex = index;
            } else {
                m_hoveredIndex = -1; // Should technically not happen if math is right
            }
        }
    }
    update();
}

void PieMenu::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (m_hoveredIndex != -1) {
            executeAndClose(m_hoveredIndex);
        } else {
            close();
        }
    }
}

void PieMenu::keyPressEvent(QKeyEvent* event) {
    // If user presses Esc, close
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    // Swallow the trigger key to prevent propagation to parent
    if (event->key() == Qt::Key_QuoteLeft) {
        event->accept();
        return;
    }
}

void PieMenu::keyReleaseEvent(QKeyEvent* event) {
    if (event->isAutoRepeat()) {
        event->ignore();
        return;
    }
    if (event->key() == Qt::Key_QuoteLeft) {
        // If an item is hovered, execute it
        if (m_hoveredIndex != -1) {
            executeAndClose(m_hoveredIndex);
        } else {
            close();
        }
    }
}

void PieMenu::focusOutEvent(QFocusEvent* /*event*/) {
    close();
}

void PieMenu::executeAndClose(int index) {
    if (index >= 0 && index < static_cast<int>(m_items.size())) {
        if (!m_items[static_cast<size_t>(index)].enabled) {
            close();
            return;
        }
        auto action = m_items[static_cast<size_t>(index)].action;
        close(); // Close first, then execute
        if (action) action();
    }
}

} // namespace tb::ui
