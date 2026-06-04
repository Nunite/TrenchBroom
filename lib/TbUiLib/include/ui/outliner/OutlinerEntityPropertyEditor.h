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
struct SelectionChange;
class Node;
class EntityNodeBase;
} // namespace tb::mdl

namespace tb::ui
{
class TitledPanel;
class FlagsEditor;
class MapDocument;
class SmartWadEditor;

class OutlinerEntityPropertyEditor : public QWidget
{
    Q_OBJECT
private:
    MapDocument& m_document;
    NotifierConnection m_notifierConnection;

    TitledPanel* m_propertiesPanel = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_scrollContents = nullptr;
    QVBoxLayout* m_scrollLayout = nullptr;

    QLineEdit* m_addKey = nullptr;
    QLineEdit* m_addValue = nullptr;

    QWidget* m_embeddedWadEditorContainer = nullptr;
    SmartWadEditor* m_embeddedWadEditor = nullptr;
    bool m_wadEditorExpanded = false;

    QWidget* m_embeddedSpawnflagsEditorContainer = nullptr;
    FlagsEditor* m_embeddedSpawnflagsEditor = nullptr;
    bool m_spawnflagsEditorExpanded = false;

    bool m_updateQueued = false;
    bool m_forceUpdate = false;
    bool m_comboPopupVisible = false;
    bool m_updateDeferred = false;

public:
    explicit OutlinerEntityPropertyEditor(MapDocument& document, QWidget* parent = nullptr);
    ~OutlinerEntityPropertyEditor() override;
    void onChoiceComboPopupHidden();

private:
    void connectObservers();
    void scheduleUpdate(bool force = false);
    void updateFromSelection();

    void rebuildPropertyRows(const std::vector<mdl::EntityNodeBase*>& entityNodes);
};

} // namespace tb::ui

