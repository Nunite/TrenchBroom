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
    setSelectionMode(QAbstractItemView::ExtendedSelection); // 改为ExtendedSelection以支持多选
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setExpandsOnDoubleClick(false);
    setRootIsDecorated(true);
    setUniformRowHeights(false);
    setItemsExpandable(true);
    setAllColumnsShowFocus(true);
    setColumnCount(4); // 名称、对象数量、锁定按钮、可见性按钮（交换顺序）
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);  // 名称列自动拉伸
    header()->setSectionResizeMode(1, QHeaderView::Fixed);    // 数量列固定宽度
    header()->setSectionResizeMode(2, QHeaderView::Fixed);    // 锁定按钮列固定宽度
    header()->setSectionResizeMode(3, QHeaderView::Fixed);    // 可见性按钮列固定宽度
    header()->setDefaultSectionSize(80);  // 增加默认列宽
    
    // 美化样式
    setStyleSheet(
        "QTreeWidget { "
        "   background-color: #2D2D30;"
        "   color: #E0E0E0;"
        "   border: none;"
        "   font-size: 12px;"
        "}"
        "QTreeWidget::item { "
        "   height: 28px;"  // 增加项目高度
        "   padding: 4px 0px;"
        "}"
        "QTreeWidget::item:selected { "
        "   background-color: #3F3F46;"
        "}"
        "QTreeWidget::item:hover { "
        "   background-color: #2A2A2D;"
        "}"
        // 优化滚动条样式
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

    // 启用自定义拖放
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    
    // 连接选择变化信号
    connect(this, &QTreeWidget::itemSelectionChanged, this, &LayerTreeWidget::onItemSelectionChanged);
    
    // 在文档中监听选择变化
  auto documentS = kdl::mem_lock(m_document);
    if (documentS) {
        m_notifierConnection += documentS->selectionDidChangeNotifier.connect(
            this, &LayerTreeWidget::onDocumentSelectionChanged);
    }
}

void LayerTreeWidget::loadIcons()
{
    // 加载各种图标
    m_worldIcon = io::loadSVGIcon("Map_fullcube.svg");  // 世界刷子（实心立方体）
    m_layerIcon = io::loadSVGIcon("Layer.svg");  // 图层图标
    m_groupIcon = io::loadSVGIcon("Map_folder.svg");  // 分组（线框文件夹）
    m_entityIcon = io::loadSVGIcon("Map_entity.svg");  // 点实体（线框基准）
    m_brushIcon = io::loadSVGIcon("Map_cube.svg");  // 刷子实体（线框立方体）
    m_visibleIcon = io::loadSVGIcon("object_show.svg");
    m_hiddenIcon = io::loadSVGIcon("object_hidden.svg");
    m_lockedIcon = io::loadSVGIcon("Lock_on.svg");
    m_unlockedIcon = io::loadSVGIcon("Lock_off.svg");
}

void LayerTreeWidget::setupTreeItem(QTreeWidgetItem* item, mdl::Node* node)
{
    if (!item || !node) return;

    // 设置基本属性
    item->setText(0, QString::fromStdString(node->name()));
    item->setData(0, Qt::UserRole, QVariant::fromValue(node));

    // 设置字体
    QFont itemFont = item->font(1);
    itemFont.setPointSize(10);
    item->setFont(1, itemFont);

    // 根据节点类型设置图标和对象数量
    if (auto* layer = dynamic_cast<mdl::LayerNode*>(node)) {
        item->setIcon(0, m_layerIcon);
        auto count = static_cast<qint64>(layer->childCount());  // 使用qint64避免溢出
        QString countText;
        if(count > 999) {
            countText = QString("%1K").arg(count/1000.0, 0, 'f', 1);
        } else {
            countText = QString::number(count);
        }
        countText += tr(" objects");
        item->setText(1, countText);
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        
        // 设置锁定和可见性图标
        item->setIcon(2, node->locked() ? m_lockedIcon : m_unlockedIcon);
        item->setIcon(3, node->visible() ? m_visibleIcon : m_hiddenIcon);
    } else if (auto* group = dynamic_cast<mdl::GroupNode*>(node)) {
        item->setIcon(0, m_groupIcon);
        auto count = static_cast<qint64>(group->childCount());  // 使用qint64避免溢出
        QString countText;
        if(count > 999) {
            countText = QString("%1K").arg(count/1000.0, 0, 'f', 1);
        } else {
            countText = QString::number(count);
        }
        countText += tr(" objects");
        item->setText(1, countText);
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        
        // 非图层节点只设置可见性图标
        item->setIcon(3, node->visible() ? m_visibleIcon : m_hiddenIcon);
    } else if (auto* entity = dynamic_cast<mdl::EntityNode*>(node)) {
        // 根据实体定义类型设置图标
        if (auto* definition = entity->entity().definition()) {
            if (definition->type() == mdl::EntityDefinitionType::PointEntity) {
                item->setIcon(0, m_entityIcon);  // 点实体使用实体图标
            } else {
                item->setIcon(0, m_worldIcon);  // 刷子实体使用世界图标
            }
        } else {
            // 如果没有定义，回退到基于子节点判断
            if (entity->childCount() > 0) {
                item->setIcon(0, m_worldIcon);
            } else {
                item->setIcon(0, m_entityIcon);
            }
        }
        item->setText(0, QString::fromStdString(entity->name()));
        
        // 如果实体包含子节点，显示数量
        if (entity->childCount() > 0) {
            auto count = static_cast<qint64>(entity->childCount());
            QString countText;
            if(count > 999) {
                countText = QString("%1K").arg(count/1000.0, 0, 'f', 1);
            } else {
                countText = QString::number(count);
            }
            countText += tr(" brushes");
            item->setText(1, countText);
            item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            
            // 为可展开的实体设置特殊风格
            QFont font = item->font(0);
            font.setBold(true);
            item->setFont(0, font);
        }
        
        // 非图层节点只设置可见性图标
        item->setIcon(3, node->visible() ? m_visibleIcon : m_hiddenIcon);
    } else if (auto* brush = dynamic_cast<mdl::BrushNode*>(node)) {
        item->setIcon(0, m_brushIcon);
        
        // 非图层节点只设置可见性图标
        item->setIcon(3, node->visible() ? m_visibleIcon : m_hiddenIcon);
    }
}

