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

#include "LayerListBox.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QPainter>
#include <QHeaderView>
#include <QTreeWidgetItem>
#include <QMouseEvent>
#include <QMenu>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QMimeData>
#include <QDrag>
#include <QApplication>
#include <QDataStream>
#include <set>

#include "mdl/LayerNode.h"
#include "mdl/WorldNode.h"
#include "mdl/EntityNode.h"
#include "mdl/GroupNode.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityDefinition.h"
#include "ui/MapDocument.h"
#include "ui/QtUtils.h"
#include "ui/ViewConstants.h"
#include "io/ResourceUtils.h"
#include "ui/Transaction.h"

#include "kdl/memory_utils.h"

namespace tb::ui
{

LayerTreeWidget::LayerTreeWidget(std::weak_ptr<MapDocument> document, QWidget* parent)
    : QTreeWidget(parent)
    , m_document(std::move(document))
    , m_syncingSelection(false)
{
    setHeaderHidden(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection); 
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setExpandsOnDoubleClick(false);
    setRootIsDecorated(true);
    setUniformRowHeights(false);
    setItemsExpandable(true);
    setAllColumnsShowFocus(true);
    setColumnCount(4); 
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);  
    header()->setSectionResizeMode(1, QHeaderView::Fixed);    
    header()->setSectionResizeMode(2, QHeaderView::Fixed);    
    header()->setSectionResizeMode(3, QHeaderView::Fixed);    
    header()->setDefaultSectionSize(80);  
    
    
    setStyleSheet(
        "QTreeWidget { "
        "   background-color: #2D2D30;"
        "   color: #E0E0E0;"
        "   border: none;"
        "   font-size: 12px;"
        "}"
        "QTreeWidget::item { "
        "   height: 28px;"  
        "   padding: 4px 0px;"
        "}"
        "QTreeWidget::item:selected { "
        "   background-color: #3F3F46;"
        "}"
        "QTreeWidget::item:hover { "
        "   background-color: #2A2A2D;"
        "}"
        
        "QScrollBar:vertical {"
        "   background-color: #2D2D30;"
        "   width: 12px;"
        "}"
        "QScrollBar::handle:vertical {"
        "   background-color: #686868;"
        "   min-height: 20px;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "   height: 0px;"
        "}"
    );

    loadIcons();

    
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    
    
    connect(this, &QTreeWidget::itemSelectionChanged, this, &LayerTreeWidget::onItemSelectionChanged);
    
    
  auto documentS = kdl::mem_lock(m_document);
    if (documentS) {
        m_notifierConnection += documentS->selectionDidChangeNotifier.connect(
            this, &LayerTreeWidget::onDocumentSelectionChanged);
    }
}

void LayerTreeWidget::loadIcons()
{
    
    m_worldIcon = io::loadSVGIcon("Map_fullcube.svg");  
    m_layerIcon = io::loadSVGIcon("Layer.svg");  
    m_groupIcon = io::loadSVGIcon("Map_folder.svg");  
    m_entityIcon = io::loadSVGIcon("Map_entity.svg");  
    m_brushIcon = io::loadSVGIcon("Map_cube.svg");  
    m_visibleIcon = io::loadSVGIcon("object_show.svg");
    m_hiddenIcon = io::loadSVGIcon("object_hidden.svg");
    m_lockedIcon = io::loadSVGIcon("Lock_on.svg");
    m_unlockedIcon = io::loadSVGIcon("Lock_off.svg");
}

void LayerTreeWidget::setupTreeItem(QTreeWidgetItem* item, mdl::Node* node)
{
    if (!item || !node) return;

    
    item->setText(0, QString::fromStdString(node->name()));
    item->setData(0, Qt::UserRole, QVariant::fromValue(node));

    
    QFont itemFont = item->font(1);
    itemFont.setPointSize(10);
    item->setFont(1, itemFont);

    
    if (auto* layer = dynamic_cast<mdl::LayerNode*>(node)) {
        item->setIcon(0, m_layerIcon);
        auto count = static_cast<qint64>(layer->childCount());  
        QString countText;
        if(count > 999) {
            countText = QString("%1K").arg(static_cast<double>(count)/1000.0, 0, 'f', 1);
        } else {
            countText = QString::number(count);
        }
        countText += tr(" objects");
        item->setText(1, countText);
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        
        
        item->setIcon(2, node->locked() ? m_lockedIcon : m_unlockedIcon);
        item->setIcon(3, node->visible() ? m_visibleIcon : m_hiddenIcon);
    } else if (auto* group = dynamic_cast<mdl::GroupNode*>(node)) {
        item->setIcon(0, m_groupIcon);
        auto count = static_cast<qint64>(group->childCount());  
        QString countText;
        if(count > 999) {
            countText = QString("%1K").arg(static_cast<double>(count)/1000.0, 0, 'f', 1);
        } else {
            countText = QString::number(count);
        }
        countText += tr(" objects");
        item->setText(1, countText);
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        
        
        item->setIcon(3, node->visible() ? m_visibleIcon : m_hiddenIcon);
    } else if (auto* entity = dynamic_cast<mdl::EntityNode*>(node)) {
        
        if (auto* definition = entity->entity().definition()) {
            if (definition->type() == mdl::EntityDefinitionType::PointEntity) {
                item->setIcon(0, m_entityIcon);  
            } else {
                item->setIcon(0, m_worldIcon);  
            }
        } else {
            
            if (entity->childCount() > 0) {
                item->setIcon(0, m_worldIcon);
            } else {
                item->setIcon(0, m_entityIcon);
            }
        }
        item->setText(0, QString::fromStdString(entity->name()));
        
        
        if (entity->childCount() > 0) {
            auto count = static_cast<qint64>(entity->childCount());
            QString countText;
            if(count > 999) {
                countText = QString("%1K").arg(static_cast<double>(count)/1000.0, 0, 'f', 1);
            } else {
                countText = QString::number(count);
            }
            countText += tr(" brushes");
            item->setText(1, countText);
            item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            
            
            QFont font = item->font(0);
            font.setBold(true);
            item->setFont(0, font);
        }
        
        
        item->setIcon(3, node->visible() ? m_visibleIcon : m_hiddenIcon);
    } else if (dynamic_cast<mdl::BrushNode*>(node)) {
        item->setIcon(0, m_brushIcon);
        
        
        item->setIcon(3, node->visible() ? m_visibleIcon : m_hiddenIcon);
    }
}

