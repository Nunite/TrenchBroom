#include "OutlinerTreeWidget.h"

#include <QHeaderView>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QMimeData>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QTimer>
#include <QApplication>

#include "mdl/Map.h"
#include "mdl/WorldNode.h"
#include "mdl/LayerNode.h"
#include "mdl/GroupNode.h"
#include "mdl/EntityNode.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityDefinition.h"
#include "mdl/Map_NodeVisibility.h"
#include "mdl/Map_NodeLocking.h"
#include "mdl/Map_Selection.h"
#include "mdl/Map_Nodes.h" // for reparentNodes helpers if needed
#include "mdl/Selection.h"
#include "mdl/Transaction.h"

#include "ui/MapDocument.h"
#include "ui/QtUtils.h"
#include "io/ResourceUtils.h"
#include "kdl/memory_utils.h"

namespace tb::ui
{

OutlinerTreeWidget::OutlinerTreeWidget(MapDocument& document, QWidget* parent)
    : QTreeWidget(parent)
    , m_document(document)
{
    setHeaderHidden(false); // Outliner usually has headers? User screenshot shows "Name"
    
    QStringList headers;
    headers << tr("Name") << tr("Info") << tr("L") << tr("V");
    setHeaderLabels(headers);
    
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setExpandsOnDoubleClick(false);
    setRootIsDecorated(true);
    setUniformRowHeights(true); // Using uniform heights for performance, LayerTreeWidget used false?
    setItemsExpandable(true);
    setAllColumnsShowFocus(true);
    setColumnCount(4);
    
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    header()->setSectionResizeMode(1, QHeaderView::ResizeToContents); // Info
    header()->setSectionResizeMode(2, QHeaderView::Fixed); // Lock
    header()->setSectionResizeMode(3, QHeaderView::Fixed); // Vis
    
    header()->resizeSection(2, 24);
    header()->resizeSection(3, 24);

    // Style similar to LayerTreeWidget
    setStyleSheet(
        "QTreeWidget { "
        "   background-color: #2D2D30;"
        "   color: #E0E0E0;"
        "   border: none;"
        "   font-size: 12px;"
        "}"
        "QTreeWidget::item { "
        "   height: 24px;"
        "   padding: 2px 0px;"
        "}"
        "QTreeWidget::item:selected { "
        "   background-color: #3F3F46;"
        "}"
        "QTreeWidget::item:hover { "
        "   background-color: #2A2A2D;"
        "}"
    );

    loadIcons();

    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);

    connect(this, &QTreeWidget::itemSelectionChanged, this, &OutlinerTreeWidget::onItemSelectionChanged);

    m_notifierConnection += m_document.map().selectionDidChangeNotifier.connect(
        this, &OutlinerTreeWidget::onDocumentSelectionChanged);
    
    // Listen to map changes to update tree
    m_notifierConnection += m_document.map().nodesWereAddedNotifier.connect(
        [this](const auto&) { updateTree(); }); // Naive full update for now
    m_notifierConnection += m_document.map().nodesWereRemovedNotifier.connect(
        [this](const auto&) { updateTree(); });
        
    // Optimize nodeDidChange: only update item text/icon, do NOT rebuild tree
    m_notifierConnection += m_document.map().nodesDidChangeNotifier.connect(
        [this](const std::vector<mdl::Node*>& nodes) {
             for (auto* node : nodes) {
                if (auto* item = findItemForNode(node)) {
                    setupTreeItem(item, node);
                }
            }
        });

    m_notifierConnection += m_document.map().nodeVisibilityDidChangeNotifier.connect(
        [this](const std::vector<mdl::Node*>& nodes) {
            for (auto* node : nodes) {
                if (auto* item = findItemForNode(node)) {
                    refreshTreeItemRecursively(item);
                }
            }
        });

    m_notifierConnection += m_document.map().nodeLockingDidChangeNotifier.connect(
        [this](const std::vector<mdl::Node*>& nodes) {
            for (auto* node : nodes) {
                if (auto* item = findItemForNode(node)) {
                    refreshTreeItemRecursively(item);
                }
            }
        });
    
