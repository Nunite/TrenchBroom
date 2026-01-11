#pragma once

#include "NotifierConnection.h"

#include <QWidget>

#include <string>
#include <vector>

class QLineEdit;
class QScrollArea;
class QVBoxLayout;

namespace tb::mdl
{
class Map;
struct SelectionChange;
class Node;
class EntityNodeBase;
} // namespace tb::mdl

namespace tb::ui
{
class CollapsibleTitledPanel;
class SmartPropertyEditorManager;

class OutlinerEntityPropertyEditor : public QWidget
{
    Q_OBJECT
private:
    mdl::Map& m_map;
    NotifierConnection m_notifierConnection;

    CollapsibleTitledPanel* m_propertiesPanel = nullptr;
    CollapsibleTitledPanel* m_smartEditorPanel = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_scrollContents = nullptr;
    QVBoxLayout* m_scrollLayout = nullptr;

    QLineEdit* m_addKey = nullptr;
    QLineEdit* m_addValue = nullptr;

    SmartPropertyEditorManager* m_smartEditorManager = nullptr;

    bool m_updateQueued = false;
    bool m_forceUpdate = false;
    bool m_comboPopupVisible = false;
    bool m_updateDeferred = false;

public:
    explicit OutlinerEntityPropertyEditor(mdl::Map& map, QWidget* parent = nullptr);
    ~OutlinerEntityPropertyEditor() override;

private:
    void connectObservers();
    void scheduleUpdate(bool force = false);
    void updateFromSelection();

    void rebuildPropertyRows(const std::vector<mdl::EntityNodeBase*>& entityNodes);
    void rebuildSmartEditor(const std::string& propertyKey);

    bool eventFilter(QObject* watched, QEvent* event) override;
};

} // namespace tb::ui