void LayerTreeWidget::addEntityToTree(QTreeWidgetItem* parentItem, mdl::Node* node)
{
    auto* item = new QTreeWidgetItem(parentItem);
    setupTreeItem(item, node);
    
    
    if (auto* entityNode = dynamic_cast<mdl::EntityNode*>(node)) {
        
        if (entityNode->childCount() > 0) {
            for (auto* childNode : entityNode->children()) {
                
                if (dynamic_cast<mdl::BrushNode*>(childNode)) {
                    auto* brushItem = new QTreeWidgetItem(item);
                    setupTreeItem(brushItem, childNode);
                } else if (dynamic_cast<mdl::GroupNode*>(childNode)) {
                    addGroupToTree(item, childNode);
                } else if (dynamic_cast<mdl::EntityNode*>(childNode)) {
                    addEntityToTree(item, childNode);
                }
            }
            
            
            if (!entityNode->visible()) {
                updateVisibilityIconRecursively(item, false);
            }
            
            else if (parentItem) {
                auto* parentNode = parentItem->data(0, Qt::UserRole).value<mdl::Node*>();
                if (parentNode && !parentNode->visible()) {
                    updateVisibilityIconRecursively(item, false);
                }
            }
        }
        
        else if (parentItem) {
            auto* parentNode = parentItem->data(0, Qt::UserRole).value<mdl::Node*>();
            if (parentNode && !parentNode->visible()) {
                item->setIcon(3, m_hiddenIcon);
            }
        }
    }
    
    else if (parentItem) {
        auto* parentNode = parentItem->data(0, Qt::UserRole).value<mdl::Node*>();
        if (parentNode && !parentNode->visible()) {
            item->setIcon(3, m_hiddenIcon);
        }
    }
}

void LayerTreeWidget::addGroupToTree(QTreeWidgetItem* parentItem, mdl::Node* node)
{
    if (auto* group = dynamic_cast<mdl::GroupNode*>(node)) {
        auto* item = new QTreeWidgetItem(parentItem);
        setupTreeItem(item, node);

        
        for (auto* child : group->children()) {
            if (dynamic_cast<mdl::GroupNode*>(child)) {
                addGroupToTree(item, child);
            } else {
                addEntityToTree(item, child);
            }
        }
        
        
        if (!group->visible()) {
            updateVisibilityIconRecursively(item, false);
        }
        
        else if (parentItem) {
            auto* parentNode = parentItem->data(0, Qt::UserRole).value<mdl::Node*>();
            if (parentNode && !parentNode->visible()) {
                updateVisibilityIconRecursively(item, false);
            }
        }
    }
}

void LayerTreeWidget::updateTree()
{
    
    std::vector<mdl::Node*> selectedNodesBefore;
    QList<QTreeWidgetItem*> selectedItems = this->selectedItems();
    for (auto* item : selectedItems) {
        auto* node = item->data(0, Qt::UserRole).value<mdl::Node*>();
        if (node) {
            selectedNodesBefore.push_back(node);
        }
    }

    clear();

    auto document = kdl::mem_lock(m_document);
    if (auto* world = document->world()) {
        
        auto* defaultLayer = world->defaultLayer();
        auto* defaultItem = new QTreeWidgetItem(this);
        setupTreeItem(defaultItem, defaultLayer);
        
        
        for (auto* node : defaultLayer->children()) {
            if (auto* group = dynamic_cast<mdl::GroupNode*>(node)) {
                addGroupToTree(defaultItem, group);
            } else {
                addEntityToTree(defaultItem, node);
            }
        }
        
        
        for (auto* layer : world->customLayersUserSorted()) {
            auto* layerItem = new QTreeWidgetItem(this);
            setupTreeItem(layerItem, layer);
            
            
            for (auto* node : layer->children()) {
                if (auto* group = dynamic_cast<mdl::GroupNode*>(node)) {
                    addGroupToTree(layerItem, group);
                } else {
                    addEntityToTree(layerItem, node);
                }
            }
            
            
            if (!layer->visible()) {
                updateVisibilityIconRecursively(layerItem, false);
            }
        }
        
        
        for (int i = 0; i < this->topLevelItemCount(); ++i) {
            this->topLevelItem(i)->setExpanded(true);
            
            
            auto* topItem = this->topLevelItem(i);
            auto* topNode = topItem->data(0, Qt::UserRole).value<mdl::Node*>();
            if (topNode && !topNode->visible()) {
                updateVisibilityIconRecursively(topItem, false);
            }
        }

        
        if (!selectedNodesBefore.empty()) {
            for (auto* node : selectedNodesBefore) {
                findAndSelectNode(node, invisibleRootItem());
            }
        }
    }
}

void LayerTreeWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    auto* item = itemAt(event->pos());
    if (item) {
        
        auto* node = item->data(0, Qt::UserRole).value<mdl::Node*>();
        if (auto* entity = dynamic_cast<mdl::EntityNode*>(node)) {
            if (entity->childCount() > 0) {
                
                if (item->isExpanded()) {
                    
                    auto document = kdl::mem_lock(m_document);
                    if (document) {
                        document->deselectAll();
                        
                        std::vector<mdl::Node*> childNodesToSelect;
                        for (auto* child : entity->children()) {
                            childNodesToSelect.push_back(child);
                        }
                        
                        if (!childNodesToSelect.empty()) {
                            document->selectNodes(childNodesToSelect);
                        }
                    }
                } else {
                    
                    item->setExpanded(true);
                }
                event->accept();
                return;
            }
        }
        
        
        if (node) {
            emit nodeActivated(node);
        }
    }
    QTreeWidget::mouseDoubleClickEvent(event);
}