void LayerTreeWidget::addEntityToTree(QTreeWidgetItem* parentItem, mdl::Node* node)
{
    auto* item = new QTreeWidgetItem(parentItem);
    setupTreeItem(item, node);
    
    // 检查是否为实体节点并且有子节点
    if (auto* entityNode = dynamic_cast<mdl::EntityNode*>(node)) {
        // 如果实体有子节点（brush或其他），则添加到树中使其可展开
        if (entityNode->childCount() > 0) {
            for (auto* childNode : entityNode->children()) {
                // 递归添加子节点
                if (dynamic_cast<mdl::BrushNode*>(childNode)) {
                    auto* brushItem = new QTreeWidgetItem(item);
                    setupTreeItem(brushItem, childNode);
                } else if (dynamic_cast<mdl::GroupNode*>(childNode)) {
                    addGroupToTree(item, childNode);
                } else if (dynamic_cast<mdl::EntityNode*>(childNode)) {
                    addEntityToTree(item, childNode);
                }
            }
            
            // 如果实体不可见，递归设置其所有子项的可见性图标
            if (!entityNode->visible()) {
                updateVisibilityIconRecursively(item, false);
            }
            // 或者如果父节点不可见，也要更新图标
            else if (parentItem) {
                auto* parentNode = parentItem->data(0, Qt::UserRole).value<mdl::Node*>();
                if (parentNode && !parentNode->visible()) {
                    updateVisibilityIconRecursively(item, false);
                }
            }
        }
        // 即使没有子节点，也检查父节点可见性
        else if (parentItem) {
            auto* parentNode = parentItem->data(0, Qt::UserRole).value<mdl::Node*>();
            if (parentNode && !parentNode->visible()) {
                item->setIcon(3, m_hiddenIcon);
            }
        }
    }
    // 如果是其他节点类型（如Brush），也检查父节点可见性
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

        // 递归添加组内的所有节点
        for (auto* child : group->children()) {
            if (dynamic_cast<mdl::GroupNode*>(child)) {
                addGroupToTree(item, child);
            } else {
                addEntityToTree(item, child);
            }
        }
        
        // 如果组不可见，递归设置其所有子项的可见性图标
        if (!group->visible()) {
            updateVisibilityIconRecursively(item, false);
        }
        // 或者如果父节点不可见，也要更新图标
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
    // 保存当前选择的节点
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
        // 添加默认图层
        auto* defaultLayer = world->defaultLayer();
        auto* defaultItem = new QTreeWidgetItem(this);
        setupTreeItem(defaultItem, defaultLayer);
        
        // 添加默认图层下的所有对象
        for (auto* node : defaultLayer->children()) {
            if (auto* group = dynamic_cast<mdl::GroupNode*>(node)) {
                addGroupToTree(defaultItem, group);
            } else {
                addEntityToTree(defaultItem, node);
            }
        }
        
        // 添加自定义图层
        for (auto* layer : world->customLayersUserSorted()) {
            auto* layerItem = new QTreeWidgetItem(this);
            setupTreeItem(layerItem, layer);
            
            // 添加图层中的所有实体和组
            for (auto* node : layer->children()) {
                if (auto* group = dynamic_cast<mdl::GroupNode*>(node)) {
                    addGroupToTree(layerItem, group);
                } else {
                    addEntityToTree(layerItem, node);
                }
            }
            
            // 如果图层不可见，递归设置其所有子项的可见性图标
            if (!layer->visible()) {
                updateVisibilityIconRecursively(layerItem, false);
            }
        }
        
        // 展开顶级项
        for (int i = 0; i < this->topLevelItemCount(); ++i) {
            this->topLevelItem(i)->setExpanded(true);
            
            // 检查默认图层的可见性
            auto* topItem = this->topLevelItem(i);
            auto* topNode = topItem->data(0, Qt::UserRole).value<mdl::Node*>();
            if (topNode && !topNode->visible()) {
                updateVisibilityIconRecursively(topItem, false);
            }
        }

        // 恢复选择
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
        // 检查是否为实体节点且有子节点
        auto* node = item->data(0, Qt::UserRole).value<mdl::Node*>();
        if (auto* entity = dynamic_cast<mdl::EntityNode*>(node)) {
            if (entity->childCount() > 0) {
                // 区分单击和双击的行为
                if (item->isExpanded()) {
                    // 如果实体已展开，双击选择所有子节点
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
                    // 如果实体折叠，双击展开
                    item->setExpanded(true);
                }
                event->accept();
                return;
            }
        }
        
        // 其他情况传递激活信号
        if (node) {
            emit nodeActivated(node);
        }
    }
    QTreeWidget::mouseDoubleClickEvent(event);
}

