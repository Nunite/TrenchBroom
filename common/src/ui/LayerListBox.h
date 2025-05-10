/*
 Copyright (C) 2010 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "NotifierConnection.h"
#include "ui/ControlListBox.h"

#include <memory>
#include <vector>
#include <QTreeWidget>

class QLabel;
class QAbstractButton;
class QLineEdit;
class QComboBox;

namespace tb::mdl
{
class LayerNode;
class Node;
} // namespace tb::mdl

namespace tb::ui
{
class MapDocument;
class Selection;

class LayerTreeWidget : public QTreeWidget
{
    Q_OBJECT
private:
    std::weak_ptr<MapDocument> m_document;
    QIcon m_worldIcon;
    QIcon m_layerIcon;
    QIcon m_groupIcon;
    QIcon m_entityIcon;
    QIcon m_brushIcon;
    QIcon m_visibleIcon;
    QIcon m_hiddenIcon;
    QIcon m_lockedIcon;
    QIcon m_unlockedIcon;
    bool m_syncingSelection;
    NotifierConnection m_notifierConnection;
    QPoint m_dragStartPosition;

    void loadIcons();
    void setupTreeItem(QTreeWidgetItem* item, mdl::Node* node);
    void addEntityToTree(QTreeWidgetItem* parentItem, mdl::Node* node);
    void addGroupToTree(QTreeWidgetItem* parentItem, mdl::Node* node);
    
    void syncSelectionFromDocument();
    bool findAndSelectNode(const mdl::Node* targetNode, QTreeWidgetItem* startItem);
    QTreeWidgetItem* findNodeItemRecursive(const mdl::Node* targetNode, QTreeWidgetItem* startItem);
    void syncSelectionToDocument();
    void onItemSelectionChanged();
    void onDocumentSelectionChanged(const Selection& selection);
    void collapseOtherEntities(QTreeWidgetItem* item, QTreeWidgetItem* selectedItem);

protected:
    void drawRow(QPainter* painter, const QStyleOptionViewItem& options, const QModelIndex& index) const override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    
    
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

signals:
    void nodeVisibilityToggled(mdl::Node* node);
    void nodeLockToggled(mdl::Node* node);
    void nodeActivated(mdl::Node* node);
    void nodeRightClicked(mdl::Node* node, const QPoint& pos);

public:
    explicit LayerTreeWidget(std::weak_ptr<MapDocument> document, QWidget* parent = nullptr);
    void updateTree();
    QTreeWidgetItem* findNodeItem(mdl::Node* targetNode, QTreeWidgetItem* startItem);
    void updateNodeItem(mdl::Node* node);
    void updateVisibilityIconRecursively(QTreeWidgetItem* item, bool isVisible);
};

class LayerListBox : public QWidget
{
    Q_OBJECT
private:
    std::weak_ptr<MapDocument> m_document;
    NotifierConnection m_notifierConnection;
    QLineEdit* m_searchBox;
    QComboBox* m_sortOptions;
    LayerTreeWidget* m_treeWidget;
    int m_currentSortMode; 

    void createGui();
    void connectObservers();
    void filterTree(const QString& text);
    void sortTree(int index);

    
    void showAllItems(QTreeWidgetItem* item);
    bool itemMatchesFilter(QTreeWidgetItem* item, const QString& text);
    void sortByType();

    
    void documentDidChange(MapDocument*);
    void nodesDidChange(const std::vector<mdl::Node*>&);

public:
    explicit LayerListBox(std::weak_ptr<MapDocument> document, QWidget* parent = nullptr);

    mdl::LayerNode* selectedLayer() const;
    void setSelectedLayer(mdl::LayerNode* layer);
    void updateSelectionForRemoval();
    void updateTree();

signals:
    void layerSelected(mdl::LayerNode* layer);
    void layerSetCurrent(mdl::LayerNode* layer);
    void layerRightClicked(mdl::LayerNode* layer);
    void layerOmitFromExportToggled(mdl::LayerNode* layer);
    void layerVisibilityToggled(mdl::LayerNode* layer);
    void layerLockToggled(mdl::LayerNode* layer);
    void itemSelectionChanged();
};

} // namespace tb::ui