void LayerTreeWidget::mousePressEvent(QMouseEvent* event)
{
    
    if (event->button() == Qt::LeftButton) {
        m_dragStartPosition = event->pos();
    }
    
    
    if (event->button() == Qt::LeftButton) {
        auto* item = itemAt(event->pos());
        if (item) {
            
            QModelIndex index = indexAt(event->pos());
            if (index.isValid()) {
                
                int indent = indentation();
                int depth = 0;
                QTreeWidgetItem* parent = item->parent();
                while (parent) {
                    depth++;
                    parent = parent->parent();
                }
                
                
                QRect itemRect = visualRect(index);
                int expandIconAreaWidth = indent * (depth + 1);
                QRect expandArea(itemRect.left() - expandIconAreaWidth, itemRect.top(), 
                               expandIconAreaWidth, itemRect.height());
                
                
                if (expandArea.contains(event->pos()) && item->childCount() > 0) {
                    
                    QTreeWidget::mousePressEvent(event);
                    return;
                }
            }
            
            if (auto* node = item->data(0, Qt::UserRole).value<mdl::Node*>()) {
                
                QRect itemRect = visualItemRect(item);
                int lockColumnX = header()->sectionPosition(2); 
                int visibilityColumnX = header()->sectionPosition(3); 
                int lockColumnWidth = header()->sectionSize(2); 
                int visibilityColumnWidth = header()->sectionSize(3); 
                
                QRect lockRect(lockColumnX, itemRect.top(), lockColumnWidth, itemRect.height());
                QRect visibilityRect(visibilityColumnX, itemRect.top(), visibilityColumnWidth, itemRect.height());
                
                
                if (lockRect.contains(event->pos())) {
                    
                    if (auto* layerNode = dynamic_cast<mdl::LayerNode*>(node)) {
                        auto document = kdl::mem_lock(m_document);
                        // document->logger().info() << "Clicked lock icon for layer: " << layerNode->name();
                        
                        
                        emit nodeLockToggled(layerNode);
                        
                        
                        QTimer::singleShot(100, this, [this, layerNode, item]() {
                            item->setIcon(2, layerNode->locked() ? m_lockedIcon : m_unlockedIcon);
                        });
                    }
                    event->accept();
                    return;
                } 
                
                else if (visibilityRect.contains(event->pos())) {
                    
                    auto document = kdl::mem_lock(m_document);
                    // document->logger().info() << "Clicked visibility icon for node: " << node->name();
                    
                    if (auto* layerNode = dynamic_cast<mdl::LayerNode*>(node)) {
                        
                        emit nodeVisibilityToggled(layerNode);
                        
                        
                        QTimer::singleShot(100, this, [this, layerNode, item]() {
                            bool isVisible = layerNode->visible();
                            item->setIcon(3, isVisible ? m_visibleIcon : m_hiddenIcon);
                            
                            
                            updateVisibilityIconRecursively(item, isVisible);
                        });
                    } else {
                        
                        std::vector<mdl::Node*> nodes{node};
                        bool willBeVisible = !node->visible();
                        
                        if (!willBeVisible) {
                            document->hide(nodes);
                        } else {
                            document->show(nodes);
                        }
                        
                        
                        item->setIcon(3, node->visible() ? m_visibleIcon : m_hiddenIcon);
                        
                        
                        if (dynamic_cast<mdl::GroupNode*>(node) || dynamic_cast<mdl::EntityNode*>(node)) {
                            updateVisibilityIconRecursively(item, node->visible());
                        }
                    }
                    event->accept();
                    return;
                } 
                
                else {
                    
                    bool isCtrlPressed = event->modifiers() & Qt::ControlModifier;
                    bool isShiftPressed = event->modifiers() & Qt::ShiftModifier;
                    
                    
                    if (isShiftPressed) {
                        
                        QTreeWidgetItem* focusItem = this->currentItem();
                        if (focusItem && focusItem != item) {
                            
                            if (!isCtrlPressed) {
                                clearSelection();
                            }
                            
                            
                            bool selectionStarted = false;
                            bool shouldReverse = false;
                            QList<QTreeWidgetItem*> itemsToSelect;
                            
                            
                            if (visualItemRect(focusItem).top() > visualItemRect(item).top()) {
                                shouldReverse = true;
                            }
                            
                            
                            if (item->parent() == focusItem->parent()) {
                                
                                QTreeWidgetItem* parent = item->parent();
                                QTreeWidgetItem* startItem = shouldReverse ? item : focusItem;
                                QTreeWidgetItem* endItem = shouldReverse ? focusItem : item;
                                
                                
                                if (!parent) {
                                    for (int i = 0; i < topLevelItemCount(); ++i) {
                                        QTreeWidgetItem* checkItem = topLevelItem(i);
                                        if (!selectionStarted && checkItem == startItem) {
                                            selectionStarted = true;
                                        }
                                        
                                        if (selectionStarted) {
                                            itemsToSelect.append(checkItem);
                                        }
                                        
                                        if (checkItem == endItem) {
                                            break;
                                        }
                                    }
                                }
                                
                                else {
                                    for (int i = 0; i < parent->childCount(); ++i) {
                                        QTreeWidgetItem* checkItem = parent->child(i);
                                        if (!selectionStarted && checkItem == startItem) {
                                            selectionStarted = true;
                                        }
                                        
                                        if (selectionStarted) {
                                            itemsToSelect.append(checkItem);
                                        }
                                        
                                        if (checkItem == endItem) {
                                            break;
                                        }
                                    }
                                }
                                
                                
                                for (QTreeWidgetItem* selectItem : itemsToSelect) {
                                    selectItem->setSelected(true);
                                }
                                
                                event->accept();
                                return;
                            }
                            
                            else {
                                
                                setCurrentItem(item);
                                item->setSelected(true);
                            }
                        } else {
                            
                            if (!isCtrlPressed) {
                                clearSelection();
                            }
                            setCurrentItem(item);
                            item->setSelected(true);
                        }
                    }
                    
                    else {
                        
                        if (!isCtrlPressed) {
                            clearSelection();
                        }
                        
                        
                        if (isCtrlPressed && item->isSelected()) {
                            
                            item->setSelected(false);
                        } else {
                            
                            setCurrentItem(item, 0, QItemSelectionModel::Current);
                            item->setSelected(true);
                        }
                    }
                    
                    
                    if (dynamic_cast<mdl::LayerNode*>(node) && !isCtrlPressed && !isShiftPressed) {
                        auto document = kdl::mem_lock(m_document);
                        if (document) {
                            
                            document->deselectAll();
                        }
                    }
                    
                    event->accept();
                    return;
                }
            }
        }
    } else if (event->button() == Qt::RightButton) {
        auto* item = itemAt(event->pos());
        if (item) {
            if (auto* node = item->data(0, Qt::UserRole).value<mdl::Node*>()) {
                emit nodeRightClicked(node, event->globalPosition().toPoint());
                event->accept();
                return;
            }
        }
    }
    
    
    QTreeWidget::mousePressEvent(event);
}

