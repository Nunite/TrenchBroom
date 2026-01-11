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
#include <QScrollBar>
#include <QMenu>
#include <QContextMenuEvent>

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
#include "mdl/Map_Layers.h"
#include "mdl/EditorContext.h"
#include "mdl/Selection.h"
#include "mdl/Transaction.h"

#include "ui/MapDocument.h"
#include "ui/MapFrame.h"
#include "ui/QtUtils.h"
#include "ui/ViewUtils.h"
#include "io/ResourceUtils.h"
#include "kdl/memory_utils.h"
#include "kdl/vector_utils.h"

namespace tb::ui
{

static constexpr int WorldspawnItemRole = Qt::UserRole + 1;
static constexpr int WorldspawnLayerRole = Qt::UserRole + 2;

static bool isWorldspawnItem(const QTreeWidgetItem* item)
{
    return item && item->data(0, WorldspawnItemRole).toBool();
}

static mdl::LayerNode* worldspawnLayerFromItem(const QTreeWidgetItem* worldspawnItem)
{
    if (!isWorldspawnItem(worldspawnItem)) {
        return nullptr;
    }
    auto* node = worldspawnItem->data(0, WorldspawnLayerRole).value<mdl::Node*>();
    return dynamic_cast<mdl::LayerNode*>(node);
}

static bool isBrushEntityNode(const mdl::Node* node)
{
    const auto* entityNode = dynamic_cast<const mdl::EntityNode*>(node);
    if (!entityNode) {
        return false;
    }

    if (const auto* definition = entityNode->entity().definition()) {
        return mdl::getType(*definition) == mdl::EntityDefinitionType::Brush;
    }

    return entityNode->childCount() > 0;
}

static int sortTypeRank(const OutlinerTreeWidget::SortMode mode, const mdl::Node* node)
{
    if (mode != OutlinerTreeWidget::SortMode::Type) {
        return 0;
    }

    if (dynamic_cast<const mdl::GroupNode*>(node)) {
        return 0;
    }

    if (const auto* entity = dynamic_cast<const mdl::EntityNode*>(node)) {
        return isBrushEntityNode(entity) ? 2 : 1;
    }

    if (dynamic_cast<const mdl::BrushNode*>(node)) {
        return 3;
    }

    return 4;
}

template <typename NodeT>
static void sortNodes(
    const OutlinerTreeWidget::SortMode mode,
    std::vector<NodeT*>& nodes)
{
    if (mode == OutlinerTreeWidget::SortMode::Default) {
        return;
    }

    const auto cmp = [&](const NodeT* a, const NodeT* b) {
        if (a == nullptr || b == nullptr) {
            return a < b;
        }

        if (mode == OutlinerTreeWidget::SortMode::Type) {
            const auto ar = sortTypeRank(mode, a);
            const auto br = sortTypeRank(mode, b);
            if (ar != br) {
                return ar < br;
            }
        }

        const auto an = QString::fromStdString(a->name());
        const auto bn = QString::fromStdString(b->name());
        const auto c = QString::compare(an, bn, Qt::CaseInsensitive);
        return c == 0 ? a < b : c < 0;
    };

    nodes = kdl::vec_sort(std::move(nodes), cmp);
}

template <typename NodeFromItem>
static std::vector<mdl::Node*> collectBrushNodesToMoveToBrushEntity(
    const QList<QTreeWidgetItem*>& selectedItems,
    const NodeFromItem& nodeFromItem,
    mdl::EntityNode* targetEntity)
{
    auto draggedBrushNodes = std::vector<mdl::BrushNode*>{};
    draggedBrushNodes.reserve(static_cast<size_t>(selectedItems.size()));

    auto selectionAllBrushes = !selectedItems.empty();
    for (auto* selectedItem : selectedItems) {
        auto* selectedNode = nodeFromItem(selectedItem);
        if (auto* brushNode = dynamic_cast<mdl::BrushNode*>(selectedNode)) {
            draggedBrushNodes.push_back(brushNode);
        } else {
            selectionAllBrushes = false;
        }
    }

    if (!selectionAllBrushes || draggedBrushNodes.empty()) {
        return {};
    }

    auto nodesToMove = std::vector<mdl::Node*>{};
    nodesToMove.reserve(draggedBrushNodes.size());

    for (auto* node : draggedBrushNodes) {
        if (node && targetEntity != node->parent() && targetEntity->canAddChild(node)) {
            nodesToMove.push_back(node);
        }
    }

    return nodesToMove;
}

template <typename NodeFromItem>
static std::vector<mdl::Node*> collectBrushNodesToMoveToWorldspawn(
    const QList<QTreeWidgetItem*>& selectedItems,
    const NodeFromItem& nodeFromItem,
    mdl::LayerNode* targetLayer)
{
    if (!targetLayer) {
        return {};
    }

    auto draggedBrushNodes = std::vector<mdl::BrushNode*>{};
    draggedBrushNodes.reserve(static_cast<size_t>(selectedItems.size()));

    auto selectionAllBrushes = !selectedItems.empty();
    for (auto* selectedItem : selectedItems) {
        auto* selectedNode = nodeFromItem(selectedItem);
        if (auto* brushNode = dynamic_cast<mdl::BrushNode*>(selectedNode)) {
            draggedBrushNodes.push_back(brushNode);
        } else {
            selectionAllBrushes = false;
        }
    }

    if (!selectionAllBrushes || draggedBrushNodes.empty()) {
        return {};
    }

    auto nodesToMove = std::vector<mdl::Node*>{};
    nodesToMove.reserve(draggedBrushNodes.size());

    for (auto* node : draggedBrushNodes) {
        if (!node) {
            continue;
        }

        auto* parentEntity = dynamic_cast<mdl::EntityNode*>(node->parent());
        if (!parentEntity || !isBrushEntityNode(parentEntity)) {
            continue;
        }

        if (targetLayer != node->parent() && targetLayer->canAddChild(node)) {
            nodesToMove.push_back(node);
        }
    }

    return nodesToMove;
}

template <typename NodeFromItem>
static std::vector<mdl::Node*> collectNodesToMoveToGroup(
    mdl::Map& map,
    const QList<QTreeWidgetItem*>& selectedItems,
    const NodeFromItem& nodeFromItem,
    mdl::GroupNode* targetGroup)
{
    auto nodes = std::vector<mdl::Node*>{};
    nodes.reserve(static_cast<size_t>(selectedItems.size()));

    auto* world = map.world();

    for (auto* item : selectedItems) {
        auto* node = nodeFromItem(item);
        if (!node) {
            continue;
        }

        if (dynamic_cast<mdl::WorldNode*>(node) || dynamic_cast<mdl::LayerNode*>(node)) {
            continue;
        }

        if (auto* brushNode = dynamic_cast<mdl::BrushNode*>(node)) {
            auto* entityBase = brushNode->entity();
            if (entityBase && entityBase != world) {
                if (auto* entityNode = dynamic_cast<mdl::EntityNode*>(entityBase)) {
                    nodes.push_back(entityNode);
                    continue;
                }
            }

            nodes.push_back(brushNode);
            continue;
        }

        if (auto* entityNode = dynamic_cast<mdl::EntityNode*>(node)) {
            nodes.push_back(entityNode);
            continue;
        }
    }

    nodes = kdl::vec_sort_and_remove_duplicates(std::move(nodes));

    auto reparentableNodes = std::vector<mdl::Node*>{};
    reparentableNodes.reserve(nodes.size());

    for (auto* node : nodes) {
        if (!node) {
            continue;
        }
        if (targetGroup == node || targetGroup == node->parent()) {
            continue;
        }
        if (targetGroup->isDescendantOf(node)) {
            continue;
        }
        if (!targetGroup->canAddChild(node)) {
            continue;
        }
        reparentableNodes.push_back(node);
    }

    return reparentableNodes;
}

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
        [this](const auto&) { scheduleUpdateTree(); }); // Naive full update for now
    m_notifierConnection += m_document.map().nodesWereRemovedNotifier.connect(
        [this](const auto&) { scheduleUpdateTree(); });
        
    // Optimize nodeDidChange: only update item text/icon, do NOT rebuild tree
    m_notifierConnection += m_document.map().nodesDidChangeNotifier.connect(
        [this](const std::vector<mdl::Node*>& nodes) {
             auto needsRebuild = false;
             for (auto* node : nodes) {
                if (auto* item = findItemForNode(node)) {
                    auto* actualParentNode = nodeFromItem(item->parent());
                    auto* expectedParentNode = node->parent();

                    if (actualParentNode != expectedParentNode) {
                        needsRebuild = true;
                    } else {
                        setupTreeItem(item, node);
                    }
                }
            }

            if (needsRebuild) {
                scheduleUpdateTree();
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
        [this](auto&) { scheduleUpdateTree(); });
    m_notifierConnection += m_document.map().mapWasCreatedNotifier.connect(
        [this](auto&) { scheduleUpdateTree(); });
    m_notifierConnection += m_document.map().mapWasClearedNotifier.connect(
        [this](auto&) { scheduleUpdateTree(); });
    
    updateTree();
}

OutlinerTreeWidget::~OutlinerTreeWidget() = default;

void OutlinerTreeWidget::scheduleUpdateTree(mdl::Node* revealNode)
{
    if (revealNode) {
        m_revealAfterUpdate = revealNode;
    }

    if (m_updateTreeQueued) {
        return;
    }

    m_updateTreeQueued = true;
    QTimer::singleShot(0, this, [this]() {
        m_updateTreeQueued = false;
        updateTree();

        if (m_revealAfterUpdate) {
            if (auto* revealItem = findItemForNode(m_revealAfterUpdate)) {
                revealItem->setExpanded(true);
                scrollToItem(revealItem);
            }
            m_revealAfterUpdate = nullptr;
        }
    });
}

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
    m_itemForNode[node] = item;

    // Icon and Type logic
    if (auto* layer = dynamic_cast<mdl::LayerNode*>(node)) {
        item->setIcon(0, m_groupIcon);
        item->setText(1, QString("%1 objects").arg(layer->childCount()));
    } else if (dynamic_cast<mdl::GroupNode*>(node)) {
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
    if (dynamic_cast<mdl::LayerNode*>(node)) {
    } else if (auto* container = dynamic_cast<mdl::GroupNode*>(node)) {
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
        auto children = group->children();
        sortNodes(m_sortMode, children);
        for (auto* child : children) {
            addNodeToTree(item, child);
        }
    } else if (auto* entity = dynamic_cast<mdl::EntityNode*>(node)) {
        auto children = entity->children();
        sortNodes(m_sortMode, children);
        for (auto* child : children) {
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

    auto expandedNodesBefore = std::unordered_map<const mdl::Node*, bool>{};
    auto expandedWorldspawnBefore = std::unordered_map<const mdl::LayerNode*, bool>{};
    captureExpandedState(expandedNodesBefore, expandedWorldspawnBefore);

    m_itemForNode.clear();
    clear();

    auto* world = m_document.map().world();
    if (!world) {
        m_syncingSelection = wasSyncing;
        blockSignals(false);
        return;
    }

    // We can iterate layers if we want to show layers, OR we can show entities directly if they are top-level in the layer.
    // The user screenshot showed entities at top level.
    // In TrenchBroom, everything is in a layer.
    // If we want to mimic "Outliner", we might want to flatten layers or show them as folders.
    // Let's iterate all layers and their children.

    const auto addLayerContents = [&](QTreeWidgetItem* layerItem, mdl::LayerNode* layer) {
        auto groupNodes = std::vector<mdl::Node*>{};
        auto entityNodes = std::vector<mdl::Node*>{};
        auto otherNodes = std::vector<mdl::Node*>{};
        auto worldspawnBrushes = std::vector<mdl::BrushNode*>{};

        for (auto* node : layer->children()) {
            if (auto* brushNode = dynamic_cast<mdl::BrushNode*>(node)) {
                worldspawnBrushes.push_back(brushNode);
            } else if (dynamic_cast<mdl::GroupNode*>(node)) {
                groupNodes.push_back(node);
            } else if (dynamic_cast<mdl::EntityNode*>(node)) {
                entityNodes.push_back(node);
            } else {
                otherNodes.push_back(node);
            }
        }

        sortNodes(m_sortMode, groupNodes);
        sortNodes(m_sortMode, entityNodes);
        sortNodes(m_sortMode, otherNodes);

        for (auto* node : groupNodes) {
            addNodeToTree(layerItem, node);
        }

        for (auto* node : entityNodes) {
            addNodeToTree(layerItem, node);
        }

        if (!worldspawnBrushes.empty()) {
            auto* worldspawnItem = new QTreeWidgetItem(layerItem);
            worldspawnItem->setText(0, "worldspawn");
            worldspawnItem->setText(1, QString("%1 brushes").arg(worldspawnBrushes.size()));
            worldspawnItem->setIcon(0, m_brushEntityIcon);
            worldspawnItem->setData(0, WorldspawnItemRole, true);
            worldspawnItem->setData(0, WorldspawnLayerRole, QVariant::fromValue(static_cast<mdl::Node*>(layer)));
            worldspawnItem->setExpanded(false);

            for (auto* brushNode : worldspawnBrushes) {
                addNodeToTree(worldspawnItem, brushNode);
            }
        }

        for (auto* node : otherNodes) {
            addNodeToTree(layerItem, node);
        }
    };
    
    // Default Layer
    auto* defaultLayer = world->defaultLayer();
    if (defaultLayer) {
        auto* defaultLayerItem = new QTreeWidgetItem(invisibleRootItem());
        setupTreeItem(defaultLayerItem, defaultLayer);
        defaultLayerItem->setExpanded(false);
        addLayerContents(defaultLayerItem, defaultLayer);
    }

    // Custom Layers
    auto customLayers = world->customLayersUserSorted();
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
        setupTreeItem(layerItem, layer);
        layerItem->setExpanded(false);

        addLayerContents(layerItem, layer);
    }
    
    restoreExpandedState(expandedNodesBefore, expandedWorldspawnBefore);

    // Restore selection
    if (!selectedNodesBefore.empty()) {
        for (auto* node : selectedNodesBefore) {
            findAndSelectNode(node);
        }
    }

    applyFilter();
    
    m_syncingSelection = wasSyncing;
    blockSignals(false);
}

void OutlinerTreeWidget::setFilterText(const QString& text)
{
    const auto next = text.trimmed();
    const auto nextHasQuery = !next.isEmpty();

    if (!m_filterActive && nextHasQuery) {
        m_expandedBeforeFilter.clear();
        m_worldspawnExpandedBeforeFilter.clear();

        captureExpandedState(m_expandedBeforeFilter, m_worldspawnExpandedBeforeFilter);
        m_filterActive = true;
    } else if (m_filterActive && !nextHasQuery) {
        m_filterText = {};

        auto stack = std::vector<QTreeWidgetItem*>{};
        stack.reserve(static_cast<size_t>(topLevelItemCount()));

        for (int i = 0; i < topLevelItemCount(); ++i) {
            if (auto* item = topLevelItem(i)) {
                stack.push_back(item);
            }
        }

        while (!stack.empty()) {
            auto* item = stack.back();
            stack.pop_back();

            item->setHidden(false);

            for (int i = 0; i < item->childCount(); ++i) {
                if (auto* child = item->child(i)) {
                    stack.push_back(child);
                }
            }
        }

        restoreExpandedState(m_expandedBeforeFilter, m_worldspawnExpandedBeforeFilter);
        m_expandedBeforeFilter.clear();
        m_worldspawnExpandedBeforeFilter.clear();
        m_filterActive = false;
        return;
    }

    m_filterText = next;
    applyFilter();
}

void OutlinerTreeWidget::applyFilter()
{
    const auto query = m_filterText;
    const auto hasQuery = !query.isEmpty();

    if (!hasQuery) {
        auto stack = std::vector<QTreeWidgetItem*>{};
        stack.reserve(static_cast<size_t>(topLevelItemCount()));

        for (int i = 0; i < topLevelItemCount(); ++i) {
            if (auto* item = topLevelItem(i)) {
                stack.push_back(item);
            }
        }

        while (!stack.empty()) {
            auto* item = stack.back();
            stack.pop_back();

            item->setHidden(false);
            for (int i = 0; i < item->childCount(); ++i) {
                if (auto* child = item->child(i)) {
                    stack.push_back(child);
                }
            }
        }
        return;
    }

    struct Frame {
        QTreeWidgetItem* item;
        bool visited;
    };

    auto stack = std::vector<Frame>{};
    stack.reserve(static_cast<size_t>(topLevelItemCount()));

    for (int i = 0; i < topLevelItemCount(); ++i) {
        if (auto* item = topLevelItem(i)) {
            stack.push_back(Frame{item, false});
        }
    }

    while (!stack.empty()) {
        auto frame = stack.back();
        stack.pop_back();

        auto* item = frame.item;
        if (!item) {
            continue;
        }

        if (!frame.visited) {
            stack.push_back(Frame{item, true});
            for (int i = 0; i < item->childCount(); ++i) {
                if (auto* child = item->child(i)) {
                    stack.push_back(Frame{child, false});
                }
            }
            continue;
        }

        auto anyChildShown = false;
        for (int i = 0; i < item->childCount(); ++i) {
            if (auto* child = item->child(i)) {
                anyChildShown = anyChildShown || !child->isHidden();
            }
        }

        const auto selfShown = item->text(0).contains(query, Qt::CaseInsensitive);
        const auto shown = selfShown || anyChildShown;
        item->setHidden(!shown);

        if (shown) {
            item->setExpanded(anyChildShown);
        }
    }
}

void OutlinerTreeWidget::setSortMode(SortMode mode)
{
    if (m_sortMode == mode) {
        return;
    }
    m_sortMode = mode;
    scheduleUpdateTree();
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
        if (m_suppressScrollToSelectionCount == 0) {
            scrollToItem(item);
        }
    }
}

QTreeWidgetItem* OutlinerTreeWidget::findItemForNode(const mdl::Node* targetNode)
{
    if (!targetNode) {
        return nullptr;
    }

    if (const auto it = m_itemForNode.find(targetNode); it != m_itemForNode.end()) {
        return it->second;
    }

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
    blockSignals(true);
    clearSelection();
    blockSignals(false);

    const auto& selection = m_document.map().selection();

    auto entitiesWithSelectedChildren = std::vector<const mdl::EntityNode*>{};
    for (const auto* node : selection.nodes) {
        if (dynamic_cast<const mdl::LayerNode*>(node) || dynamic_cast<const mdl::WorldNode*>(node)) {
            continue;
        }

        if (const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(node)) {
            if (const auto* entityBase = brushNode->entity()) {
                if (const auto* entityNode = dynamic_cast<const mdl::EntityNode*>(entityBase)) {
                    if (!kdl::vec_contains(entitiesWithSelectedChildren, entityNode)) {
                        entitiesWithSelectedChildren.push_back(entityNode);
                    }
                }
            }
        }
    }

    auto nodesToSelect = std::vector<const mdl::Node*>{};

    const auto addNode = [&](const mdl::Node* node) {
        if (!node) return;
        if (!kdl::vec_contains(nodesToSelect, node)) {
            nodesToSelect.push_back(node);
        }
    };

    auto suppressedChildren = std::vector<const mdl::Node*>{};

    for (const auto* entityNode : entitiesWithSelectedChildren) {
        if (!entityNode || !entityNode->hasChildren()) {
            continue;
        }

        const auto& children = entityNode->children();
        if (children.empty()) {
            continue;
        }

        auto allChildrenSelected = true;
        for (const auto* child : children) {
            if (!kdl::vec_contains(selection.nodes, child)) {
                allChildrenSelected = false;
                break;
            }
        }

        if (allChildrenSelected) {
            addNode(entityNode);
            for (const auto* child : children) {
                if (!kdl::vec_contains(suppressedChildren, child)) {
                    suppressedChildren.push_back(child);
                }
            }
        }
    }

    for (const auto* node : selection.nodes) {
        if (dynamic_cast<const mdl::LayerNode*>(node) || dynamic_cast<const mdl::WorldNode*>(node)) {
            continue;
        }

        if (kdl::vec_contains(suppressedChildren, node)) {
            continue;
        }

        addNode(node);
    }

    for (const auto* node : nodesToSelect) {
        findAndSelectNode(node);
    }

    for (const auto* node : nodesToSelect) {
        if (const auto* entityNode = dynamic_cast<const mdl::EntityNode*>(node)) {
            if (entityNode->hasChildren()) {
                if (auto* item = findItemForNode(entityNode)) {
                    item->setExpanded(false);
                    setCurrentItem(item);
                    scrollToItem(item);
                }
            }
        }
    }
    
    m_syncingSelection = false;
}

void OutlinerTreeWidget::onItemSelectionChanged()
{
    if (m_syncingSelection) return;

    auto items = selectedItems();
    
    std::vector<mdl::Node*> nodes;
    for (auto* item : items) {
        if (auto* node = nodeFromItem(item)) {
            if (dynamic_cast<mdl::LayerNode*>(node) || dynamic_cast<mdl::WorldNode*>(node)) {
                continue;
            }
            nodes.push_back(node);
        }
    }

    if (!items.empty() && nodes.empty()) {
        return;
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
                const auto scrollValue = verticalScrollBar()->value();
                ++m_suppressScrollToSelectionCount;

                bool newLocked = !node->locked();
                if (newLocked) {
                    mdl::lockNodes(m_document.map(), {node});
                } else {
                    mdl::unlockNodes(m_document.map(), {node});
                }
                refreshTreeItemRecursively(item);

                QTimer::singleShot(0, this, [this, scrollValue]() {
                    verticalScrollBar()->setValue(scrollValue);
                    --m_suppressScrollToSelectionCount;
                });
                return;
            } else if (column == 3) { // Vis
                const auto scrollValue = verticalScrollBar()->value();
                ++m_suppressScrollToSelectionCount;

                bool newVisible = !node->visible();
                if (newVisible) {
                    mdl::showNodes(m_document.map(), {node});
                } else {
                    mdl::hideNodes(m_document.map(), {node});
                }
                refreshTreeItemRecursively(item);

                QTimer::singleShot(0, this, [this, scrollValue]() {
                    verticalScrollBar()->setValue(scrollValue);
                    --m_suppressScrollToSelectionCount;
                });
                return;
            }
        }
    }
    
    QTreeWidget::mousePressEvent(event);
}