    // Connect to map lifecycle events to ensure tree is populated when map is loaded/created
    m_notifierConnection += m_document.map().mapWasLoadedNotifier.connect(
        [this](auto&) { qDebug() << "Map loaded"; updateTree(); });
    m_notifierConnection += m_document.map().mapWasCreatedNotifier.connect(
        [this](auto&) { qDebug() << "Map created"; updateTree(); });
    m_notifierConnection += m_document.map().mapWasClearedNotifier.connect(
        [this](auto&) { qDebug() << "Map cleared"; updateTree(); });
    
    qDebug() << "OutlinerTreeWidget constructed";
    updateTree();
}

OutlinerTreeWidget::~OutlinerTreeWidget() = default;

void OutlinerTreeWidget::loadIcons()
{
    m_groupIcon = io::loadSVGIcon("Map_folder.svg");
    m_entityIcon = io::loadSVGIcon("Map_entity.svg");
    m_brushEntityIcon = io::loadSVGIcon("Map_fullcube.svg");
    m_brushIcon = io::loadSVGIcon("Map_cube.svg");
    
    m_visibleIcon = io::loadSVGIcon("object_show.svg");
    m_hiddenIcon = io::loadSVGIcon("object_hidden.svg");
    m_lockedIcon = io::loadSVGIcon("Lock_on.svg");
    m_unlockedIcon = io::loadSVGIcon("Lock_off.svg");
}

mdl::Node* OutlinerTreeWidget::nodeFromItem(QTreeWidgetItem* item) const
{
    if (!item) return nullptr;
    return item->data(0, Qt::UserRole).value<mdl::Node*>();
}

void OutlinerTreeWidget::setupTreeItem(QTreeWidgetItem* item, mdl::Node* node)
{
    if (!item || !node) return;

    item->setText(0, QString::fromStdString(node->name()));
    item->setData(0, Qt::UserRole, QVariant::fromValue(node));

    // Icon and Type logic
    if (dynamic_cast<mdl::GroupNode*>(node)) {
        item->setIcon(0, m_groupIcon);
    } else if (auto* entity = dynamic_cast<mdl::EntityNode*>(node)) {
        if (auto* definition = entity->entity().definition()) {
            if (mdl::getType(*definition) == mdl::EntityDefinitionType::Brush) {
                item->setIcon(0, m_brushEntityIcon);
            } else {
                item->setIcon(0, m_entityIcon);
            }
        } else {
            // Fallback if no definition
            item->setIcon(0, entity->childCount() > 0 ? m_brushEntityIcon : m_entityIcon);
        }
    } else if (dynamic_cast<mdl::BrushNode*>(node)) {
        item->setIcon(0, m_brushIcon);
    }

    // Info Column (1)
    if (auto* container = dynamic_cast<mdl::GroupNode*>(node)) {
        item->setText(1, QString("%1 objects").arg(container->childCount()));
    } else if (auto* entity = dynamic_cast<mdl::EntityNode*>(node)) {
        if (entity->childCount() > 0) {
             item->setText(1, QString("%1 brushes").arg(entity->childCount()));
        }
    }

    // Lock (2) and Visibility (3)
    item->setIcon(2, node->locked() ? m_lockedIcon : m_unlockedIcon);
    item->setIcon(3, node->visible() ? m_visibleIcon : m_hiddenIcon);
    
    // Dim hidden items text
    if (!node->visible()) {
        item->setForeground(0, QColor(128, 128, 128));
    } else {
        item->setForeground(0, QColor(224, 224, 224));
    }
}

void OutlinerTreeWidget::addNodeToTree(QTreeWidgetItem* parentItem, mdl::Node* node)
{
    auto* item = new QTreeWidgetItem(parentItem);
    setupTreeItem(item, node);

    // Recursion
    // If it's a Group or Entity (which can have children), add them
    if (auto* group = dynamic_cast<mdl::GroupNode*>(node)) {
        for (auto* child : group->children()) {
            addNodeToTree(item, child);
        }
    } else if (auto* entity = dynamic_cast<mdl::EntityNode*>(node)) {
        for (auto* child : entity->children()) {
            addNodeToTree(item, child);
        }
    }
    // Brushes don't have children in this view usually
}