void LayerTreeWidget::drawRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QTreeWidget::drawRow(painter, option, index);
}


void LayerTreeWidget::keyPressEvent(QKeyEvent* event)
{
    
    if (event->key() == Qt::Key_Escape) {
        
        clearSelection();
        
        
        auto document = kdl::mem_lock(m_document);
        if (document) {
            document->deselectAll();
        }
        
        
        parentWidget()->setFocus();
        event->accept();
        return;
    }
    
    
    QTreeWidget::keyPressEvent(event);
  }


void LayerTreeWidget::dragEnterEvent(QDragEnterEvent* event)
{
    
    if (event->source() == this && 
        event->mimeData()->hasFormat("application/x-trenchronbroom-node")) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}


void LayerTreeWidget::dragMoveEvent(QDragMoveEvent* event)
{
    
    if (!event->mimeData()->hasFormat("application/x-trenchronbroom-node")) {
        event->ignore();
        return;
    }
    
    
    QByteArray itemData = event->mimeData()->data("application/x-trenchronbroom-node");
    QDataStream dataStream(&itemData, QIODevice::ReadOnly);
    quintptr nodePtr;
    dataStream >> nodePtr;
    mdl::Node* sourceNode = reinterpret_cast<mdl::Node*>(nodePtr);
    
    
    QTreeWidgetItem* targetItem = itemAt(event->position().toPoint());
    if (!targetItem) {
        event->ignore();
        return;
    }

    
    auto* targetNode = targetItem->data(0, Qt::UserRole).value<mdl::Node*>();
    if (!targetNode) {
        event->ignore();
        return;
    }
    
    
    if (sourceNode == targetNode) {
        event->ignore();
        return;
    }
    
    
    if (dynamic_cast<mdl::LayerNode*>(sourceNode) && 
        sourceNode->name() == "Default Layer") {
        event->ignore();
        return;
    }

    
    bool canAccept = false;
    
    
    auto* sourceBrush = dynamic_cast<mdl::BrushNode*>(sourceNode);
    
    auto* targetLayer = dynamic_cast<mdl::LayerNode*>(targetNode);
    auto* targetGroup = dynamic_cast<mdl::GroupNode*>(targetNode);
    auto* targetEntity = dynamic_cast<mdl::EntityNode*>(targetNode);
    auto* targetBrush = dynamic_cast<mdl::BrushNode*>(targetNode);
    
    
    if (targetLayer) {
        
        canAccept = true;
    }
    else if (targetGroup) {
        
        if (!dynamic_cast<mdl::LayerNode*>(sourceNode)) {
            canAccept = true;
        }
    }
    else if (targetEntity) {
        
        if (auto* definition = targetEntity->entity().definition()) {
            if (definition->type() == mdl::EntityDefinitionType::BrushEntity) {
                
                if (sourceBrush) {
                    canAccept = true;
                }
            } else {
                
                canAccept = false;
            }
        } else {
            
            if (targetEntity->childCount() > 0) {
                
                if (sourceBrush) {
                    canAccept = true;
                }
            } else {
                
                canAccept = false;
            }
        }
    }
    else if (targetBrush) {
        
        
        canAccept = false;
    }
    
    if (canAccept) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}


void LayerTreeWidget::dropEvent(QDropEvent* event)
{
    
    if (!event->mimeData()->hasFormat("application/x-trenchronbroom-node")) {
        event->ignore();
        return;
    }
    
    
    QByteArray itemData = event->mimeData()->data("application/x-trenchronbroom-node");
    QDataStream dataStream(&itemData, QIODevice::ReadOnly);
    quintptr nodePtr;
    dataStream >> nodePtr;
    mdl::Node* sourceNode = reinterpret_cast<mdl::Node*>(nodePtr);
    
    
    QTreeWidgetItem* targetItem = itemAt(event->position().toPoint());
    if (!targetItem) {
        event->ignore();
        return;
    }
    
    
    auto* targetNode = targetItem->data(0, Qt::UserRole).value<mdl::Node*>();
    if (!targetNode) {
        event->ignore();
        return;
    }
    
    
    if (sourceNode == targetNode) {
        event->ignore();
        return;
    }
    
    
    if (sourceNode->locked()) {
        
        event->ignore();
        return;
    }
    
    if (!targetNode->visible() || targetNode->locked()) {
        
        event->ignore();
        return;
    }
    
    auto document = kdl::mem_lock(m_document);
    if (!document) {
        event->ignore();
        return;
    }
    
    try {
        
        auto* sourceBrush = dynamic_cast<mdl::BrushNode*>(sourceNode);
        auto* targetEntity = dynamic_cast<mdl::EntityNode*>(targetNode);
        
        if (sourceBrush && targetEntity) {
            
            const auto nodesToMove = std::vector<mdl::Node*>{sourceNode};
            
            
            document->deselectAll();
            
            
            std::map<mdl::Node*, std::vector<mdl::Node*>> reparentMap;
            reparentMap[targetEntity] = nodesToMove;
            
            if (!document->reparentNodes(reparentMap)) {
                event->ignore();
                return;
            }
            
            
            document->selectNodes(nodesToMove);
            
            
            targetItem->setExpanded(true);
            
            
            event->acceptProposedAction();
            return;
        }
        
        
        try {
            
            
            std::map<mdl::Node*, std::vector<mdl::Node*>> nodesToReparent;
            nodesToReparent[targetNode].push_back(sourceNode);
            
            
            auto selectedNode = sourceNode;
            document->deselectAll();
            
            
            if (document->reparentNodes(nodesToReparent)) {
                
                document->selectNodes({selectedNode});
                
                
                targetItem->setExpanded(true);
                
                
                event->acceptProposedAction();
            } else {
                event->ignore();
            }
        } catch (const std::exception& e) {
            document->logger().error() << "Error during node reparenting: " << e.what();
            event->ignore();
        }
    } catch (const std::exception& e) {
        document->logger().error() << "Error during node reparenting: " << e.what();
        event->ignore();
    }
}


