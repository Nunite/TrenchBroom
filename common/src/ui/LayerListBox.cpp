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

#include "mdl/LayerNode.h"
#include "mdl/WorldNode.h"
#include "mdl/EntityNode.h"
#include "mdl/GroupNode.h"
#include "mdl/BrushNode.h"
#include "ui/MapDocument.h"
#include "ui/QtUtils.h"
#include "ui/ViewConstants.h"
#include "io/ResourceUtils.h"

#include "kdl/memory_utils.h"

namespace tb::ui
{

LayerTreeWidget::LayerTreeWidget(std::weak_ptr<MapDocument> document, QWidget* parent)
    : QTreeWidget(parent)
    , m_document(std::move(document))
    , m_syncingSelection(false)
{
    setHeaderHidden(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection); // 允许多选
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setExpandsOnDoubleClick(false);
    setRootIsDecorated(true);
    setUniformRowHeights(false);
    setItemsExpandable(true);
    setAllColumnsShowFocus(true);
    setColumnCount(3); // 名称、对象数量、控制按钮
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);  // 名称列自动拉伸
    header()->setSectionResizeMode(1, QHeaderView::Fixed);    // 数量列固定宽度
    header()->setSectionResizeMode(2, QHeaderView::Fixed);    // 控制按钮列固定宽度
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
    m_visibleIcon = io::loadSVGIcon("Visible.svg");
    m_hiddenIcon = io::loadSVGIcon("Hidden.svg");
    m_lockedIcon = io::loadSVGIcon("Locked.svg");
    m_unlockedIcon = io::loadSVGIcon("Unlocked.svg");
}

void LayerTreeWidget::setupTreeItem(QTreeWidgetItem* item, mdl::Node* node)
{
    if (!item || !node) return;

    // 设置基本属性
    item->setText(0, QString::fromStdString(node->name()));
    item->setData(0, Qt::UserRole, QVariant::fromValue(node));

    // 设置字体
    QFont font = item->font(1);
    font.setPointSize(10);
    item->setFont(1, font);

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
    } else if (auto* entity = dynamic_cast<mdl::EntityNode*>(node)) {
        if (entity->parent() && dynamic_cast<mdl::LayerNode*>(entity->parent()) && 
            entity->parent()->name() == "Default Layer") {
            item->setIcon(0, m_worldIcon);
        } else {
            item->setIcon(0, m_entityIcon);
        }
        item->setText(0, QString::fromStdString(entity->name()));
    } else if (auto* brush = dynamic_cast<mdl::BrushNode*>(node)) {
        item->setIcon(0, m_brushIcon);
    }

    // 设置可见性和锁定状态
    item->setIcon(2, node->visible() ? m_visibleIcon : m_hiddenIcon);
    item->setIcon(3, node->locked() ? m_lockedIcon : m_unlockedIcon);
}

void LayerTreeWidget::addEntityToTree(QTreeWidgetItem* parentItem, mdl::Node* node)
{
    auto* item = new QTreeWidgetItem(parentItem);
    setupTreeItem(item, node);
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
    }
}

void LayerTreeWidget::updateTree()
{
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
        }
        
        // 展开顶级项
        for (int i = 0; i < this->topLevelItemCount(); ++i) {
            this->topLevelItem(i)->setExpanded(true);
        }
    }
}

void LayerTreeWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    auto* item = itemAt(event->pos());
    if (item) {
        if (auto* node = item->data(0, Qt::UserRole).value<mdl::Node*>()) {
            emit nodeActivated(node);
        }
    }
    QTreeWidget::mouseDoubleClickEvent(event);
}