void LayerTreeWidget::mousePressEvent(QMouseEvent* event)
{
    // 记录按下位置，用于判断是否应该启动拖拽
    if (event->button() == Qt::LeftButton) {
        m_dragStartPosition = event->pos();
    }
    
    // 处理左键点击
    if (event->button() == Qt::LeftButton) {
        auto* item = itemAt(event->pos());
        if (item) {
            // 尝试检测是否点击在展开/折叠图标区域
            QModelIndex index = indexAt(event->pos());
            if (index.isValid()) {
                // 获取父视图左侧缩进区域（包含展开图标的区域）
                int indent = indentation();
                int depth = 0;
                QTreeWidgetItem* parent = item->parent();
                while (parent) {
                    depth++;
                    parent = parent->parent();
                }
                
                // 简单计算左侧区域
                QRect itemRect = visualRect(index);
                int expandIconAreaWidth = indent * (depth + 1);
                QRect expandArea(itemRect.left() - expandIconAreaWidth, itemRect.top(), 
                               expandIconAreaWidth, itemRect.height());
                
                // 检查是否点击了展开区域并且有子项
                if (expandArea.contains(event->pos()) && item->childCount() > 0) {
                    // 直接让Qt处理展开/折叠事件
                    QTreeWidget::mousePressEvent(event);
                    return;
                }
            }
            
            if (auto* node = item->data(0, Qt::UserRole).value<mdl::Node*>()) {
                // 计算点击区域
                QRect itemRect = visualItemRect(item);
                int lockColumnX = header()->sectionPosition(2); // 锁定图标列起始位置
                int visibilityColumnX = header()->sectionPosition(3); // 可见性图标列起始位置
                int lockColumnWidth = header()->sectionSize(2); // 锁定图标列宽度
                int visibilityColumnWidth = header()->sectionSize(3); // 可见性图标列宽度
                
                QRect lockRect(lockColumnX, itemRect.top(), lockColumnWidth, itemRect.height());
                QRect visibilityRect(visibilityColumnX, itemRect.top(), visibilityColumnWidth, itemRect.height());
                
                // 处理锁定图标点击
                if (lockRect.contains(event->pos())) {
                    // 点击了锁定图标 - 只有图层节点才有锁定功能
                    if (auto* layerNode = dynamic_cast<mdl::LayerNode*>(node)) {
                        auto document = kdl::mem_lock(m_document);
                        // document->logger().info() << "Clicked lock icon for layer: " << layerNode->name();
                        
                        // 发出信号，让LayerEditor处理
                        emit nodeLockToggled(layerNode);
                        
                        // 延迟更新图标，确保状态已变更
                        QTimer::singleShot(100, this, [this, layerNode, item]() {
                            item->setIcon(2, layerNode->locked() ? m_lockedIcon : m_unlockedIcon);
                        });
                    }
                    event->accept();
                    return;
                } 
                // 处理可见性图标点击
                else if (visibilityRect.contains(event->pos())) {
                    // 点击了可见性图标
                    auto document = kdl::mem_lock(m_document);
                    // document->logger().info() << "Clicked visibility icon for node: " << node->name();
                    
                    if (auto* layerNode = dynamic_cast<mdl::LayerNode*>(node)) {
                        // 对于图层节点，只发出信号，让LayerEditor处理
                        emit nodeVisibilityToggled(layerNode);
                        
                        // 延迟更新图标，确保状态已变更
                        QTimer::singleShot(100, this, [this, layerNode, item]() {
                            bool isVisible = layerNode->visible();
                            item->setIcon(3, isVisible ? m_visibleIcon : m_hiddenIcon);
                            
                            // 递归更新所有子项的图标
                            updateVisibilityIconRecursively(item, isVisible);
                        });
                    } else {
                        // 非图层节点仍由本地逻辑处理
                        std::vector<mdl::Node*> nodes{node};
                        bool willBeVisible = !node->visible();
                        
                        if (!willBeVisible) {
                            document->hide(nodes);
                        } else {
                            document->show(nodes);
                        }
                        
                        // 立即更新图标
                        item->setIcon(3, node->visible() ? m_visibleIcon : m_hiddenIcon);
                        
                        // 如果是组节点，递归更新其子项图标
                        if (dynamic_cast<mdl::GroupNode*>(node) || dynamic_cast<mdl::EntityNode*>(node)) {
                            updateVisibilityIconRecursively(item, node->visible());
                        }
                    }
                    event->accept();
                    return;
                } 
                // 处理常规区域点击 - 设置选择并防止多选
                else {
                    // 检查修饰键状态
                    bool isCtrlPressed = event->modifiers() & Qt::ControlModifier;
                    bool isShiftPressed = event->modifiers() & Qt::ShiftModifier;
                    
                    // 处理Shift键批量选择
                    if (isShiftPressed) {
                        // 找到当前的焦点项
                        QTreeWidgetItem* focusItem = this->currentItem();
                        if (focusItem && focusItem != item) {
                            // 清除当前所有选择
                            if (!isCtrlPressed) {
                                clearSelection();
                            }
                            
                            // 实现范围选择
                            bool selectionStarted = false;
                            bool shouldReverse = false;
                            QList<QTreeWidgetItem*> itemsToSelect;
                            
                            // 确定是否需要反向选择（从下往上）
                            if (visualItemRect(focusItem).top() > visualItemRect(item).top()) {
                                shouldReverse = true;
                            }
                            
                            // 在同一父节点内查找范围项
                            if (item->parent() == focusItem->parent()) {
                                // 同一父节点下的连续项
                                QTreeWidgetItem* parent = item->parent();
                                QTreeWidgetItem* startItem = shouldReverse ? item : focusItem;
                                QTreeWidgetItem* endItem = shouldReverse ? focusItem : item;
                                
                                // 如果在根级别
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
                                // 子节点
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
                                
                                // 选择所有找到的项
                                for (QTreeWidgetItem* selectItem : itemsToSelect) {
                                    selectItem->setSelected(true);
                                }
                                
                                event->accept();
                                return;
                            }
                            // 不同父节点情况下，简单选择当前点击的项
                            else {
                                // 保留现有选择
                                setCurrentItem(item);
                                item->setSelected(true);
                            }
                        } else {
                            // 没有已选项或选了同一项，按正常方式处理
                            if (!isCtrlPressed) {
                                clearSelection();
                            }
                            setCurrentItem(item);
                            item->setSelected(true);
                        }
                    }
                    // 处理普通点击和Ctrl点击
                    else {
                        // 如果没有按下Ctrl键，则清除所有选择
                        if (!isCtrlPressed) {
                            clearSelection();
                        }
                        
                        // 处理当前项的选中状态
                        if (isCtrlPressed && item->isSelected()) {
                            // 如果按下Ctrl并且已经选中，则取消选择
                            item->setSelected(false);
                        } else {
                            // 否则选中当前项
                            setCurrentItem(item, 0, QItemSelectionModel::Current);
                            item->setSelected(true);
                        }
                    }
                    
                    // 只有在点击图层节点且不是多选时才同步选择到文档
                    if (dynamic_cast<mdl::LayerNode*>(node) && !isCtrlPressed && !isShiftPressed) {
                        auto document = kdl::mem_lock(m_document);
                        if (document) {
                            // 确保选择状态同步前已清除文档选择
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
    
    // 其他情况调用父类处理
    QTreeWidget::mousePressEvent(event);
}

void LayerTreeWidget::drawRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QTreeWidget::drawRow(painter, option, index);
}

// 重写键盘按键事件，处理ESC键
void LayerTreeWidget::keyPressEvent(QKeyEvent* event)
{
    // 检查是否按下ESC键
    if (event->key() == Qt::Key_Escape) {
        // 清除当前图层树控件的选择
        clearSelection();
        
        // 同步到文档，以清除在场景中的所有选择
        auto document = kdl::mem_lock(m_document);
        if (document) {
            document->deselectAll();
        }
        
        // 让父部件处理焦点等其他操作
        parentWidget()->setFocus();
        event->accept();
        return;
    }
    
    // 其他键按下，调用父类处理
    QTreeWidget::keyPressEvent(event);
  }

// 重写拖拽开始事件，控制哪些项可以被拖拽
void LayerTreeWidget::dragEnterEvent(QDragEnterEvent* event)
{
    // 只接受来自本控件且带有正确MIME类型的拖拽
    if (event->source() == this && 
        event->mimeData()->hasFormat("application/x-trenchronbroom-node")) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

// 重写拖拽移动事件，根据目标位置判断是否允许放置
void LayerTreeWidget::dragMoveEvent(QDragMoveEvent* event)
{
    // 验证MIME数据
    if (!event->mimeData()->hasFormat("application/x-trenchronbroom-node")) {
        event->ignore();
        return;
    }
    
    // 获取源节点
    QByteArray itemData = event->mimeData()->data("application/x-trenchronbroom-node");
    QDataStream dataStream(&itemData, QIODevice::ReadOnly);
    quintptr nodePtr;
    dataStream >> nodePtr;
    mdl::Node* sourceNode = reinterpret_cast<mdl::Node*>(nodePtr);
    
    // 获取目标项
    QTreeWidgetItem* targetItem = itemAt(event->position().toPoint());
    if (!targetItem) {
        event->ignore();
        return;
    }

    // 获取目标节点
    auto* targetNode = targetItem->data(0, Qt::UserRole).value<mdl::Node*>();
    if (!targetNode) {
        event->ignore();
        return;
    }
    
    // 不允许拖放到自身
    if (sourceNode == targetNode) {
        event->ignore();
        return;
    }
    
    // 不能拖拽默认图层
    if (dynamic_cast<mdl::LayerNode*>(sourceNode) && 
        sourceNode->name() == "Default Layer") {
        event->ignore();
        return;
    }

    // 判断目标节点是否可以接收此类源节点
    bool canAccept = false;
    
    // 判断节点类型
    auto* sourceBrush = dynamic_cast<mdl::BrushNode*>(sourceNode);
    
    auto* targetLayer = dynamic_cast<mdl::LayerNode*>(targetNode);
    auto* targetGroup = dynamic_cast<mdl::GroupNode*>(targetNode);
    auto* targetEntity = dynamic_cast<mdl::EntityNode*>(targetNode);
    auto* targetBrush = dynamic_cast<mdl::BrushNode*>(targetNode);
    
    // 规则1-5：详细拖拽逻辑
    if (targetLayer) {
        // 图层可以接收所有类型的节点
        canAccept = true;
    }
    else if (targetGroup) {
        // 组可以接收除了图层以外的所有节点
        if (!dynamic_cast<mdl::LayerNode*>(sourceNode)) {
            canAccept = true;
        }
    }
    else if (targetEntity) {
        // 检查实体类型
        if (auto* definition = targetEntity->entity().definition()) {
            if (definition->type() == mdl::EntityDefinitionType::BrushEntity) {
                // 刷子实体只能接收刷子节点
                if (sourceBrush) {
                    canAccept = true;
                }
            } else {
                // 点实体不接收任何节点
                canAccept = false;
            }
        } else {
            // 没有定义的实体，根据是否有子节点判断
            if (targetEntity->childCount() > 0) {
                // 假设有子节点是刷子实体，只接收刷子
                if (sourceBrush) {
                    canAccept = true;
                }
            } else {
                // 假设无子节点是点实体，不接收任何节点
                canAccept = false;
            }
        }
    }
    else if (targetBrush) {
        // 刷子节点不能直接接收拖拽
        // 注意：规则4(生成默认组)需要在dropEvent中处理
        canAccept = false;
    }
    
    if (canAccept) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

// 重写放置事件，实现实际的节点移动
void LayerTreeWidget::dropEvent(QDropEvent* event)
{
    // 验证MIME数据
    if (!event->mimeData()->hasFormat("application/x-trenchronbroom-node")) {
        event->ignore();
        return;
    }
    
    // 解析源节点
    QByteArray itemData = event->mimeData()->data("application/x-trenchronbroom-node");
    QDataStream dataStream(&itemData, QIODevice::ReadOnly);
    quintptr nodePtr;
    dataStream >> nodePtr;
    mdl::Node* sourceNode = reinterpret_cast<mdl::Node*>(nodePtr);
    
    // 获取目标项
    QTreeWidgetItem* targetItem = itemAt(event->position().toPoint());
    if (!targetItem) {
        event->ignore();
        return;
    }
    
    // 获取目标节点
    auto* targetNode = targetItem->data(0, Qt::UserRole).value<mdl::Node*>();
    if (!targetNode) {
        event->ignore();
        return;
    }
    
    // 不能拖拽到自身
    if (sourceNode == targetNode) {
        event->ignore();
        return;
    }
    
    // 检查节点状态
    if (sourceNode->locked()) {
        // 锁定的节点不能移动
        event->ignore();
        return;
    }
    
    if (!targetNode->visible() || targetNode->locked()) {
        // 不能移动到不可见或锁定的目标
        event->ignore();
        return;
    }
    
    auto document = kdl::mem_lock(m_document);
    if (!document) {
        event->ignore();
        return;
    }
    
    try {
        // 特殊处理：如果是将Brush拖到实体上
        auto* sourceBrush = dynamic_cast<mdl::BrushNode*>(sourceNode);
        auto* targetEntity = dynamic_cast<mdl::EntityNode*>(targetNode);
        
        if (sourceBrush && targetEntity) {
            // 将brush节点添加到目标实体中
            const auto nodesToMove = std::vector<mdl::Node*>{sourceNode};
            
            // 清除选择
            document->deselectAll();
            
            // 创建重父级映射并执行
            std::map<mdl::Node*, std::vector<mdl::Node*>> reparentMap;
            reparentMap[targetEntity] = nodesToMove;
            
            if (!document->reparentNodes(reparentMap)) {
                event->ignore();
                return;
            }
            
            // 选择被移动的节点
            document->selectNodes(nodesToMove);
            
            // 确保目标项展开
            targetItem->setExpanded(true);
            
            // 接受拖放事件
            event->acceptProposedAction();
            return;
        }
        
        // 处理其他类型的节点重父级
        try {
            // 使用文档的原生reparentNodes方法
            // MapDocument可能已经在内部创建了Transaction
            std::map<mdl::Node*, std::vector<mdl::Node*>> nodesToReparent;
            nodesToReparent[targetNode].push_back(sourceNode);
            
            // 先保存当前选择状态
            auto selectedNode = sourceNode;
            document->deselectAll();
            
            // 执行重新父级操作
            if (document->reparentNodes(nodesToReparent)) {
                // 重新选择节点
                document->selectNodes({selectedNode});
                
                // 确保目标项展开
                targetItem->setExpanded(true);
                
                // 接受拖放事件
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

// 为LayerTreeWidget添加新方法用于同步选择
void LayerTreeWidget::syncSelectionFromDocument()
{
    auto document = kdl::mem_lock(m_document);
    if (!document) return;
    
    try {
        // 获取当前文档中选择的对象
        const auto& selectedNodes = document->selectedNodes().nodes();
        if (selectedNodes.empty()) {
            clearSelection();
            return;
        }
        
        // 检查是否所有选中节点都是同一实体的子节点
        const mdl::EntityNode* parentEntity = nullptr;
        bool allAreChildrenOfSameEntity = true;
        bool containsAllChildren = true;
        
        // 首先检查是否所有节点都有相同的父节点，且父节点是EntityNode
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
        
        // 如果所有节点都有相同的实体父节点，检查是否包含了所有子节点
        if (allAreChildrenOfSameEntity && parentEntity) {
            // 检查实体的所有子节点是否都在选择列表中
            if (parentEntity->childCount() == selectedNodes.size()) {
                std::set<const mdl::Node*> selectedNodeSet(selectedNodes.begin(), selectedNodes.end());
                for (const auto* child : parentEntity->children()) {
                    if (selectedNodeSet.find(child) == selectedNodeSet.end()) {
                        containsAllChildren = false;
                        break;
                    }
                }
                
                // 如果选中了实体的所有子节点，则直接选择实体本身
                if (containsAllChildren) {
                    clearSelection();
                    
                    // 查找并选择父实体节点
                    for (int i = 0; i < topLevelItemCount(); ++i) {
                        QTreeWidgetItem* foundItem = findNodeItemRecursive(parentEntity, topLevelItem(i));
                        if (foundItem) {
                            // 选择实体节点并折叠
                            foundItem->setSelected(true);
                            foundItem->setExpanded(false);
                            scrollToItem(foundItem);
                            
                            // 同步到文档
                            document->deselectAll();
                            document->selectNodes({const_cast<mdl::EntityNode*>(parentEntity)});
                            return;
                        }
                    }
                }
            }
        }
        
        // 常规处理 - 寻找并选择树中对应的项
        clearSelection();
        QList<QTreeWidgetItem*> itemsToSelect;
        
        for (const auto* node : selectedNodes) {
            // 递归查找节点及其所有父节点
            std::set<const mdl::Node*> nodesToFind;
            nodesToFind.insert(node);
            
            // 添加所有父节点直到图层节点
            const mdl::Node* current = node->parent();
            while (current) {
                if (!dynamic_cast<const mdl::LayerNode*>(current)) {
                    nodesToFind.insert(current);
                    current = current->parent();
                } else {
                    break;
                }
            }
            
            // 查找树中对应的项
            for (const mdl::Node* targetNode : nodesToFind) {
                QTreeWidgetItem* foundItem = nullptr;
                for (int i = 0; i < topLevelItemCount(); ++i) {
                    foundItem = findNodeItemRecursive(targetNode, topLevelItem(i));
                    if (foundItem) {
                        if (!itemsToSelect.contains(foundItem)) {
                            itemsToSelect.append(foundItem);
                        }
                        break;
                    }
                }
            }
        }
        
        // 选择找到的所有项
        for (auto* item : itemsToSelect) {
            item->setSelected(true);
            // 确保项可见
            QTreeWidgetItem* parent = item->parent();
            while (parent) {
                parent->setExpanded(true);
                parent = parent->parent();
            }
            scrollToItem(item);
        }
    } catch (const std::exception& e) {
        auto docLog = kdl::mem_lock(m_document);
        if (docLog) {
            docLog->logger().error() << "Error during selection sync: " << e.what();
        }
    }
}

// 添加递归查找节点的辅助方法
QTreeWidgetItem* LayerTreeWidget::findNodeItemRecursive(const mdl::Node* targetNode, QTreeWidgetItem* startItem)
{
    if (!startItem) return nullptr;
    
    // 检查当前项
    auto* node = startItem->data(0, Qt::UserRole).value<mdl::Node*>();
    if (node == targetNode) {
        return startItem;
    }
    
    // 递归检查子项
    for (int i = 0; i < startItem->childCount(); ++i) {
        QTreeWidgetItem* result = findNodeItemRecursive(targetNode, startItem->child(i));
        if (result) {
            return result;
        }
    }
    
    return nullptr;
}

// 在树中查找并选择指定节点
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

// 从树项选择同步到文档
void LayerTreeWidget::syncSelectionToDocument()
{
    auto document = kdl::mem_lock(m_document);
    if (!document) return;
    
    try {
        const auto selectedItems = this->selectedItems();
        if (selectedItems.empty()) {
            return; // 不在列表没有选择时清除主视图选择，以避免意外的选择丢失
        }
        
        if (selectedItems.size() == 1) {
            auto* item = selectedItems.first();
            auto* node = item->data(0, Qt::UserRole).value<mdl::Node*>();
            if (node && dynamic_cast<mdl::LayerNode*>(node)) {
                // 如果是图层节点，不同步选择到文档
                return;
            } else if (node) {
                // 只选择当前节点
                document->deselectAll();
                document->selectNodes({node});
            }
        } else {
            // 处理多选情况
            bool hasLayerNode = false;
            std::vector<mdl::Node*> nodesToSelect;
            
            // 检查选中项中是否包含图层节点
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
            
            // 如果有图层节点被选中，不执行同步
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

// 处理列表选择变化
void LayerTreeWidget::onItemSelectionChanged()
{
    if (!m_syncingSelection) {
        m_syncingSelection = true;
        
        // 获取当前选中的项
        QList<QTreeWidgetItem*> selectedItems = this->selectedItems();
        if (selectedItems.size() == 1) {
            // 如果只选中了一个项
            QTreeWidgetItem* item = selectedItems.first();
            auto* node = item->data(0, Qt::UserRole).value<mdl::Node*>();
            
            // 检查是否为实体节点
            if (node && dynamic_cast<mdl::EntityNode*>(node)) {
                if (node->childCount() > 0) {
                    // 折叠其他已展开的实体
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

// 折叠除当前选中项外的其他实体
void LayerTreeWidget::collapseOtherEntities(QTreeWidgetItem* currentItem, QTreeWidgetItem* selectedItem)
{
    if (!currentItem || currentItem == selectedItem) return;
    
    // 检查当前项是否为实体节点
    auto* node = currentItem->data(0, Qt::UserRole).value<mdl::Node*>();
    if (dynamic_cast<mdl::EntityNode*>(node) && currentItem->childCount() > 0 && currentItem->isExpanded()) {
        // 如果是非选中的实体节点且已展开，则折叠
        currentItem->setExpanded(false);
    }
    
    // 递归处理子项
    for (int i = 0; i < currentItem->childCount(); ++i) {
        collapseOtherEntities(currentItem->child(i), selectedItem);
    }
}

// 处理文档选择变化
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

LayerListBox::LayerListBox(std::weak_ptr<MapDocument> document, QWidget* parent)
    : QWidget(parent)
    , m_document(std::move(document))
    , m_currentSortMode(0)
{
    createGui();
    connectObservers();
    updateTree();  // 初始化时更新树
    
    // 应用初始排序（按名称排序）
    sortTree(0);
}

void LayerListBox::createGui()
{
    // 创建搜索和排序控件
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

    // 创建树形控件
    m_treeWidget = new LayerTreeWidget(m_document, this);
    m_treeWidget->setMinimumHeight(400);  // 增加最小高度
    m_treeWidget->setIndentation(20);  // 增加缩进
    m_treeWidget->setIconSize(QSize(16, 16));
    
    // 设置列宽
    m_treeWidget->setColumnWidth(0, 240); // 名称列加宽
    m_treeWidget->setColumnWidth(1, 100); // 数量列加宽
    m_treeWidget->setColumnWidth(2, 40);  // 可见性按钮列
    m_treeWidget->setColumnWidth(3, 40);  // 锁定按钮列

    // 创建顶部工具栏布局
    auto* toolbarLayout = new QHBoxLayout();
    toolbarLayout->setContentsMargins(
        LayoutConstants::MediumHMargin,
        LayoutConstants::MediumVMargin, 
        LayoutConstants::MediumHMargin,
        LayoutConstants::MediumVMargin);
    toolbarLayout->setSpacing(LayoutConstants::WideHMargin);  // 增加间距
    toolbarLayout->addWidget(m_searchBox, 1);
    toolbarLayout->addWidget(m_sortOptions);

    // 创建主布局
    auto* mainLayout = new QVBoxLayout();
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(LayoutConstants::MediumVMargin);
    mainLayout->addLayout(toolbarLayout);
    mainLayout->addWidget(m_treeWidget, 1);

    setLayout(mainLayout);
    setMinimumWidth(450);  // 增加整体最小宽度

    // 连接信号
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
    
    // 连接节点操作信号
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
    
    // 每次文档变更后，应用当前排序
    if (m_currentSortMode == 0) {
        // 对于名称排序，直接调用sortItems
        m_treeWidget->sortItems(0, Qt::AscendingOrder);
    } else if (m_currentSortMode == 1) {
        // 对于类型排序，调用自定义排序
        sortByType();
    }
    // Mode 2是自定义排序，不需处理
}

void LayerListBox::nodesDidChange(const std::vector<mdl::Node*>& nodes)
{
    // 检查是否需要完全更新树
    bool needFullUpdate = false;
    
    // 如果节点列表为空，或者有节点的父级发生变化，就需要完全更新
    if (nodes.empty()) {
        needFullUpdate = true;
    } else {
        for (mdl::Node* node : nodes) {
            // 检查是否为图层节点或组节点或其父级发生变化
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
        updateTree(); // 只在必要时重建整个树
    } else {
        // 只更新受影响的节点
        for (mdl::Node* node : nodes) {
            m_treeWidget->updateNodeItem(node);
        }
    }
}

void LayerListBox::filterTree(const QString& text)
  {
    // 清空之前的过滤状态
    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_treeWidget->topLevelItem(i);
        showAllItems(item);
    }
    
    // 如果搜索文本为空，直接返回
    if (text.isEmpty()) {
    return;
  }
    
    // 否则，根据搜索文本过滤
    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_treeWidget->topLevelItem(i);
        bool matches = itemMatchesFilter(item, text);
        item->setHidden(!matches);
}
}

// 显示所有项目
void LayerListBox::showAllItems(QTreeWidgetItem* item)
{
    if (!item) return;
    
    item->setHidden(false);
    
    // 递归处理所有子项
    for (int i = 0; i < item->childCount(); ++i) {
        showAllItems(item->child(i));
  }
}

// 检查项目是否匹配过滤条件
bool LayerListBox::itemMatchesFilter(QTreeWidgetItem* item, const QString& text)
{
    if (!item) return false;
    
    // 检查当前项是否匹配
    bool match = item->text(0).contains(text, Qt::CaseInsensitive);
    
    // 递归检查所有子项
    for (int i = 0; i < item->childCount(); ++i) {
        QTreeWidgetItem* childItem = item->child(i);
        bool childMatch = itemMatchesFilter(childItem, text);
        match = match || childMatch;
        
        // 如果子项匹配，则始终显示
        childItem->setHidden(!childMatch);
    }
    
    // 如果当前项或任何子项匹配，则显示，否则隐藏
    item->setHidden(!match);
    
    return match;
}

void LayerListBox::sortTree(int index)
{
    // 保存当前选中的图层
    auto* currentLayer = selectedLayer();
    
    // 保存当前排序模式
    m_currentSortMode = index;
    
    // 根据选择的排序方式进行排序
    switch (index) {
        case 0: // 按名称排序
            m_treeWidget->sortItems(0, Qt::AscendingOrder);
            break;
        case 1: // 按类型排序
            // 复杂排序，需要重新排列项
            sortByType();
            break;
        case 2: // 自定义排序（重新加载以恢复原始顺序）
            updateTree();
            break;
    }
    
    // 恢复选中的图层
    if (currentLayer) {
        setSelectedLayer(currentLayer);
    }
}

void LayerListBox::sortByType()
{
    // 获取所有顶级项
    QList<QTreeWidgetItem*> items;
    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        items.append(m_treeWidget->takeTopLevelItem(0));
    }
    
    // 按类型排序：默认图层优先，然后是自定义图层
    std::sort(items.begin(), items.end(), [](const QTreeWidgetItem* a, const QTreeWidgetItem* b) {
        auto* nodeA = a->data(0, Qt::UserRole).value<mdl::Node*>();
        auto* nodeB = b->data(0, Qt::UserRole).value<mdl::Node*>();
        
        auto* layerA = dynamic_cast<mdl::LayerNode*>(nodeA);
        auto* layerB = dynamic_cast<mdl::LayerNode*>(nodeB);
        
        if (layerA && layerB) {
            // 默认图层始终在最前面
            bool isDefaultA = layerA->name() == "Default Layer";
            bool isDefaultB = layerB->name() == "Default Layer";
            
            if (isDefaultA != isDefaultB) {
                return isDefaultA;
            }
            
            // 非默认图层按名称排序
            return layerA->name() < layerB->name();
        }
        
        return false;
    });
    
    // 将排序后的项添加回树形控件
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
    
    // 遍历所有顶级项查找对应的图层
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
    // 获取当前选中项的索引
    int currentIndex = -1;
    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        if (m_treeWidget->topLevelItem(i) == m_treeWidget->currentItem()) {
            currentIndex = i;
            break;
        }
    }
    
    // 如果有选中项，则在删除后选择相邻项
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
    // 调用LayerTreeWidget的updateTree方法
    m_treeWidget->updateTree();
    
    // 更新树后重新应用当前排序方式
    // 阻止重复调用updateTree()，避免无限递归
    int tempSortMode = m_currentSortMode;
    m_currentSortMode = -1; // 临时设置为-1，防止无限递归
    
    switch (tempSortMode) {
        case 0: // 按名称排序
            m_treeWidget->sortItems(0, Qt::AscendingOrder);
            break;
        case 1: // 按类型排序
            sortByType();
            break;
        // 不处理自定义排序(2)，因为自定义排序本身就是调用updateTree()
    }
    
    // 恢复当前排序模式
    m_currentSortMode = tempSortMode;
}

// 在LayerTreeWidget类定义中添加新方法
void LayerTreeWidget::updateNodeItem(mdl::Node* node)
{
    if (!node) return;
    
    // 查找节点对应的项
    QTreeWidgetItem* item = findNodeItem(node, invisibleRootItem());
    if (item) {
        // 更新项的基本信息
        item->setText(0, QString::fromStdString(node->name()));
        
        // 更新图标状态
        if (auto* layerNode = dynamic_cast<mdl::LayerNode*>(node)) {
            // 图层节点更新锁定和可见性图标
            item->setIcon(2, node->locked() ? m_lockedIcon : m_unlockedIcon);
            item->setIcon(3, node->visible() ? m_visibleIcon : m_hiddenIcon);
            
            // 更新计数
            auto count = static_cast<qint64>(layerNode->childCount());
            QString countText;
            if(count > 999) {
                countText = QString("%1K").arg(count/1000.0, 0, 'f', 1);
            } else {
                countText = QString::number(count);
            }
            countText += tr(" objects");
            item->setText(1, countText);
        }
        else if (auto* group = dynamic_cast<mdl::GroupNode*>(node)) {
            // 非图层节点只更新可见性图标
            item->setIcon(3, node->visible() ? m_visibleIcon : m_hiddenIcon);
            
            // 更新计数
            auto count = static_cast<qint64>(group->childCount());
            QString countText;
            if(count > 999) {
                countText = QString("%1K").arg(count/1000.0, 0, 'f', 1);
            } else {
                countText = QString::number(count);
            }
            countText += tr(" objects");
            item->setText(1, countText);
        }
        else {
            // 其他节点只更新可见性图标
            item->setIcon(3, node->visible() ? m_visibleIcon : m_hiddenIcon);
        }
    }
}

// 查找节点对应的项
QTreeWidgetItem* LayerTreeWidget::findNodeItem(mdl::Node* targetNode, QTreeWidgetItem* startItem)
{
    if (!startItem) return nullptr;
    
    // 检查当前项
    auto* node = startItem->data(0, Qt::UserRole).value<mdl::Node*>();
    if (node == targetNode) {
        return startItem;
    }
    
    // 递归检查子项
    for (int i = 0; i < startItem->childCount(); ++i) {
        QTreeWidgetItem* result = findNodeItem(targetNode, startItem->child(i));
        if (result) {
            return result;
        }
    }
    
    return nullptr;
}

// 递归更新子节点的可见性图标
void LayerTreeWidget::updateVisibilityIconRecursively(QTreeWidgetItem* item, bool isVisible)
{
    if (!item) return;
    
    // 更新当前项的可见性图标
    item->setIcon(3, isVisible ? m_visibleIcon : m_hiddenIcon);
    
    // 递归更新所有子项
    for (int i = 0; i < item->childCount(); ++i) {
        updateVisibilityIconRecursively(item->child(i), isVisible);
    }
}

// 重写鼠标移动事件，阻止拖拽多选
void LayerTreeWidget::mouseMoveEvent(QMouseEvent* event)
{
    // 如果没有按下左键则不考虑拖拽
    if (!(event->buttons() & Qt::LeftButton)) {
        QTreeWidget::mouseMoveEvent(event);
        return;
    }
    
    // 判断移动距离是否足够启动拖拽
    if ((event->pos() - m_dragStartPosition).manhattanLength() < QApplication::startDragDistance()) {
        // 阻止默认的多选行为
        event->accept();
        return;
    }
    
    // 获取当前选中项(只处理单选)
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
    
    // 获取节点信息
    auto* node = currentItem->data(0, Qt::UserRole).value<mdl::Node*>();
    if (!node || node->locked()) {
        event->accept();
        return;
    }
    
    // 创建拖拽对象
    QDrag* drag = new QDrag(this);
    QMimeData* mimeData = new QMimeData;
    
    // 存储节点指针用于识别
    QByteArray itemData;
    QDataStream dataStream(&itemData, QIODevice::WriteOnly);
    quintptr nodePtr = reinterpret_cast<quintptr>(node);
    dataStream << nodePtr;
    mimeData->setData("application/x-trenchronbroom-node", itemData);
    
    drag->setMimeData(mimeData);
    
    // 设置拖拽图标
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    QIcon icon = currentItem->icon(0);
    icon.paint(&painter, QRect(0, 0, 32, 32));
    painter.end();
    
    drag->setPixmap(pixmap);
    drag->setHotSpot(QPoint(16, 16));
    
    // 执行拖拽(阻止事件传递给父控件)
    drag->exec(Qt::MoveAction);
    event->accept();
}

} // namespace tb::ui