void LayerTreeWidget::syncSelectionFromDocument()
{
    auto document = kdl::mem_lock(m_document);
    if (!document) return;
    
    try {
        
        const auto& selectedNodes = document->selectedNodes().nodes();
        if (selectedNodes.empty()) {
            clearSelection();
            return;
        }
        
        
        bool isReparentingOrEntityCreation = false;
        try {
            
            
            for (const auto* node : selectedNodes) {
                if (auto* entityNode = dynamic_cast<const mdl::EntityNode*>(node)) {
                    if (entityNode->childCount() > 0) {
                        
                        isReparentingOrEntityCreation = true;
                        break;
                    }
                }
            }
        } catch (const std::exception&) {
            
        }
        
        
        const mdl::EntityNode* parentEntity = nullptr;
        bool allAreChildrenOfSameEntity = true;
        bool containsAllChildren = true;
        
        
        for (const auto* node : selectedNodes) {
            const mdl::Node* parent = node->parent();
            if (!parent || !dynamic_cast<const mdl::EntityNode*>(parent)) {
                allAreChildrenOfSameEntity = false;
                break;
            }
            
            if (!parentEntity) {
                parentEntity = dynamic_cast<const mdl::EntityNode*>(parent);
            } else if (parent != parentEntity) {
                allAreChildrenOfSameEntity = false;
                break;
            }
        }
        
        
        if (allAreChildrenOfSameEntity && parentEntity) {
            
            if (parentEntity->childCount() == selectedNodes.size()) {
                std::set<const mdl::Node*> selectedNodeSet(selectedNodes.begin(), selectedNodes.end());
                for (const auto* child : parentEntity->children()) {
                    if (selectedNodeSet.find(child) == selectedNodeSet.end()) {
                        containsAllChildren = false;
                        break;
                    }
                }
                
                
                if (containsAllChildren) {
                    clearSelection();
                    
                    
                    for (int i = 0; i < topLevelItemCount(); ++i) {
                        QTreeWidgetItem* foundItem = findNodeItemRecursive(parentEntity, topLevelItem(i));
                        if (foundItem) {
                            
                            foundItem->setSelected(true);
                            foundItem->setExpanded(false);
                            scrollToItem(foundItem);
                            
                            
                            if (!m_syncingSelection) {
                                document->selectNodes({const_cast<mdl::EntityNode*>(parentEntity)});
                            }
                            return;
                        }
                    }
                }
            }
        }
        
        
        clearSelection();
        QList<QTreeWidgetItem*> itemsToSelect;
        
        for (const auto* node : selectedNodes) {
            
            QTreeWidgetItem* foundItem = nullptr;
            for (int i = 0; i < topLevelItemCount(); ++i) {
                foundItem = findNodeItemRecursive(node, topLevelItem(i));
                if (foundItem) {
                    if (!itemsToSelect.contains(foundItem)) {
                        itemsToSelect.append(foundItem);
                    }
                    break;
                }
            }
            
            
            if (!foundItem || isReparentingOrEntityCreation) {
                
                
                const mdl::Node* parent = node->parent();
                while (parent && !dynamic_cast<const mdl::LayerNode*>(parent)) {
                    QTreeWidgetItem* parentItem = nullptr;
                    for (int i = 0; i < topLevelItemCount(); ++i) {
                        parentItem = findNodeItemRecursive(parent, topLevelItem(i));
                        if (parentItem) {
                            if (isReparentingOrEntityCreation && !itemsToSelect.contains(parentItem)) {
                                itemsToSelect.append(parentItem); 
                            }
                            parentItem->setExpanded(true);
                            break;
                        }
                    }
                    parent = parent->parent();
                }
            }
        }
        
        
        for (auto* item : itemsToSelect) {
            item->setSelected(true);
            
            QTreeWidgetItem* parent = item->parent();
            while (parent) {
                parent->setExpanded(true);
                parent = parent->parent();
            }
        }
        
        if (!itemsToSelect.isEmpty()) {
            scrollToItem(itemsToSelect.first()); 
        }
    } catch (const std::exception& e) {
        auto docLog = kdl::mem_lock(m_document);
        if (docLog) {
            docLog->logger().error() << "Error during selection sync: " << e.what();
        }
    }
}


void LayerTreeWidget::onDocumentSelectionChanged(const Selection& /* selection */)
{
    if (!m_syncingSelection) {
        m_syncingSelection = true;
        try {
            syncSelectionFromDocument();
        } catch (const std::exception& e) {
            auto docLog = kdl::mem_lock(m_document);
            if (docLog) {
                docLog->logger().error() << "Error during document selection changed: " << e.what();
            }
        }
        m_syncingSelection = false;
    }
}


QTreeWidgetItem* LayerTreeWidget::findNodeItemRecursive(const mdl::Node* targetNode, QTreeWidgetItem* startItem)
{
    if (!startItem) return nullptr;
    
    
    auto* node = startItem->data(0, Qt::UserRole).value<mdl::Node*>();
    if (node == targetNode) {
        return startItem;
    }
    
    
    for (int i = 0; i < startItem->childCount(); ++i) {
        QTreeWidgetItem* result = findNodeItemRecursive(targetNode, startItem->child(i));
        if (result) {
            return result;
        }
    }
    
    return nullptr;
}


