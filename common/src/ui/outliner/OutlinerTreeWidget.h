#pragma once

#include <QTreeWidget>
#include <memory>
#include <unordered_map>
#include "NotifierConnection.h"

namespace tb::mdl
{
class Node;
class WorldNode;
class LayerNode;
class GroupNode;
class EntityNode;
class BrushNode;
struct SelectionChange;
}

namespace tb::ui
{
class MapDocument;
class Selection;

class OutlinerTreeWidget : public QTreeWidget
{
    Q_OBJECT
private:
    MapDocument& m_document;
    
    // Icons
    QIcon m_groupIcon;       // Map_folder.svg
    QIcon m_entityIcon;      // Map_entity.svg (Point entity)
    QIcon m_brushEntityIcon; // Map_fullcube.svg (Brush entity)
    QIcon m_brushIcon;       // Map_cube.svg (Brush)
    QIcon m_visibleIcon;     // object_show.svg
    QIcon m_hiddenIcon;      // object_hidden.svg
    QIcon m_lockedIcon;      // Lock_on.svg
    QIcon m_unlockedIcon;    // Lock_off.svg

    bool m_syncingSelection = false;
    int m_suppressScrollToSelectionCount = 0;
    bool m_updateTreeQueued = false;
    mdl::Node* m_revealAfterUpdate = nullptr;
    NotifierConnection m_notifierConnection;
    QPoint m_dragStartPosition;

public:
    explicit OutlinerTreeWidget(MapDocument& document, QWidget* parent = nullptr);
    ~OutlinerTreeWidget() override;

    void updateTree();
    void setFilterText(const QString& text);

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void drawRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    // Drag and Drop
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

signals:
    void nodeVisibilityToggled(mdl::Node* node);
    void nodeLockToggled(mdl::Node* node);
    void nodeActivated(mdl::Node* node);
    void nodeRightClicked(mdl::Node* node, const QPoint& pos);

private:
    void scheduleUpdateTree(mdl::Node* revealNode = nullptr);
    void loadIcons();
    void setupTreeItem(QTreeWidgetItem* item, mdl::Node* node);
    void applyFilter();
    
    // Recursive helpers
    void addNodeToTree(QTreeWidgetItem* parentItem, mdl::Node* node);
    
    // Selection sync
    void onDocumentSelectionChanged(const mdl::SelectionChange& change);
    void onItemSelectionChanged();
    void syncSelectionFromDocument();
    void findAndSelectNode(const mdl::Node* targetNode);
    QTreeWidgetItem* findItemForNode(const mdl::Node* targetNode);
    void refreshTreeItemRecursively(QTreeWidgetItem* item);
    void updateVisibilityIconRecursively(QTreeWidgetItem* item, bool isVisible);
    
    // Helper to get node from item
    mdl::Node* nodeFromItem(QTreeWidgetItem* item) const;

private:
    QString m_filterText;
    bool m_filterActive = false;
    std::unordered_map<const mdl::Node*, bool> m_expandedBeforeFilter;
    std::unordered_map<const mdl::LayerNode*, bool> m_worldspawnExpandedBeforeFilter;
};

} // namespace tb::ui
