#pragma once

#include <QWidget>
#include <vector>
#include <functional>
#include <QString>
#include <QPoint>

namespace tb::ui {

struct PieMenuItem {
    QString label;
    std::function<void()> action;
};

class PieMenu : public QWidget {
    Q_OBJECT

public:
    explicit PieMenu(QWidget* parent = nullptr);

    void addItem(const QString& label, std::function<void()> action);
    void clearItems();

    // Show the menu at the given global position
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

    int getIndexForAngle(double angle) const;
    void executeAndClose(int index);
};

} // namespace tb::ui