void OutlinerTreeWidget::refreshTreeItemRecursively(QTreeWidgetItem* item)
{
    if (!item) return;

    if (auto* node = nodeFromItem(item)) {
        setupTreeItem(item, node);
    }

    for (int i = 0; i < item->childCount(); ++i) {
        refreshTreeItemRecursively(item->child(i));
    }
}

void OutlinerTreeWidget::updateTree()
{
    qDebug() << "OutlinerTreeWidget::updateTree called";

    // Prevent selection changes from propagating during rebuild
    const bool wasSyncing = m_syncingSelection;
    m_syncingSelection = true;
    blockSignals(true);

    // Save selection
    std::vector<mdl::Node*> selectedNodesBefore;
    for (auto* item : selectedItems()) {
        if (auto* node = nodeFromItem(item)) {
            selectedNodesBefore.push_back(node);
        }
    }

    clear();

    auto* world = m_document.map().world();
    if (!world) {
        qDebug() << "World is null";
        m_syncingSelection = wasSyncing;
        blockSignals(false);
        return;
    }

    // We can iterate layers if we want to show layers, OR we can show entities directly if they are top-level in the layer.
    // The user screenshot showed entities at top level.
    // In TrenchBroom, everything is in a layer.
    // If we want to mimic "Outliner", we might want to flatten layers or show them as folders.
    // Let's iterate all layers and their children.
    
    // Default Layer
    auto* defaultLayer = world->defaultLayer();
    if (defaultLayer) {
        qDebug() << "Default layer children count:" << defaultLayer->children().size();
        for (auto* node : defaultLayer->children()) {
            addNodeToTree(invisibleRootItem(), node);
        }
    } else {
        qDebug() << "Default layer is null";
    }

    // Custom Layers
    auto customLayers = world->customLayersUserSorted();
    qDebug() << "Custom layers count:" << customLayers.size();
    for (auto* layer : customLayers) {
        // Option 1: Show Layer as a folder
        // auto* layerItem = new QTreeWidgetItem(invisibleRootItem());
        // setupTreeItem(layerItem, layer); // Need icon for layer
        // for (auto* node : layer->children()) addNodeToTree(layerItem, node);
        
        // Option 2: Flatten layers (show content directly). 
        // User screenshot shows "dont_export" which is likely a layer.
        // So we should show layers if they are not the default layer?
        // Or maybe just show everything.
        
        auto* layerItem = new QTreeWidgetItem(invisibleRootItem());
        layerItem->setText(0, QString::fromStdString(layer->name()));
        layerItem->setData(0, Qt::UserRole, QVariant::fromValue<mdl::Node*>(layer));
        layerItem->setIcon(0, m_groupIcon); // Use group icon for layer for now
        layerItem->setText(1, QString("%1 objects").arg(layer->childCount()));
        layerItem->setIcon(2, layer->locked() ? m_lockedIcon : m_unlockedIcon);
        layerItem->setIcon(3, layer->visible() ? m_visibleIcon : m_hiddenIcon);
        
        for (auto* node : layer->children()) {
            addNodeToTree(layerItem, node);
        }
    }
    
    // Restore selection
    if (!selectedNodesBefore.empty()) {
        for (auto* node : selectedNodesBefore) {
            findAndSelectNode(node);
        }
    }
    
    m_syncingSelection = wasSyncing;
    blockSignals(false);
}

void OutlinerTreeWidget::findAndSelectNode(const mdl::Node* targetNode)
{
    if (auto* item = findItemForNode(targetNode)) {
        item->setSelected(true);
        // Expand parents
        auto* p = item->parent();
        while(p) {
            p->setExpanded(true);
            p = p->parent();
        }
        scrollToItem(item);
    }
}

QTreeWidgetItem* OutlinerTreeWidget::findItemForNode(const mdl::Node* targetNode)
{
    // Breadth-first search or recursive search
    QList<QTreeWidgetItem*> queue;
    
    // Add top level items
    for(int i=0; i<topLevelItemCount(); ++i) {
        queue.append(topLevelItem(i));
    }
    
    while(!queue.isEmpty()) {
        auto* item = queue.takeFirst();
        if (nodeFromItem(item) == targetNode) {
            return item;
        }
        
        for(int i=0; i<item->childCount(); ++i) {
            queue.append(item->child(i));
        }
    }
    return nullptr;
}