bool LayerTreeWidget::findAndSelectNode(const mdl::Node* targetNode, QTreeWidgetItem* startItem)
{
    QTreeWidgetItem* foundItem = findNodeItemRecursive(targetNode, startItem);
    if (foundItem) {
        foundItem->setSelected(true);
        scrollToItem(foundItem);
        return true;
    }
    return false;
}


void LayerTreeWidget::syncSelectionToDocument()
{
    auto document = kdl::mem_lock(m_document);
    if (!document) return;
    
    try {
        const auto selectedItems = this->selectedItems();
        if (selectedItems.empty()) {
            return; 
        }
        
        if (selectedItems.size() == 1) {
            auto* item = selectedItems.first();
            auto* node = item->data(0, Qt::UserRole).value<mdl::Node*>();
            if (node && dynamic_cast<mdl::LayerNode*>(node)) {
                
                return;
            } else if (node) {
                
                document->deselectAll();
                document->selectNodes({node});
            }
        } else {
            
            bool hasLayerNode = false;
            std::vector<mdl::Node*> nodesToSelect;
            
            
            for (auto* item : selectedItems) {
                auto* node = item->data(0, Qt::UserRole).value<mdl::Node*>();
                if (node) {
                    if (dynamic_cast<mdl::LayerNode*>(node)) {
                        hasLayerNode = true;
                    } else {
                        nodesToSelect.push_back(node);
                    }
                }
            }
            
            
            if (!hasLayerNode && !nodesToSelect.empty()) {
                document->deselectAll();
                document->selectNodes(nodesToSelect);
            }
        }
    } catch (const std::exception& e) {
        auto documentPtr = kdl::mem_lock(m_document);
        if (documentPtr) {
            documentPtr->logger().error() << "Error during selection sync to document: " << e.what();
        }
    }
}


void LayerTreeWidget::onItemSelectionChanged()
{
    if (!m_syncingSelection) {
        m_syncingSelection = true;
        
        
        QList<QTreeWidgetItem*> selectedItems = this->selectedItems();
        if (selectedItems.size() == 1) {
            
            QTreeWidgetItem* item = selectedItems.first();
            auto* node = item->data(0, Qt::UserRole).value<mdl::Node*>();
            
            
            if (node && dynamic_cast<mdl::EntityNode*>(node)) {
                if (node->childCount() > 0) {
                    
                    for (int i = 0; i < topLevelItemCount(); ++i) {
                        collapseOtherEntities(topLevelItem(i), item);
                    }
                }
            }
        }
        
        syncSelectionToDocument();
        m_syncingSelection = false;
    }
}


void LayerTreeWidget::collapseOtherEntities(QTreeWidgetItem* currentItem, QTreeWidgetItem* selectedItem)
{
    if (!currentItem || currentItem == selectedItem) return;
    
    
    auto* node = currentItem->data(0, Qt::UserRole).value<mdl::Node*>();
    if (dynamic_cast<mdl::EntityNode*>(node) && currentItem->childCount() > 0 && currentItem->isExpanded()) {
        
        currentItem->setExpanded(false);
    }
    
    
    for (int i = 0; i < currentItem->childCount(); ++i) {
        collapseOtherEntities(currentItem->child(i), selectedItem);
    }
}

LayerListBox::LayerListBox(std::weak_ptr<MapDocument> document, QWidget* parent)
    : QWidget(parent)
    , m_document(std::move(document))
    , m_currentSortMode(0)
{
    createGui();
    connectObservers();
    updateTree();  
    
    
    sortTree(0);
}

void LayerListBox::createGui()
{
    
    m_searchBox = new QLineEdit();
    m_searchBox->setPlaceholderText(tr("Search..."));
    m_searchBox->setStyleSheet(
        "QLineEdit {"
        "   background-color: #3F3F46;"
        "   color: #E0E0E0;"
        "   border: 1px solid #2D2D30;"
        "   border-radius: 2px;"
        "   padding: 4px;"
        "   height: 24px;"
        "}"
    );
    
    m_sortOptions = new QComboBox();
    m_sortOptions->addItems({tr("Name"), tr("Type"), tr("Custom")});
    m_sortOptions->setStyleSheet(
        "QComboBox {"
        "   background-color: #3F3F46;"
        "   color: #E0E0E0;"
        "   border: 1px solid #2D2D30;"
        "   border-radius: 2px;"
        "   padding: 4px;"
        "   height: 24px;"
        "   min-width: 100px;"
        "}"
        "QComboBox::drop-down {"
        "   border: none;"
        "}"
        "QComboBox::down-arrow {"
        "   image: none;"
        "   width: 12px;"
        "}"
    );

    
    m_treeWidget = new LayerTreeWidget(m_document, this);
    m_treeWidget->setMinimumHeight(400);  
    m_treeWidget->setIndentation(20);  
    m_treeWidget->setIconSize(QSize(16, 16));
    
    
    m_treeWidget->setColumnWidth(0, 240); 
    m_treeWidget->setColumnWidth(1, 100); 
    m_treeWidget->setColumnWidth(2, 40);  
    m_treeWidget->setColumnWidth(3, 40);  

    
    auto* toolbarLayout = new QHBoxLayout();
    toolbarLayout->setContentsMargins(
        LayoutConstants::MediumHMargin,
        LayoutConstants::MediumVMargin, 
        LayoutConstants::MediumHMargin,
        LayoutConstants::MediumVMargin);
    toolbarLayout->setSpacing(LayoutConstants::WideHMargin);  
    toolbarLayout->addWidget(m_searchBox, 1);
    toolbarLayout->addWidget(m_sortOptions);

    
    auto* mainLayout = new QVBoxLayout();
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(LayoutConstants::MediumVMargin);
    mainLayout->addLayout(toolbarLayout);
    mainLayout->addWidget(m_treeWidget, 1);

    setLayout(mainLayout);
    setMinimumWidth(450);  

    
    connect(m_searchBox, &QLineEdit::textChanged, this, &LayerListBox::filterTree);
    connect(m_sortOptions, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &LayerListBox::sortTree);
    connect(m_treeWidget, &QTreeWidget::itemSelectionChanged, this, [this]() {
        auto* layer = selectedLayer();
        if (layer) {
            emit layerSelected(layer);
        }
        emit itemSelectionChanged();
    });
    
    
    connect(m_treeWidget, &LayerTreeWidget::nodeVisibilityToggled, this,
            [this](mdl::Node* node) {
                if (auto* layer = dynamic_cast<mdl::LayerNode*>(node)) {
                    emit layerVisibilityToggled(layer);
                }
            });
    
    connect(m_treeWidget, &LayerTreeWidget::nodeLockToggled, this,
            [this](mdl::Node* node) {
                if (auto* layer = dynamic_cast<mdl::LayerNode*>(node)) {
                    emit layerLockToggled(layer);
  }
            });
    
    connect(m_treeWidget, &LayerTreeWidget::nodeActivated, this,
            [this](mdl::Node* node) {
                if (auto* layer = dynamic_cast<mdl::LayerNode*>(node)) {
                    emit layerSetCurrent(layer);
                }
            });
    
    connect(m_treeWidget, &LayerTreeWidget::nodeRightClicked, this,
            [this](mdl::Node* node, const QPoint& /* pos */) {
                if (auto* layer = dynamic_cast<mdl::LayerNode*>(node)) {
                    emit layerRightClicked(layer);
                }
            });
}