void OutlinerTreeWidget::contextMenuEvent(QContextMenuEvent* event)
{
    auto* item = itemAt(event->pos());
    if (!item) {
        return;
    }

    auto* node = nodeFromItem(item);
    const auto setSingleSelectionIfNeeded = [&]() {
        if (item->isSelected()) {
            return;
        }

        const auto wasSyncing = m_syncingSelection;
        m_syncingSelection = true;
        blockSignals(true);
        clearSelection();
        item->setSelected(true);
        setCurrentItem(item);
        blockSignals(false);
        m_syncingSelection = wasSyncing;

        if (node) {
            mdl::Transaction transaction(m_document.map(), "Select Objects");
            mdl::deselectAll(m_document.map());
            mdl::selectNodes(m_document.map(), {node});
            transaction.commit();
        }
    };

    const auto addFocusAction = [&](QMenu& menu) {
        if (auto* mapFrame = qobject_cast<MapFrame*>(window())) {
            if (auto* focusAction = mapFrame->findAction("Menu/View/Camera/Focus on Selection")) {
                if (!menu.actions().isEmpty()) {
                    menu.addSeparator();
                }
                menu.addAction(focusAction);
            }
        }
    };

    if (auto* layerNode = dynamic_cast<mdl::LayerNode*>(node)) {
        auto& map = m_document.map();
        auto popupMenu = QMenu{this};

        auto* makeActiveAction = popupMenu.addAction(tr("Make active layer"), this, [&, layerNode]() {
            mdl::setCurrentLayer(map, layerNode);
        });
        auto* moveSelectionToLayerAction =
            popupMenu.addAction(tr("Move selection to layer"), this, [&, layerNode]() {
                mdl::moveSelectedNodesToLayer(map, layerNode);
            });
        auto* selectAllInLayerAction =
            popupMenu.addAction(tr("Select all in layer"), this, [&, layerNode]() {
                mdl::selectAllInLayers(map, {layerNode});
            });

        popupMenu.addSeparator();

        auto* toggleLayerVisibleAction = popupMenu.addAction(
            layerNode->hidden() ? tr("Show layer") : tr("Hide layer"), this, [&, layerNode]() {
                if (!layerNode->hidden()) {
                    mdl::hideNodes(map, std::vector<mdl::Node*>{layerNode});
                } else {
                    mdl::resetNodeVisibility(map, std::vector<mdl::Node*>{layerNode});
                }
            });
        auto* isolateLayerAction = popupMenu.addAction(tr("Isolate layer"), this, [&, layerNode]() {
            mdl::isolateLayers(map, std::vector<mdl::LayerNode*>{layerNode});
        });
        auto* toggleLayerLockedAction = popupMenu.addAction(
            layerNode->locked() ? tr("Unlock layer") : tr("Lock layer"), this, [&, layerNode]() {
                if (!layerNode->locked()) {
                    mdl::lockNodes(map, std::vector<mdl::Node*>{layerNode});
                } else {
                    mdl::resetNodeLockingState(map, std::vector<mdl::Node*>{layerNode});
                }
            });
        auto* toggleLayerOmitFromExportAction =
            popupMenu.addAction(tr("Omit From Export"), this, [&, layerNode]() {
                mdl::setOmitLayerFromExport(map, layerNode, !layerNode->layer().omitFromExport());
            });

        popupMenu.addSeparator();

        auto* showAllLayersAction = popupMenu.addAction(tr("Show All Layers"), this, [&]() {
            const auto layers = map.world()->allLayers();
            mdl::resetNodeVisibility(map, kdl::vec_static_cast<mdl::Node*>(layers));
        });
        auto* hideAllLayersAction = popupMenu.addAction(tr("Hide All Layers"), this, [&]() {
            const auto layers = map.world()->allLayers();
            mdl::hideNodes(map, kdl::vec_static_cast<mdl::Node*>(layers));
        });

        popupMenu.addSeparator();

        auto* unlockAllLayersAction = popupMenu.addAction(tr("Unlock All Layers"), this, [&]() {
            const auto layers = map.world()->allLayers();
            mdl::resetNodeLockingState(map, kdl::vec_static_cast<mdl::Node*>(layers));
        });
        auto* lockAllLayersAction = popupMenu.addAction(tr("Lock All Layers"), this, [&]() {
            const auto layers = map.world()->allLayers();
            mdl::lockNodes(map, kdl::vec_static_cast<mdl::Node*>(layers));
        });

        popupMenu.addSeparator();

        auto* renameLayerAction = popupMenu.addAction(tr("Rename Layer"), this, [&, layerNode]() {
            const auto name = queryLayerName(this, layerNode->name());
            if (!name.empty()) {
                mdl::renameLayer(map, layerNode, name);
            }
        });
        auto* removeLayerAction = popupMenu.addAction(tr("Remove Layer"), this, [&, layerNode]() {
            auto* defaultLayerNode = map.world()->defaultLayer();

            auto transaction = mdl::Transaction{map, "Remove Layer " + layerNode->name()};
            mdl::deselectAll(map);
            if (layerNode->hasChildren()) {
                if (!mdl::reparentNodes(map, {{defaultLayerNode, layerNode->children()}})) {
                    transaction.cancel();
                    return;
                }
            }

            if (map.editorContext().currentLayer() == layerNode) {
                mdl::setCurrentLayer(map, defaultLayerNode);
            }

            mdl::removeNodes(map, {layerNode});
            transaction.commit();
        });

        const auto canSetCurrentLayer = mdl::canSetCurrentLayer(map, layerNode);
        const auto canMoveSelectedNodes = mdl::canMoveSelectedNodesToLayer(map, layerNode);
        const auto canSelectAll = mdl::canSelectAllInLayers(map, {layerNode});
        const auto canIsolate = mdl::canIsolateLayers(map, {layerNode});

        auto canShowAll = false;
        auto canHideAll = false;
        auto canUnlockAll = false;
        auto canLockAll = false;
        for (const auto* layer : map.world()->allLayers()) {
            if (!layer->visible()) {
                canShowAll = true;
            }
            if (layer->visible()) {
                canHideAll = true;
            }
            if (layer->locked()) {
                canUnlockAll = true;
            }
            if (!layer->locked()) {
                canLockAll = true;
            }
        }

        const auto isDefaultLayer = (layerNode == map.world()->defaultLayer());
        const auto canRename = !isDefaultLayer;
        const auto canRemove = [&]() {
            if (isDefaultLayer) {
                return false;
            }
            auto* defaultLayer = map.world()->defaultLayer();
            if (!defaultLayer->locked() && !defaultLayer->hidden()) {
                return true;
            }
            for (auto* customLayer : map.world()->customLayers()) {
                if (customLayer != layerNode && !customLayer->locked() && !customLayer->hidden()) {
                    return true;
                }
            }
            return false;
        }();

        makeActiveAction->setEnabled(canSetCurrentLayer);
        moveSelectionToLayerAction->setEnabled(canMoveSelectedNodes);
        selectAllInLayerAction->setEnabled(canSelectAll);
        toggleLayerVisibleAction->setEnabled(true);
        isolateLayerAction->setEnabled(canIsolate);

        toggleLayerOmitFromExportAction->setCheckable(true);
        toggleLayerOmitFromExportAction->setChecked(layerNode->layer().omitFromExport());

        toggleLayerLockedAction->setEnabled(true);
        showAllLayersAction->setEnabled(canShowAll);
        hideAllLayersAction->setEnabled(canHideAll);
        unlockAllLayersAction->setEnabled(canUnlockAll);
        lockAllLayersAction->setEnabled(canLockAll);
        renameLayerAction->setEnabled(canRename);
        removeLayerAction->setEnabled(canRemove);

        popupMenu.exec(event->globalPos());
        event->accept();
        return;
    }

    if (auto* groupNode = dynamic_cast<mdl::GroupNode*>(node)) {
        setSingleSelectionIfNeeded();

        const auto& selection = m_document.map().selection();
        const auto isGroupInSelection = kdl::vec_contains(selection.groups, groupNode);
        const auto keepSelection = selection.hasOnlyGroups() && isGroupInSelection;

        if (!keepSelection) {
            const auto wasSyncing = m_syncingSelection;
            m_syncingSelection = true;
            blockSignals(true);
            clearSelection();
            item->setSelected(true);
            setCurrentItem(item);
            blockSignals(false);
            m_syncingSelection = wasSyncing;

            mdl::Transaction transaction(m_document.map(), "Select Objects");
            mdl::deselectAll(m_document.map());
            mdl::selectNodes(m_document.map(), {groupNode});
            transaction.commit();
        }

        QMenu menu(this);
        if (auto* mapFrame = qobject_cast<MapFrame*>(window())) {
            if (auto* renameGroupsAction = mapFrame->findAction("Menu/Edit/Rename Groups")) {
                menu.addAction(renameGroupsAction);
            }
        }
        addFocusAction(menu);

        if (!menu.actions().isEmpty()) {
            menu.exec(event->globalPos());
            event->accept();
        }
        return;
    }

    if (node) {
        setSingleSelectionIfNeeded();

        QMenu menu(this);
        addFocusAction(menu);
        if (!menu.actions().isEmpty()) {
            menu.exec(event->globalPos());
            event->accept();
        }
        return;
    }
}

void OutlinerTreeWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (auto* item = itemAt(event->pos())) {
        if (auto* node = nodeFromItem(item)) {
            if (dynamic_cast<mdl::LayerNode*>(node)) {
                item->setExpanded(!item->isExpanded());
                event->accept();
                return;
            }

            if (dynamic_cast<mdl::BrushNode*>(node)) {
                for (auto* parentItem = item->parent(); parentItem != nullptr;
                     parentItem = parentItem->parent()) {
                    if (auto* parentNode = nodeFromItem(parentItem)) {
                        if (auto* entityNode = dynamic_cast<mdl::EntityNode*>(parentNode)) {
                            {
                                mdl::Transaction transaction(m_document.map(), "Select Objects");
                                mdl::deselectAll(m_document.map());
                                mdl::selectNodes(m_document.map(), {entityNode});
                                transaction.commit();
                            }

                            QTimer::singleShot(0, this, [this, entityNode]() {
                                if (auto* entityItem = findItemForNode(entityNode)) {
                                    const auto wasSyncing = m_syncingSelection;
                                    m_syncingSelection = true;
                                    blockSignals(true);
                                    clearSelection();
                                    entityItem->setSelected(true);
                                    setCurrentItem(entityItem);
                                    blockSignals(false);
                                    m_syncingSelection = wasSyncing;
                                    entityItem->setExpanded(false);
                                    scrollToItem(entityItem);
                                }
                            });
                            return;
                        }
                    }
                }
            }
        }
    }

    QTreeWidget::mouseDoubleClickEvent(event);
}

void OutlinerTreeWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
        const auto wasSyncing = m_syncingSelection;
        m_syncingSelection = true;

        blockSignals(true);
        clearSelection();
        blockSignals(false);

        {
            mdl::Transaction transaction(m_document.map(), "Deselect Objects");
            mdl::deselectAll(m_document.map());
            transaction.commit();
        }

        m_syncingSelection = wasSyncing;
        event->accept();
        return;
    }

    if (
        (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
        && event->modifiers() == Qt::NoModifier) {
        auto nodesToDelete = std::vector<mdl::Node*>{};
        const auto items = selectedItems();
        nodesToDelete.reserve(static_cast<size_t>(items.size()));
        for (auto* item : items) {
            auto* node = nodeFromItem(item);
            if (!node) {
                continue;
            }
            if (dynamic_cast<mdl::LayerNode*>(node) || dynamic_cast<mdl::WorldNode*>(node)) {
                continue;
            }
            nodesToDelete.push_back(node);
        }

        if (!nodesToDelete.empty()) {
            auto& map = m_document.map();
            auto transaction = mdl::Transaction{map, "Delete Objects"};
            mdl::deselectAll(map);
            mdl::removeNodes(map, nodesToDelete);
            transaction.commit();
            event->accept();
            return;
        }
    }
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
    const auto indicator = dropIndicatorPosition();
    if (indicator == QAbstractItemView::OnViewport) {
        event->ignore();
        return;
    }

    auto* targetItem = itemAt(event->position().toPoint());
    if (!targetItem) {
        event->ignore();
        return;
    }

    if (auto* worldspawnItem = isWorldspawnItem(targetItem) ? targetItem : targetItem->parent();
        isWorldspawnItem(worldspawnItem)) {
        auto* layerNode = dynamic_cast<mdl::LayerNode*>(nodeFromItem(worldspawnItem->parent()));
        const auto nodesToMove = collectBrushNodesToMoveToWorldspawn(
            selectedItems(), [this](QTreeWidgetItem* item) { return nodeFromItem(item); }, layerNode);
        if (!nodesToMove.empty()) {
            event->acceptProposedAction();
            return;
        }
        event->ignore();
        return;
    }

    auto* targetNode = nodeFromItem(targetItem);
    if (!targetNode) {
        event->ignore();
        return;
    }

    if (auto* targetGroup = dynamic_cast<mdl::GroupNode*>(targetNode)) {
        auto& map = m_document.map();
        const auto nodesToMove = collectNodesToMoveToGroup(
            map,
            selectedItems(),
            [this](QTreeWidgetItem* item) { return nodeFromItem(item); },
            targetGroup);

        if (!nodesToMove.empty()) {
            event->acceptProposedAction();
            return;
        }

        event->ignore();
        return;
    }

    if (auto* targetEntity = dynamic_cast<mdl::EntityNode*>(targetNode)) {
        if (isBrushEntityNode(targetEntity)) {
            const auto nodesToMove = collectBrushNodesToMoveToBrushEntity(
                selectedItems(), [this](QTreeWidgetItem* item) { return nodeFromItem(item); }, targetEntity);
            if (!nodesToMove.empty()) {
                event->acceptProposedAction();
                return;
            }
        }
    }

    event->ignore();
}