void OutlinerTreeWidget::onDocumentSelectionChanged(const mdl::SelectionChange& /*change*/)
{
    if (m_syncingSelection) return;
    m_syncingSelection = true;
    
    // We can use change info to optimize, but full sync is safer for now
    clearSelection();
    
    const auto& selection = m_document.map().selection();
    for (const auto* node : selection.nodes) {
        findAndSelectNode(node);
    }
    
    m_syncingSelection = false;
}

void OutlinerTreeWidget::onItemSelectionChanged()
{
    if (m_syncingSelection) return;

    // Debugging selection
    auto items = selectedItems();
    qDebug() << "Outliner selection changed. Count:" << items.size();
    
    std::vector<mdl::Node*> nodes;
    for (auto* item : items) {
        if (auto* node = nodeFromItem(item)) {
            nodes.push_back(node);
            qDebug() << "  Selected Node:" << QString::fromStdString(node->name());
        }
    }
    
    m_syncingSelection = true;
    
    {
        // Use a transaction to ensure atomic update and undo/redo support
        mdl::Transaction transaction(m_document.map(), "Select Objects");
        
        // Always deselect all first to ensure we match the UI state exactly.
        // Even if UI adds to selection, 'nodes' contains ALL selected items,
        // so clearing and re-selecting 'nodes' is correct.
        mdl::deselectAll(m_document.map());
        
        if (!nodes.empty()) {
            mdl::selectNodes(m_document.map(), nodes);
        }
        transaction.commit();
    }
    
    m_syncingSelection = false;
}

void OutlinerTreeWidget::mousePressEvent(QMouseEvent* event)
{
    // Handle Lock/Vis clicks
    auto* item = itemAt(event->pos());
    if (item) {
        int column = header()->logicalIndexAt(event->pos().x());
        auto* node = nodeFromItem(item);
        
        if (node) {
            if (column == 2) { // Lock
                bool newLocked = !node->locked();
                if (newLocked) {
                    mdl::lockNodes(m_document.map(), {node});
                } else {
                    mdl::unlockNodes(m_document.map(), {node});
                }
                refreshTreeItemRecursively(item);
                return;
            } else if (column == 3) { // Vis
                bool newVisible = !node->visible();
                if (newVisible) {
                    mdl::showNodes(m_document.map(), {node});
                } else {
                    mdl::hideNodes(m_document.map(), {node});
                }
                refreshTreeItemRecursively(item);
                return;
            }
        }
    }
    
    QTreeWidget::mousePressEvent(event);
}

void OutlinerTreeWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    QTreeWidget::mouseDoubleClickEvent(event);
}

void OutlinerTreeWidget::keyPressEvent(QKeyEvent* event)
{
    QTreeWidget::keyPressEvent(event);
}

void OutlinerTreeWidget::drawRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QTreeWidget::drawRow(painter, option, index);
}

void OutlinerTreeWidget::dragEnterEvent(QDragEnterEvent* event)
{
    event->acceptProposedAction(); // Simplify for now
}

void OutlinerTreeWidget::dragMoveEvent(QDragMoveEvent* event)
{
    event->acceptProposedAction();
}

void OutlinerTreeWidget::dropEvent(QDropEvent* event)
{
    // Implement reparenting logic here
    // Get source node from mime data
    // Get target node from itemAt(event->pos())
    // Call document->reparentNodes(...)
    
    QTreeWidget::dropEvent(event); // Default behavior (visual only) - needs override
}

void OutlinerTreeWidget::updateVisibilityIconRecursively(QTreeWidgetItem* item, bool isVisible)
{
    if (!item) return;

    item->setIcon(3, isVisible ? m_visibleIcon : m_hiddenIcon);
    item->setForeground(0, isVisible ? QColor(224, 224, 224) : QColor(128, 128, 128));

    for (int i = 0; i < item->childCount(); ++i) {
        auto* child = item->child(i);
        auto* childNode = nodeFromItem(child);
        updateVisibilityIconRecursively(child, childNode ? childNode->visible() : isVisible);
    }
}

} // namespace tb::ui