void LayerListBox::connectObservers()
{
  auto document = kdl::mem_lock(m_document);
    m_notifierConnection += document->documentWasNewedNotifier.connect(
        this, &LayerListBox::documentDidChange);
    m_notifierConnection += document->documentWasLoadedNotifier.connect(
        this, &LayerListBox::documentDidChange);
    m_notifierConnection += document->nodesWereAddedNotifier.connect(
        this, &LayerListBox::nodesDidChange);
    m_notifierConnection += document->nodesWereRemovedNotifier.connect(
        this, &LayerListBox::nodesDidChange);
    m_notifierConnection += document->nodesDidChangeNotifier.connect(
    this, &LayerListBox::nodesDidChange);
}

void LayerListBox::documentDidChange(MapDocument*)
{
    updateTree();
    
    
    if (m_currentSortMode == 0) {
        
        m_treeWidget->sortItems(0, Qt::AscendingOrder);
    } else if (m_currentSortMode == 1) {
        
        sortByType();
    }
    
}

void LayerListBox::nodesDidChange(const std::vector<mdl::Node*>& nodes)
{
    
    bool needFullUpdate = false;
    
    
    if (nodes.empty()) {
        needFullUpdate = true;
    } else {
        for (mdl::Node* node : nodes) {
            
            if (dynamic_cast<mdl::LayerNode*>(node) || 
                dynamic_cast<mdl::GroupNode*>(node) ||
                (node->parent() && (dynamic_cast<mdl::LayerNode*>(node->parent()) || 
                                  dynamic_cast<mdl::GroupNode*>(node->parent())))) {
                needFullUpdate = true;
                break;
            }
        }
    }
    
    if (needFullUpdate) {
        updateTree(); 
    } else {
        
        for (mdl::Node* node : nodes) {
            m_treeWidget->updateNodeItem(node);
        }
    }
}

void LayerListBox::filterTree(const QString& text)
  {
    
    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_treeWidget->topLevelItem(i);
        showAllItems(item);
    }
    
    
    if (text.isEmpty()) {
    return;
  }
    
    
    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_treeWidget->topLevelItem(i);
        bool matches = itemMatchesFilter(item, text);
        item->setHidden(!matches);
}
}


void LayerListBox::showAllItems(QTreeWidgetItem* item)
{
    if (!item) return;
    
    item->setHidden(false);
    
    
    for (int i = 0; i < item->childCount(); ++i) {
        showAllItems(item->child(i));
  }
}


bool LayerListBox::itemMatchesFilter(QTreeWidgetItem* item, const QString& text)
{
    if (!item) return false;
    
    
    bool match = item->text(0).contains(text, Qt::CaseInsensitive);
    
    
    for (int i = 0; i < item->childCount(); ++i) {
        QTreeWidgetItem* childItem = item->child(i);
        bool childMatch = itemMatchesFilter(childItem, text);
        match = match || childMatch;
        
        
        childItem->setHidden(!childMatch);
    }
    
    
    item->setHidden(!match);
    
    return match;
}

void LayerListBox::sortTree(int index)
{
    
    auto* currentLayer = selectedLayer();
    
    
    m_currentSortMode = index;
    
    
    switch (index) {
        case 0: 
            m_treeWidget->sortItems(0, Qt::AscendingOrder);
            break;
        case 1: 
            
            sortByType();
            break;
        case 2: 
            updateTree();
            break;
    }
    
    
    if (currentLayer) {
        setSelectedLayer(currentLayer);
    }
}

void LayerListBox::sortByType()
{
    
    QList<QTreeWidgetItem*> items;
    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        items.append(m_treeWidget->takeTopLevelItem(0));
    }
    
    
    std::sort(items.begin(), items.end(), [](const QTreeWidgetItem* a, const QTreeWidgetItem* b) {
        auto* nodeA = a->data(0, Qt::UserRole).value<mdl::Node*>();
        auto* nodeB = b->data(0, Qt::UserRole).value<mdl::Node*>();
        
        auto* layerA = dynamic_cast<mdl::LayerNode*>(nodeA);
        auto* layerB = dynamic_cast<mdl::LayerNode*>(nodeB);
        
        if (layerA && layerB) {
            
            bool isDefaultA = layerA->name() == "Default Layer";
            bool isDefaultB = layerB->name() == "Default Layer";
            
            if (isDefaultA != isDefaultB) {
                return isDefaultA;
            }
            
            
            return layerA->name() < layerB->name();
        }
        
        return false;
    });
    
    
    for (auto* item : items) {
        m_treeWidget->addTopLevelItem(item);
    }
}