void LayerTreeWidget::mousePressEvent(QMouseEvent* event)
{
    auto* item = itemAt(event->pos());
    if (item) {
        if (auto* node = item->data(0, Qt::UserRole).value<mdl::Node*>()) {
            QRect itemRect = visualItemRect(item);
            
            // 检查是否点击了可见性或锁定图标
            int visibilityIconX = itemRect.right() - 48;
            int lockIconX = itemRect.right() - 24;
            
            if (event->position().x() >= visibilityIconX && event->position().x() < visibilityIconX + 24) {
                emit nodeVisibilityToggled(node);
                return;
            } else if (event->position().x() >= lockIconX && event->position().x() < lockIconX + 24) {
                emit nodeLockToggled(node);
                return;
            } else if (event->button() == Qt::RightButton) {
                emit nodeRightClicked(node, event->globalPosition().toPoint());
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
    if (event->source() == this) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

// 重写拖拽移动事件，根据目标位置判断是否允许放置
void LayerTreeWidget::dragMoveEvent(QDragMoveEvent* event)
{
    QTreeWidgetItem* targetItem = itemAt(event->position().toPoint());
    QTreeWidgetItem* sourceItem = this->currentItem();

    if (!sourceItem || !targetItem) {
        event->ignore();
        return;
    }

    // 获取源节点和目标节点
    auto* sourceNode = sourceItem->data(0, Qt::UserRole).value<mdl::Node*>();
    auto* targetNode = targetItem->data(0, Qt::UserRole).value<mdl::Node*>();

    if (!sourceNode || !targetNode) {
        event->ignore();
        return;
    }

    // 不允许拖拽默认图层
    auto* sourceLayer = dynamic_cast<mdl::LayerNode*>(sourceNode);
    if (sourceLayer && sourceLayer->name() == "Default Layer") {
        event->ignore();
        return;
    }

    // 只允许相同类型的节点之间拖拽
    bool isSourceLayer = dynamic_cast<mdl::LayerNode*>(sourceNode) != nullptr;
    bool isTargetLayer = dynamic_cast<mdl::LayerNode*>(targetNode) != nullptr;
    bool isSourceEntity = dynamic_cast<mdl::EntityNode*>(sourceNode) != nullptr;
    bool isSourceGroup = dynamic_cast<mdl::GroupNode*>(sourceNode) != nullptr;
    bool isTargetGroup = dynamic_cast<mdl::GroupNode*>(targetNode) != nullptr;

    // 允许图层之间拖拽
    if (isSourceLayer && isTargetLayer) {
        event->acceptProposedAction();
        return;
    }

    // 允许实体拖拽到图层或组
    if (isSourceEntity && (isTargetLayer || isTargetGroup)) {
        event->acceptProposedAction();
        return;
    }

    // 允许组拖拽到图层或其他组
    if (isSourceGroup && (isTargetLayer || isTargetGroup)) {
        event->acceptProposedAction();
        return;
    }

    // 其他情况不允许拖拽
    event->ignore();
}

// 重写放置事件，实现实际的节点移动
void LayerTreeWidget::dropEvent(QDropEvent* event)
{
    // 记录源项和目标项
    QTreeWidgetItem* sourceItem = this->currentItem();
    QTreeWidgetItem* targetItem = itemAt(event->position().toPoint());
    
    if (!sourceItem || !targetItem) {
        event->ignore();
        return;
    }
    
    // 获取源节点和目标节点
    auto* sourceNode = sourceItem->data(0, Qt::UserRole).value<mdl::Node*>();
    auto* targetNode = targetItem->data(0, Qt::UserRole).value<mdl::Node*>();
    
    if (!sourceNode || !targetNode) {
        event->ignore();
        return;
    }
    
    // 如果源节点是实体，目标节点是图层或组，则进行实际的节点移动
    bool isSourceEntity = dynamic_cast<mdl::EntityNode*>(sourceNode) != nullptr ||
                         dynamic_cast<mdl::BrushNode*>(sourceNode) != nullptr;
    bool isTargetLayer = dynamic_cast<mdl::LayerNode*>(targetNode) != nullptr;
    bool isTargetGroup = dynamic_cast<mdl::GroupNode*>(targetNode) != nullptr;
    
    auto document = kdl::mem_lock(m_document);
    
    if (isSourceEntity && (isTargetLayer || isTargetGroup)) {
        // 使用 MapDocument 的 API 进行实际的节点移动
        if (document) {
            std::map<mdl::Node*, std::vector<mdl::Node*>> nodesToReparent;
            nodesToReparent[targetNode].push_back(sourceNode);
            document->reparentNodes(nodesToReparent);
            
            // 接受拖放事件
            event->acceptProposedAction();
            return;
    }
  }
    
    // 对于其他类型的拖放（如图层重排序），使用默认处理
    QTreeWidget::dropEvent(event);
    
    // 确保目标项展开
    if (targetItem) {
        targetItem->setExpanded(true);
    }
}

// 为LayerTreeWidget添加新方法用于同步选择
void LayerTreeWidget::syncSelectionFromDocument()
  {
    auto document = kdl::mem_lock(m_document);
    if (!document) return;
    
    // 获取当前文档中选择的对象
    const auto& selectedNodes = document->selectedNodes().nodes();
    if (selectedNodes.empty()) {
        clearSelection();
        return;
    }
    
    // 寻找并选择树中对应的项
    clearSelection();
    for (const auto* node : selectedNodes) {
        findAndSelectNode(node, invisibleRootItem());
    }
}

// 在树中查找并选择指定节点
bool LayerTreeWidget::findAndSelectNode(const mdl::Node* targetNode, QTreeWidgetItem* startItem)
    {
    if (!startItem) return false;
    
    // 检查当前项
    auto* node = startItem->data(0, Qt::UserRole).value<mdl::Node*>();
    if (node == targetNode) {
        startItem->setSelected(true);
        scrollToItem(startItem);
      return true;
    }
    
    // 递归检查子项
    for (int i = 0; i < startItem->childCount(); ++i) {
        if (findAndSelectNode(targetNode, startItem->child(i))) {
            return true;
        }
    }
    
    return false;
}

// 从树项选择同步到文档
void LayerTreeWidget::syncSelectionToDocument()
{
    auto document = kdl::mem_lock(m_document);
    if (!document) return;
    
    const auto selectedItems = this->selectedItems();
    if (selectedItems.empty()) {
        return; // 不在列表没有选择时清除主视图选择，以避免意外的选择丢失
    }
    
    std::vector<mdl::Node*> nodesToSelect;
    for (auto* item : selectedItems) {
        auto* node = item->data(0, Qt::UserRole).value<mdl::Node*>();
        if (node) {
            nodesToSelect.push_back(node);
        }
    }
    
    if (!nodesToSelect.empty()) {
        document->deselectAll();
        document->selectNodes(nodesToSelect);
    }
}

// 处理列表选择变化
void LayerTreeWidget::onItemSelectionChanged()
{
    if (!m_syncingSelection) {
        m_syncingSelection = true;
        syncSelectionToDocument();
        m_syncingSelection = false;
    }
}

// 处理文档选择变化
void LayerTreeWidget::onDocumentSelectionChanged(const Selection& /* selection */)
{
    if (!m_syncingSelection) {
        m_syncingSelection = true;
        syncSelectionFromDocument();
        m_syncingSelection = false;
    }
}

LayerListBox::LayerListBox(std::weak_ptr<MapDocument> document, QWidget* parent)
    : QWidget(parent)
    , m_document(std::move(document))
{
    createGui();
    connectObservers();
    updateTree();  // 初始化时更新树
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
    m_treeWidget->setColumnWidth(2, 80);  // 控制按钮列加宽

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
}

void LayerListBox::nodesDidChange(const std::vector<mdl::Node*>&)
{
    updateTree();
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
}

} // namespace tb::ui