void OutlinerTreeWidget::dropEvent(QDropEvent* event)
{
    const auto indicator = dropIndicatorPosition();
    if (indicator == QAbstractItemView::OnViewport) {
        event->ignore();
        return;
    }

    auto* targetItem = itemAt(event->position().toPoint());
    if (!targetItem) {
        event->ignore();
        return;
    }

    if (auto* worldspawnItem = isWorldspawnItem(targetItem) ? targetItem : targetItem->parent();
        isWorldspawnItem(worldspawnItem)) {
        auto& map = m_document.map();
        auto* layerNode = dynamic_cast<mdl::LayerNode*>(nodeFromItem(worldspawnItem->parent()));

        auto nodesToMove = collectBrushNodesToMoveToWorldspawn(
            selectedItems(), [this](QTreeWidgetItem* item) { return nodeFromItem(item); }, layerNode);

        if (nodesToMove.empty()) {
            event->ignore();
            return;
        }

        auto transaction = mdl::Transaction{map, "Move Brushes to worldspawn"};
        mdl::deselectAll(map);

        auto nodesToAdd = std::map<mdl::Node*, std::vector<mdl::Node*>>{};
        nodesToAdd[layerNode] = nodesToMove;
        if (!mdl::reparentNodes(map, nodesToAdd)) {
            transaction.cancel();
            event->ignore();
            return;
        }

        mdl::selectNodes(map, nodesToMove);
        transaction.commit();

        scheduleUpdateTree(layerNode);
        event->acceptProposedAction();
        return;
    }

    auto* targetNode = nodeFromItem(targetItem);
    if (!targetNode) {
        event->ignore();
        return;
    }

    if (auto* targetGroup = dynamic_cast<mdl::GroupNode*>(targetNode)) {
        auto& map = m_document.map();
        auto nodesToMove = collectNodesToMoveToGroup(
            map,
            selectedItems(),
            [this](QTreeWidgetItem* item) { return nodeFromItem(item); },
            targetGroup);

        if (nodesToMove.empty()) {
            event->ignore();
            return;
        }

        auto transaction = mdl::Transaction{map, "Move Objects to " + targetGroup->name()};
        mdl::deselectAll(map);

        auto nodesToAdd = std::map<mdl::Node*, std::vector<mdl::Node*>>{};
        nodesToAdd[targetGroup] = nodesToMove;
        if (!mdl::reparentNodes(map, nodesToAdd)) {
            transaction.cancel();
            event->ignore();
            return;
        }

        mdl::selectNodes(map, nodesToMove);
        transaction.commit();

        scheduleUpdateTree(targetGroup);
        event->acceptProposedAction();
        return;
    }

    if (auto* targetEntity = dynamic_cast<mdl::EntityNode*>(targetNode)) {
        if (isBrushEntityNode(targetEntity)) {
            auto& map = m_document.map();
            auto nodesToMove = collectBrushNodesToMoveToBrushEntity(
                selectedItems(), [this](QTreeWidgetItem* item) { return nodeFromItem(item); }, targetEntity);

            if (nodesToMove.empty()) {
                event->ignore();
                return;
            }

            auto transaction =
                mdl::Transaction{map, "Move Brushes to Entity " + targetEntity->name()};
            mdl::deselectAll(map);

            auto nodesToAdd = std::map<mdl::Node*, std::vector<mdl::Node*>>{};
            nodesToAdd[targetEntity] = nodesToMove;
            if (!mdl::reparentNodes(map, nodesToAdd)) {
                transaction.cancel();
                event->ignore();
                return;
            }

            mdl::selectNodes(map, nodesToMove);
            transaction.commit();

            scheduleUpdateTree(targetEntity);
            event->acceptProposedAction();
            return;
        }
    }

    event->ignore();
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

void OutlinerTreeWidget::captureExpandedState(
    std::unordered_map<const mdl::Node*, bool>& expandedNodes,
    std::unordered_map<const mdl::LayerNode*, bool>& expandedWorldspawn) const
{
    auto stack = std::vector<QTreeWidgetItem*>{};
    stack.reserve(static_cast<size_t>(topLevelItemCount()));

    for (int i = 0; i < topLevelItemCount(); ++i) {
        if (auto* item = topLevelItem(i)) {
            stack.push_back(item);
        }
    }

    while (!stack.empty()) {
        auto* item = stack.back();
        stack.pop_back();

        if (auto* node = nodeFromItem(item)) {
            expandedNodes[node] = item->isExpanded();
        } else if (isWorldspawnItem(item)) {
            if (auto* layer = worldspawnLayerFromItem(item)) {
                expandedWorldspawn[layer] = item->isExpanded();
            }
        }

        for (int i = 0; i < item->childCount(); ++i) {
            if (auto* child = item->child(i)) {
                stack.push_back(child);
            }
        }
    }
}

void OutlinerTreeWidget::restoreExpandedState(
    const std::unordered_map<const mdl::Node*, bool>& expandedNodes,
    const std::unordered_map<const mdl::LayerNode*, bool>& expandedWorldspawn)
{
    auto stack = std::vector<QTreeWidgetItem*>{};
    stack.reserve(static_cast<size_t>(topLevelItemCount()));

    for (int i = 0; i < topLevelItemCount(); ++i) {
        if (auto* item = topLevelItem(i)) {
            stack.push_back(item);
        }
    }

    while (!stack.empty()) {
        auto* item = stack.back();
        stack.pop_back();

        if (auto* node = nodeFromItem(item)) {
            const auto it = expandedNodes.find(node);
            if (it != expandedNodes.end()) {
                item->setExpanded(it->second);
            }
        } else if (isWorldspawnItem(item)) {
            if (auto* layer = worldspawnLayerFromItem(item)) {
                const auto it = expandedWorldspawn.find(layer);
                if (it != expandedWorldspawn.end()) {
                    item->setExpanded(it->second);
                }
            }
        }

        for (int i = 0; i < item->childCount(); ++i) {
            if (auto* child = item->child(i)) {
                stack.push_back(child);
            }
        }
    }
}

} // namespace tb::ui