mdl::LayerNode* LayerListBox::selectedLayer() const
{
    auto* item = m_treeWidget->currentItem();
    if (item) {
        auto* node = item->data(0, Qt::UserRole).value<mdl::Node*>();
        return dynamic_cast<mdl::LayerNode*>(node);
  }
  return nullptr;
}

void LayerListBox::setSelectedLayer(mdl::LayerNode* layer)
{
    if (!layer) return;
    
    
    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        auto* item = m_treeWidget->topLevelItem(i);
        auto* node = item->data(0, Qt::UserRole).value<mdl::Node*>();
        
        if (node == layer) {
            m_treeWidget->setCurrentItem(item);
            return;
        }
    }
}

void LayerListBox::updateSelectionForRemoval()
{
    
    int currentIndex = -1;
    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        if (m_treeWidget->topLevelItem(i) == m_treeWidget->currentItem()) {
            currentIndex = i;
            break;
        }
    }
    
    
    if (currentIndex >= 0) {
        if (currentIndex < m_treeWidget->topLevelItemCount() - 1) {
            m_treeWidget->setCurrentItem(m_treeWidget->topLevelItem(currentIndex + 1));
        } else if (currentIndex > 0) {
            m_treeWidget->setCurrentItem(m_treeWidget->topLevelItem(currentIndex - 1));
        } else {
            m_treeWidget->setCurrentItem(nullptr);
        }
    }
}

void LayerListBox::updateTree()
{
    
    m_treeWidget->updateTree();
    
    
    
    int tempSortMode = m_currentSortMode;
    m_currentSortMode = -1; 
    
    switch (tempSortMode) {
        case 0: 
            m_treeWidget->sortItems(0, Qt::AscendingOrder);
            break;
        case 1: 
            sortByType();
            break;
        
    }
    
    
    m_currentSortMode = tempSortMode;
}


void LayerTreeWidget::updateNodeItem(mdl::Node* node)
{
    if (!node) return;
    
    
    QTreeWidgetItem* item = findNodeItem(node, invisibleRootItem());
    if (item) {
        
        item->setText(0, QString::fromStdString(node->name()));
        
        
        if (auto* layerNode = dynamic_cast<mdl::LayerNode*>(node)) {
            
            item->setIcon(2, node->locked() ? m_lockedIcon : m_unlockedIcon);
            item->setIcon(3, node->visible() ? m_visibleIcon : m_hiddenIcon);
            
            
            auto count = static_cast<qint64>(layerNode->childCount());
            QString countText;
            if(count > 999) {
                countText = QString("%1K").arg(static_cast<double>(count)/1000.0, 0, 'f', 1);
            } else {
                countText = QString::number(count);
            }
            countText += tr(" objects");
            item->setText(1, countText);
        }
        else if (auto* group = dynamic_cast<mdl::GroupNode*>(node)) {
            
            item->setIcon(3, node->visible() ? m_visibleIcon : m_hiddenIcon);
            
            
            auto count = static_cast<qint64>(group->childCount());
            QString countText;
            if(count > 999) {
                countText = QString("%1K").arg(static_cast<double>(count)/1000.0, 0, 'f', 1);
            } else {
                countText = QString::number(count);
            }
            countText += tr(" objects");
            item->setText(1, countText);
        }
        else {
            
            item->setIcon(3, node->visible() ? m_visibleIcon : m_hiddenIcon);
        }
    }
}


QTreeWidgetItem* LayerTreeWidget::findNodeItem(mdl::Node* targetNode, QTreeWidgetItem* startItem)
{
    if (!startItem) return nullptr;
    
    
    auto* node = startItem->data(0, Qt::UserRole).value<mdl::Node*>();
    if (node == targetNode) {
        return startItem;
    }
    
    
    for (int i = 0; i < startItem->childCount(); ++i) {
        QTreeWidgetItem* result = findNodeItem(targetNode, startItem->child(i));
        if (result) {
            return result;
        }
    }
    
    return nullptr;
}


void LayerTreeWidget::updateVisibilityIconRecursively(QTreeWidgetItem* item, bool isVisible)
{
    if (!item) return;
    
    
    item->setIcon(3, isVisible ? m_visibleIcon : m_hiddenIcon);
    
    
    for (int i = 0; i < item->childCount(); ++i) {
        updateVisibilityIconRecursively(item->child(i), isVisible);
    }
}


void LayerTreeWidget::mouseMoveEvent(QMouseEvent* event)
{
    
    if (!(event->buttons() & Qt::LeftButton)) {
        QTreeWidget::mouseMoveEvent(event);
        return;
    }
    
    
    if ((event->pos() - m_dragStartPosition).manhattanLength() < QApplication::startDragDistance()) {
        
        event->accept();
        return;
    }
    
    
    QList<QTreeWidgetItem*> selectedItems = this->selectedItems();
    if (selectedItems.size() != 1) {
        event->accept();
        return;
    }
    
    QTreeWidgetItem* currentItem = selectedItems.first();
    if (!currentItem) {
        event->accept();
        return;
    }
    
    
    auto* node = currentItem->data(0, Qt::UserRole).value<mdl::Node*>();
    if (!node || node->locked()) {
        event->accept();
        return;
    }
    
    
    QDrag* drag = new QDrag(this);
    QMimeData* mimeData = new QMimeData;
    
    
    QByteArray itemData;
    QDataStream dataStream(&itemData, QIODevice::WriteOnly);
    quintptr nodePtr = reinterpret_cast<quintptr>(node);
    dataStream << nodePtr;
    mimeData->setData("application/x-trenchronbroom-node", itemData);
    
    drag->setMimeData(mimeData);
    
    
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    QIcon icon = currentItem->icon(0);
    icon.paint(&painter, QRect(0, 0, 32, 32));
    painter.end();
    
    drag->setPixmap(pixmap);
    drag->setHotSpot(QPoint(16, 16));
    
    
    drag->exec(Qt::MoveAction);
    event->accept();
}

} // namespace tb::ui

