#include "ui/outliner/OutlinerTreeWidget.h"

#include <QHeaderView>
#include <QItemSelectionModel>
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
#include <QSet>

#include "mdl/Map.h"
#include "mdl/WorldNode.h"
#include "mdl/LayerNode.h"
#include "mdl/GroupNode.h"
#include "mdl/EntityNode.h"
#include "mdl/BrushNode.h"
#include "mdl/PatchNode.h"
#include "mdl/EntityDefinition.h"
#include "mdl/Map_NodeVisibility.h"
#include "mdl/Map_NodeLocking.h"
#include "mdl/Map_Selection.h"
#include "mdl/Map_Nodes.h" // for reparentNodes helpers if needed
#include "mdl/Map_Layers.h"
#include "mdl/ModelUtils.h"
#include "mdl/EditorContext.h"
#include "mdl/Selection.h"
#include "mdl/Transaction.h"

#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapViewBase.h"
#include "ui/QWidgetUtils.h"
#include "ui/ViewUtils.h"
#include "ui/ImageUtils.h"
#include "kd/memory_utils.h"
#include "kd/vector_utils.h"

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
    // Use static_cast because dynamic_cast accesses the object's vtable, which causes a crash
    // if the object has been deleted (which can happen during tree updates).
    // We know the type is correct because we set it ourselves.
    return static_cast<mdl::LayerNode*>(node);
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
    if (mode == OutlinerTreeWidget::SortMode::FileOrder) {
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
static std::vector<mdl::Node*> collectSelectedNodes(
    const QList<QTreeWidgetItem*>& selectedItems,
    const NodeFromItem& nodeFromItem)
{
    auto nodes = std::vector<mdl::Node*>{};
    nodes.reserve(static_cast<size_t>(selectedItems.size()));

    for (auto* item : selectedItems) {
        auto* node = nodeFromItem(item);
        if (!node) {
            continue;
        }

        if (dynamic_cast<mdl::WorldNode*>(node) || dynamic_cast<mdl::LayerNode*>(node)) {
            continue;
        }

        nodes.push_back(node);
    }

    return kdl::vec_sort_and_remove_duplicates(std::move(nodes));
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
        if (node && targetEntity != node->parent() && targetEntity->canAddChild(*node)) {
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

        if (targetLayer != node->parent() && targetLayer->canAddChild(*node)) {
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

    auto* world = &map.worldNode();

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
        if (targetGroup->isDescendantOf(*node)) {
            continue;
        }
        if (!targetGroup->canAddChild(*node)) {
            continue;
        }
        reparentableNodes.push_back(node);
    }

    return reparentableNodes;
}

template <typename NodeFromItem>
static std::vector<mdl::Node*> collectNodesToMoveToLayer(
    mdl::Map& map,
    const QList<QTreeWidgetItem*>& selectedItems,
    const NodeFromItem& nodeFromItem,
    mdl::LayerNode* targetLayer)
{
    auto nodes = std::vector<mdl::Node*>{};
    nodes.reserve(static_cast<size_t>(selectedItems.size()));

    auto* world = &map.worldNode();

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

        if (auto* groupNode = dynamic_cast<mdl::GroupNode*>(node)) {
            nodes.push_back(groupNode);
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
        if (targetLayer == node || targetLayer == node->parent()) {
            continue;
        }
        if (targetLayer->isDescendantOf(*node)) {
            continue;
        }
        if (!targetLayer->canAddChild(*node)) {
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

    m_notifierConnection += m_document.selectionDidChangeNotifier.connect(
        this, &OutlinerTreeWidget::onDocumentSelectionChanged);

    m_notifierConnection += m_document.groupWasOpenedNotifier.connect(
        [this]() { updateCurrentGroupHighlight(); });
    m_notifierConnection += m_document.map().editorContext().editorContextDidChangeNotifier.connect(
        [this]() { updateCurrentGroupHighlight(); });
    m_notifierConnection += m_document.groupWasClosedNotifier.connect(
        [this]() {
            updateCurrentGroupHighlight();
        });
    
    // Listen to map changes to update tree
    m_notifierConnection += m_document.nodesWereAddedNotifier.connect(
        [this](const auto&) { scheduleUpdateTree(); }); // Naive full update for now
    m_notifierConnection += m_document.nodesWereRemovedNotifier.connect(
        [this](const auto&) { scheduleUpdateTree(); });
        
    // Optimize nodeDidChange: only update item text/icon, do NOT rebuild tree
    m_notifierConnection += m_document.nodesDidChangeNotifier.connect(
        [this](const std::vector<mdl::Node*>& nodes) {
             auto needsRebuild = false;
             for (auto* node : nodes) {
                if (!node) {
                    continue;
                }

                const auto relevant =
                    dynamic_cast<mdl::LayerNode*>(node) != nullptr
                    || dynamic_cast<mdl::GroupNode*>(node) != nullptr
                    || dynamic_cast<mdl::EntityNode*>(node) != nullptr
                    || dynamic_cast<mdl::WorldNode*>(node) != nullptr;
                if (!relevant) {
                    continue;
                }

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

    m_notifierConnection += m_document.nodeVisibilityDidChangeNotifier.connect(
        [this](const std::vector<mdl::Node*>& nodes) {
            for (auto* node : nodes) {
                if (auto* item = findItemForNode(node)) {
                    refreshTreeItemRecursively(item);
                }
            }
        });

    m_notifierConnection += m_document.nodeLockingDidChangeNotifier.connect(
        [this](const std::vector<mdl::Node*>& nodes) {
            for (auto* node : nodes) {
                if (auto* item = findItemForNode(node)) {
                    refreshTreeItemRecursively(item);
                } else if (dynamic_cast<mdl::WorldNode*>(node)) {
                    for (int i = 0; i < topLevelItemCount(); ++i) {
                        refreshTreeItemRecursively(topLevelItem(i));
                    }
                }
            }
        });
    
    // Connect to map lifecycle events to ensure tree is populated when map is loaded/created
    m_notifierConnection += m_document.documentWasLoadedNotifier.connect(
        [this]() { scheduleUpdateTree(); });
    
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
                for (auto* p = revealItem->parent(); p != nullptr; p = p->parent()) {
                    p->setExpanded(true);
                }

                const auto expandRevealItem = [&]() {
                    if (const auto* node = nodeFromItem(revealItem)) {
                        if (dynamic_cast<const mdl::GroupNode*>(node)) {
                            return false;
                        }
                        if (const auto* entityNode = dynamic_cast<const mdl::EntityNode*>(node)) {
                            if (isBrushEntityNode(entityNode)) {
                                return false;
                            }
                        }
                    }
                    return true;
                }();

                revealItem->setExpanded(expandRevealItem);
                scrollToItem(revealItem);
            }
            m_revealAfterUpdate = nullptr;
        }
    });
}

void OutlinerTreeWidget::loadIcons()
{
    m_groupIcon = loadSVGIcon("Map_folder.svg");
    m_entityIcon = loadSVGIcon("Map_entity.svg");
    m_brushEntityIcon = loadSVGIcon("Map_fullcube.svg");
    m_brushIcon = loadSVGIcon("Map_cube.svg");
    
    m_visibleIcon = loadSVGIcon("object_show.svg");
    m_hiddenIcon = loadSVGIcon("object_hidden.svg");
    m_lockedIcon = loadSVGIcon("Lock_on.svg");
    m_unlockedIcon = loadSVGIcon("Lock_off.svg");
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
        auto info = QString("%1 objects").arg(container->childCount());

        if (const auto sizeIt = m_linkedGroupSetSizes.find(container->linkId());
            sizeIt != m_linkedGroupSetSizes.end() && sizeIt->second > 1u) {
            const auto indexIt = m_linkedGroupSetIndices.find(container->linkId());
            const auto index = indexIt != m_linkedGroupSetIndices.end() ? indexIt->second : 0;

            if (index > 0) {
                info += QString(" · Link %1").arg(index);
                item->setToolTip(
                    1,
                    QString("Linked group set %1 (%2 groups)")
                        .arg(index)
                        .arg(static_cast<qulonglong>(sizeIt->second)));
            } else {
                info += QString(" · Link");
                item->setToolTip(
                    1, QString("Linked group set (%1 groups)").arg(static_cast<qulonglong>(sizeIt->second)));
            }
        }

        item->setText(1, info);
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

    auto expandedNodesBefore = std::unordered_map<const mdl::Node*, bool>{};
    auto expandedWorldspawnBefore = std::unordered_map<const mdl::LayerNode*, bool>{};
    captureExpandedState(expandedNodesBefore, expandedWorldspawnBefore);

    m_itemForNode.clear();
    clear();

    auto* world = &m_document.map().worldNode();
    if (!world) {
        m_syncingSelection = wasSyncing;
        blockSignals(false);
        return;
    }

    m_linkedGroupSetSizes.clear();
    m_linkedGroupSetIndices.clear();
    {
        const auto groups = mdl::collectGroups({world});
        for (const auto* groupNode : groups) {
            if (groupNode) {
                ++m_linkedGroupSetSizes[groupNode->linkId()];
            }
        }

        auto nextIndex = 1;
        for (const auto* groupNode : groups) {
            if (!groupNode) {
                continue;
            }

            const auto& linkId = groupNode->linkId();
            const auto sizeIt = m_linkedGroupSetSizes.find(linkId);
            if (sizeIt == m_linkedGroupSetSizes.end() || sizeIt->second <= 1u) {
                continue;
            }

            if (m_linkedGroupSetIndices.find(linkId) == m_linkedGroupSetIndices.end()) {
                m_linkedGroupSetIndices.emplace(linkId, nextIndex++);
            }
        }
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
    syncSelectionFromDocument();
    updateCurrentGroupHighlight();

    applyFilter();
    
    m_syncingSelection = wasSyncing;
    blockSignals(false);
}

void OutlinerTreeWidget::updateCurrentGroupHighlight()
{
    const auto& editorContext = m_document.map().editorContext();
    const auto* currentGroup = editorContext.currentGroup();
    const auto* currentLayer = editorContext.currentLayer();

    const auto clearNode = [&](const mdl::Node* node) {
        if (!node) {
            return;
        }
        if (auto* item = findItemForNode(node)) {
            for (int c = 0; c < columnCount(); ++c) {
                item->setBackground(c, QBrush{});
            }
        }
    };

    if (m_highlightedCurrentGroup != currentGroup) {
        clearNode(m_highlightedCurrentGroup);
        m_highlightedCurrentGroup = currentGroup;
    }

    if (currentGroup) {
        if (auto* item = findItemForNode(currentGroup)) {
            const auto highlight = QBrush{QColor{135, 206, 235, 70}}; // 淡蓝色
            for (int c = 0; c < columnCount(); ++c) {
                item->setBackground(c, highlight);
            }
        }
    }

    if (m_highlightedCurrentLayer != currentLayer) {
        clearNode(m_highlightedCurrentLayer);
        m_highlightedCurrentLayer = currentLayer;
    }

    if (currentLayer) {
        if (auto* item = findItemForNode(currentLayer)) {
            const auto highlight = QBrush{QColor{255, 215, 0, 70}}; // 金色
            for (int c = 0; c < columnCount(); ++c) {
                item->setBackground(c, highlight);
            }
        }
    }

    viewport()->update();
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

    struct ParsedFilter {
        QStringList nameTerms;
        QSet<QString> types;
        int visible = -1;
        int locked = -1;
        bool selectedOnly = false;
    };

    const auto parsed = [&]() {
        auto result = ParsedFilter{};

        const auto tokens = query.split(' ', Qt::SkipEmptyParts);
        for (const auto& tokenRaw : tokens) {
            const auto token = tokenRaw.trimmed();
            if (token.isEmpty()) {
                continue;
            }

            const auto lower = token.toLower();
            if (lower.startsWith("type:")) {
                const auto values = lower.mid(5).split(',', Qt::SkipEmptyParts);
                for (const auto& v : values) {
                    const auto t = v.trimmed();
                    if (!t.isEmpty()) {
                        result.types.insert(t);
                    }
                }
                continue;
            }

            if (lower.startsWith("vis:")) {
                const auto v = lower.mid(4).trimmed();
                if (v == "visible" || v == "shown" || v == "show") {
                    result.visible = 1;
                } else if (v == "hidden" || v == "hide") {
                    result.visible = 0;
                }
                continue;
            }

            if (lower.startsWith("lock:")) {
                const auto v = lower.mid(5).trimmed();
                if (v == "locked" || v == "lock") {
                    result.locked = 1;
                } else if (v == "unlocked" || v == "unlock") {
                    result.locked = 0;
                }
                continue;
            }

            if (lower == "sel" || lower == "selected") {
                result.selectedOnly = true;
                continue;
            }

            result.nameTerms.push_back(token);
        }

        return result;
    }();

    const auto matchesItem = [&](QTreeWidgetItem* item) {
        if (!item) {
            return false;
        }

        const auto matchesName = [&]() {
            if (parsed.nameTerms.empty()) {
                return true;
            }

            const auto name = item->text(0);
            for (const auto& term : parsed.nameTerms) {
                if (!name.contains(term, Qt::CaseInsensitive)) {
                    return false;
                }
            }
            return true;
        };

        const auto matchesSelected = [&]() {
            return !parsed.selectedOnly || item->isSelected();
        };

        if (isWorldspawnItem(item)) {
            if (!parsed.types.empty() && !parsed.types.contains("worldspawn")) {
                return false;
            }

            if (auto* layer = worldspawnLayerFromItem(item)) {
                if (parsed.visible != -1 && layer->visible() != (parsed.visible == 1)) {
                    return false;
                }
                if (parsed.locked != -1 && layer->locked() != (parsed.locked == 1)) {
                    return false;
                }
            } else {
                if (parsed.visible != -1 || parsed.locked != -1) {
                    return false;
                }
            }

            return matchesSelected() && matchesName();
        }

        auto* node = nodeFromItem(item);
        if (!node) {
            return false;
        }

        if (!parsed.types.empty()) {
            auto typeName = QString{};
            if (dynamic_cast<const mdl::GroupNode*>(node)) {
                typeName = "group";
            } else if (dynamic_cast<const mdl::EntityNode*>(node)) {
                typeName = "entity";
            } else if (dynamic_cast<const mdl::BrushNode*>(node)) {
                typeName = "brush";
            } else if (dynamic_cast<const mdl::PatchNode*>(node)) {
                typeName = "patch";
            } else if (dynamic_cast<const mdl::LayerNode*>(node)) {
                typeName = "layer";
            } else if (dynamic_cast<const mdl::WorldNode*>(node)) {
                typeName = "world";
            } else {
                typeName = "other";
            }

            if (!parsed.types.contains(typeName)) {
                return false;
            }
        }

        if (parsed.visible != -1 && node->visible() != (parsed.visible == 1)) {
            return false;
        }

        if (parsed.locked != -1 && node->locked() != (parsed.locked == 1)) {
            return false;
        }

        return matchesSelected() && matchesName();
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

        const auto selfShown = matchesItem(item);
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

void OutlinerTreeWidget::syncSelectionFromDocument()
{
    const auto wasSyncing = m_syncingSelection;
    m_syncingSelection = true;

    ++m_suppressScrollToSelectionCount;

    const auto wereSignalsBlocked = signalsBlocked();
    blockSignals(true);
    clearSelection();
    blockSignals(wereSignalsBlocked);

    const auto& selection = m_document.map().selection();

    const auto appendDescendants = [](const mdl::Node* root, std::vector<const mdl::Node*>& out) {
        if (!root) {
            return;
        }

        auto stack = std::vector<const mdl::Node*>{};
        for (const auto* child : root->children()) {
            if (child) {
                stack.push_back(child);
            }
        }

        while (!stack.empty()) {
            const auto* node = stack.back();
            stack.pop_back();

            if (!kdl::vec_contains(out, node)) {
                out.push_back(node);
            }

            for (const auto* child : node->children()) {
                if (child) {
                    stack.push_back(child);
                }
            }
        }
    };

    const auto allLeafDescendantsSelected = [&](const mdl::Node* root) {
        if (!root) {
            return false;
        }

        auto stack = std::vector<const mdl::Node*>{root};
        while (!stack.empty()) {
            const auto* node = stack.back();
            stack.pop_back();

            const auto isContainer = [&]() {
                if (dynamic_cast<const mdl::GroupNode*>(node)) {
                    return true;
                }
                if (const auto* entityNode = dynamic_cast<const mdl::EntityNode*>(node)) {
                    return entityNode->hasChildren() && isBrushEntityNode(entityNode);
                }
                return false;
            }();

            if (isContainer) {
                for (const auto* child : node->children()) {
                    if (child) {
                        stack.push_back(child);
                    }
                }
                continue;
            }

            if (
                dynamic_cast<const mdl::WorldNode*>(node) || dynamic_cast<const mdl::LayerNode*>(node)) {
                continue;
            }

            if (!node->selected()) {
                return false;
            }
        }

        return true;
    };

    auto orderedContainers = std::vector<std::pair<size_t, const mdl::Node*>>{};
    for (const auto* node : selection.nodes) {
        for (auto* parent = node ? node->parent() : nullptr; parent != nullptr;
             parent = parent->parent()) {
            const auto* container = [&]() -> const mdl::Node* {
                if (dynamic_cast<const mdl::GroupNode*>(parent)) {
                    return parent;
                }
                if (const auto* entityNode = dynamic_cast<const mdl::EntityNode*>(parent)) {
                    if (entityNode->hasChildren() && isBrushEntityNode(entityNode)) {
                        return parent;
                    }
                }
                return nullptr;
            }();

            if (!container) {
                continue;
            }

            auto alreadyAdded = false;
            for (const auto& entry : orderedContainers) {
                if (entry.second == container) {
                    alreadyAdded = true;
                    break;
                }
            }
            if (alreadyAdded) {
                continue;
            }

            auto depth = size_t{0};
            for (auto* p = parent; p != nullptr; p = p->parent()) {
                ++depth;
            }

            auto inserted = false;
            for (auto it = orderedContainers.begin(); it != orderedContainers.end(); ++it) {
                if (depth < it->first) {
                    orderedContainers.insert(it, {depth, container});
                    inserted = true;
                    break;
                }
            }
            if (!inserted) {
                orderedContainers.push_back({depth, container});
            }
        }
    }

    auto nodesToSelect = std::vector<const mdl::Node*>{};

    const auto addNode = [&](const mdl::Node* node) {
        if (!node) {
            return;
        }
        if (!kdl::vec_contains(nodesToSelect, node)) {
            nodesToSelect.push_back(node);
        }
    };

    auto suppressedChildren = std::vector<const mdl::Node*>{};

    for (const auto& entry : orderedContainers) {
        const auto* containerNode = entry.second;
        if (!containerNode || !containerNode->hasChildren()) {
            continue;
        }
        if (kdl::vec_contains(suppressedChildren, containerNode)) {
            continue;
        }

        if (allLeafDescendantsSelected(containerNode)) {
            addNode(containerNode);
            appendDescendants(containerNode, suppressedChildren);
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
        if (!findItemForNode(node)) {
            --m_suppressScrollToSelectionCount;
            m_syncingSelection = wasSyncing;
            scheduleUpdateTree(const_cast<mdl::Node*>(node));
            return;
        }
    }

    for (const auto* node : nodesToSelect) {
        if (auto* item = findItemForNode(node)) {
            item->setSelected(true);
            setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);

            for (auto* p = item->parent(); p != nullptr; p = p->parent()) {
                p->setExpanded(true);
            }

            if (dynamic_cast<const mdl::GroupNode*>(node)) {
                item->setExpanded(false);
            } else if (const auto* entityNode = dynamic_cast<const mdl::EntityNode*>(node)) {
                if (isBrushEntityNode(entityNode)) {
                    item->setExpanded(false);
                }
            }
        }
    }

    --m_suppressScrollToSelectionCount;

    if (!m_filterActive) {
        for (int i = 0; i < topLevelItemCount(); ++i) {
            if (auto* layerItem = topLevelItem(i)) {
                for (int j = 0; j < layerItem->childCount(); ++j) {
                    if (auto* childItem = layerItem->child(j)) {
                        if (isWorldspawnItem(childItem)) {
                            auto hasSelectedChild = false;
                            for (int k = 0; k < childItem->childCount(); ++k) {
                                if (childItem->child(k)->isSelected()) {
                                    hasSelectedChild = true;
                                    break;
                                }
                            }
                            if (!hasSelectedChild) {
                                childItem->setExpanded(false);
                            }
                        }
                    }
                }
            }
        }
    }

    m_syncingSelection = wasSyncing;
}

void OutlinerTreeWidget::onDocumentSelectionChanged(const mdl::SelectionChange& /*change*/)
{
    if (m_syncingSelection) {
        return;
    }
    syncSelectionFromDocument();
}

void OutlinerTreeWidget::onItemSelectionChanged()
{
    if (m_syncingSelection) return;

    auto items = selectedItems();
    
    const auto& editorContext = m_document.map().editorContext();
    const auto outlinerSelectable = [&](const mdl::Node& node) {
        if (const auto* groupNode = dynamic_cast<const mdl::GroupNode*>(&node)) {
            if (groupNode->opened()) {
                return false;
            }
        }

        if (const auto* currentGroup = editorContext.currentGroup()) {
            return &node == currentGroup || node.isDescendantOf(*currentGroup);
        }

        return true;
    };

    std::vector<mdl::Node*> nodes;
    for (auto* item : items) {
        if (auto* node = nodeFromItem(item)) {
            if (dynamic_cast<mdl::LayerNode*>(node) || dynamic_cast<mdl::WorldNode*>(node)) {
                continue;
            }
            if (outlinerSelectable(*node)) {
                nodes.push_back(node);
            }
        }
    }

    if (!items.empty() && nodes.empty()) {
        syncSelectionFromDocument();
        return;
    }
    
    m_syncingSelection = true;
    
    {
        mdl::Transaction transaction(m_document.map(), "Select Objects");
        
        mdl::deselectAll(m_document.map());
        
        if (!nodes.empty()) {
            mdl::selectNodes(m_document.map(), nodes);
        }
        transaction.commit();
    }
    
    m_syncingSelection = false;
    syncSelectionFromDocument();
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
                const auto& editorContext = m_document.map().editorContext();
                if (const auto* currentGroup = editorContext.currentGroup()) {
                    const auto inCurrentGroup =
                        node == currentGroup || node->isDescendantOf(*currentGroup);
                    if (!inCurrentGroup) {
                        return;
                    }
                }

                if (const auto* groupNode = dynamic_cast<const mdl::GroupNode*>(node)) {
                    if (groupNode->opened()) {
                        return;
                    }
                }

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
    const auto& editorContext = m_document.map().editorContext();
    const auto outlinerSelectable = [&](const mdl::Node& node_) {
        if (const auto* groupNode = dynamic_cast<const mdl::GroupNode*>(&node_)) {
            if (groupNode->opened()) {
                return false;
            }
        }

        if (const auto* currentGroup = editorContext.currentGroup()) {
            return &node_ == currentGroup || node_.isDescendantOf(*currentGroup);
        }

        return true;
    };
    const auto setSingleSelectionIfNeeded = [&]() {
        if (item->isSelected()) {
            return;
        }

        if (node) {
            if (!outlinerSelectable(*node)) {
                syncSelectionFromDocument();
                return;
            }
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
        if (auto* mapWindow = qobject_cast<MapWindow*>(window())) {
            if (auto* focusAction = mapWindow->findAction("Menu/View/Camera/Focus on Selection")) {
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
            const auto layers = map.worldNode().allLayers();
            mdl::resetNodeVisibility(map, kdl::vec_static_cast<mdl::Node*>(layers));
        });
        auto* hideAllLayersAction = popupMenu.addAction(tr("Hide All Layers"), this, [&]() {
            const auto layers = map.worldNode().allLayers();
            mdl::hideNodes(map, kdl::vec_static_cast<mdl::Node*>(layers));
        });

        popupMenu.addSeparator();

        auto* unlockAllLayersAction = popupMenu.addAction(tr("Unlock All Layers"), this, [&]() {
            const auto layers = map.worldNode().allLayers();
            mdl::resetNodeLockingState(map, kdl::vec_static_cast<mdl::Node*>(layers));
        });
        auto* lockAllLayersAction = popupMenu.addAction(tr("Lock All Layers"), this, [&]() {
            const auto layers = map.worldNode().allLayers();
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
            auto* defaultLayerNode = map.worldNode().defaultLayer();

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
        for (const auto* layer : map.worldNode().allLayers()) {
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

        const auto isDefaultLayer = (layerNode == map.worldNode().defaultLayer());
        const auto canRename = !isDefaultLayer;
        const auto canRemove = [&]() {
            if (isDefaultLayer) {
                return false;
            }
            auto* defaultLayer = map.worldNode().defaultLayer();
            if (!defaultLayer->locked() && !defaultLayer->hidden()) {
                return true;
            }
            for (auto* customLayer : map.worldNode().customLayers()) {
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
        if (auto* mapWindow = qobject_cast<MapWindow*>(window())) {
            if (auto* renameGroupsAction = mapWindow->findAction("Menu/Edit/Rename Groups")) {
                menu.addAction(renameGroupsAction);
            }
        }
        addFocusAction(menu);

        if (!menu.actions().isEmpty()) {
            menu.addSeparator();
        }

        const auto nodes = collectSelectedNodes(selectedItems(), [this](QTreeWidgetItem* i) { return nodeFromItem(i); });
        auto* duplicateAction = menu.addAction(tr("Duplicate"), this, [this]() {
            mdl::duplicateSelectedNodes(m_document.map());
        });
        duplicateAction->setEnabled(!nodes.empty());

        auto* deleteAction = menu.addAction(tr("Delete"), this, [this]() {
            mdl::removeSelectedNodes(m_document.map());
        });
        deleteAction->setEnabled(!nodes.empty());

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
            menu.addSeparator();
        }

        const auto nodes = collectSelectedNodes(selectedItems(), [this](QTreeWidgetItem* i) { return nodeFromItem(i); });
        auto* duplicateAction = menu.addAction(tr("Duplicate"), this, [this]() {
            mdl::duplicateSelectedNodes(m_document.map());
        });
        duplicateAction->setEnabled(!nodes.empty());

        auto* deleteAction = menu.addAction(tr("Delete"), this, [this]() {
            mdl::removeSelectedNodes(m_document.map());
        });
        deleteAction->setEnabled(!nodes.empty());

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

    if (event->key() == Qt::Key_QuoteLeft) {
        if (auto* frame = dynamic_cast<MapWindow*>(window())) {
            if (auto* view = frame->currentMapViewBase()) {
                view->showPieMenu();
                event->accept();
                return;
            }
        }
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

    if (auto* targetLayer = dynamic_cast<mdl::LayerNode*>(targetNode)) {
        const auto nodesToMove = collectNodesToMoveToLayer(
            m_document.map(),
            selectedItems(),
            [this](QTreeWidgetItem* item) { return nodeFromItem(item); },
            targetLayer);

        if (!nodesToMove.empty()) {
            event->acceptProposedAction();
            return;
        }
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

    if (auto* targetLayer = dynamic_cast<mdl::LayerNode*>(targetNode)) {
        auto& map = m_document.map();
        auto nodesToMove = collectNodesToMoveToLayer(
            map,
            selectedItems(),
            [this](QTreeWidgetItem* item) { return nodeFromItem(item); },
            targetLayer);

        if (nodesToMove.empty()) {
            event->ignore();
            return;
        }

        auto transaction = mdl::Transaction{map, "Move Objects to Layer " + targetLayer->name()};
        mdl::deselectAll(map);

        auto nodesToAdd = std::map<mdl::Node*, std::vector<mdl::Node*>>{};
        nodesToAdd[targetLayer] = nodesToMove;
        if (!mdl::reparentNodes(map, nodesToAdd)) {
            transaction.cancel();
            event->ignore();
            return;
        }

        mdl::selectNodes(map, nodesToMove);
        transaction.commit();

        scheduleUpdateTree(targetLayer);
        event->acceptProposedAction();
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
