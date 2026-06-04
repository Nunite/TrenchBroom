#include "ui/outliner/OutlinerModel.h"

#include "ui/ImageUtils.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Node.h"
#include "mdl/WorldNode.h"
#include "mdl/EntityNode.h"
#include "mdl/GroupNode.h"
#include "mdl/BrushNode.h"
#include "mdl/PatchNode.h"

#include <QIcon>
#include <QMimeData>
#include <QDataStream>
#include <QIODevice>
#include <QSet>

#include <algorithm>
#include <map>
#include <vector>

namespace tb::ui
{

OutlinerModel::OutlinerModel(mdl::Map& map, QObject* parent) :
    QAbstractItemModel(parent),
    m_map(map)
{
    m_groupIcon = loadSVGIcon("Folder.svg");
    m_entityIcon = loadSVGIcon("NoTool.svg");
    m_brushIcon = loadSVGIcon("BrushTool.svg");
    m_visibleIcon = loadSVGIcon("Hidden_off.svg");
    m_hiddenIcon = loadSVGIcon("Hidden_on.svg");
    m_lockedIcon = loadSVGIcon("Lock_on.svg");
    m_unlockedIcon = loadSVGIcon("Lock_off.svg");

    m_notifierConnection += m_map.nodesWereAddedNotifier.connect([this](const std::vector<mdl::Node*>& nodes) { onNodesWereAdded(nodes); });
    m_notifierConnection += m_map.nodesWillBeRemovedNotifier.connect([this](const std::vector<mdl::Node*>& nodes) { onNodesWillBeRemoved(nodes); });
    m_notifierConnection += m_map.nodesWereRemovedNotifier.connect([this](const std::vector<mdl::Node*>& nodes) { onNodesWereRemoved(nodes); });
    m_notifierConnection += m_map.nodesDidChangeNotifier.connect([this](const std::vector<mdl::Node*>& nodes) { onNodeDidChange(nodes); });
    m_notifierConnection += m_map.nodeVisibilityDidChangeNotifier.connect([this](const std::vector<mdl::Node*>& nodes) { onNodeVisibilityDidChange(nodes); });
    m_notifierConnection += m_map.nodeLockingDidChangeNotifier.connect([this](const std::vector<mdl::Node*>& nodes) { onNodeLockingDidChange(nodes); });
}

OutlinerModel::~OutlinerModel() = default;

QModelIndex OutlinerModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent))
        return {};

    mdl::Node* parentNode = nodeFromIndex(parent);
    if (!parentNode)
        return {};

    const auto& children = parentNode->children();
    if (row >= 0 && static_cast<size_t>(row) < children.size())
    {
        return createIndex(row, column, children[static_cast<size_t>(row)]);
    }

    return {};
}

QModelIndex OutlinerModel::parent(const QModelIndex& child) const
{
    if (!child.isValid())
        return {};

    mdl::Node* childNode = static_cast<mdl::Node*>(child.internalPointer());
    mdl::Node* parentNode = childNode->parent();

    if (parentNode == &m_map.worldNode())
        return {}; // Root parent is invalid index

    if (!parentNode)
        return {};

    // Find row of parentNode in its own parent
    mdl::Node* grandParent = parentNode->parent();
    if (grandParent)
    {
        const auto& siblings = grandParent->children();
        auto it = std::find(siblings.begin(), siblings.end(), parentNode);
        if (it != siblings.end())
        {
            int row = static_cast<int>(std::distance(siblings.begin(), it));
            return createIndex(row, 0, parentNode);
        }
    }

    return {};
}

int OutlinerModel::rowCount(const QModelIndex& parent) const
{
    if (parent.column() > 0)
        return 0;

    mdl::Node* parentNode = nodeFromIndex(parent);
    return parentNode ? static_cast<int>(parentNode->childCount()) : 0;
}

int OutlinerModel::columnCount(const QModelIndex& parent) const
{
    (void)parent;
    return 3; 
}

QVariant OutlinerModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};

    mdl::Node* node = static_cast<mdl::Node*>(index.internalPointer());

    if (index.column() == 0)
    {
        if (role == Qt::DisplayRole)
        {
            // Use classname for entities, or name if set
            if (auto* entityNode = dynamic_cast<mdl::EntityNode*>(node))
            {
                return QString::fromStdString(entityNode->entity().classname());
            }
            else if (auto* groupNode = dynamic_cast<mdl::GroupNode*>(node))
            {
                 if (groupNode->name().empty())
                     return QString("Group");
                 return QString::fromStdString(groupNode->name());
            }
            else if (auto* brushNode = dynamic_cast<mdl::BrushNode*>(node))
            {
                (void)brushNode;
                return QString("Brush");
            }
            else if (auto* patchNode = dynamic_cast<mdl::PatchNode*>(node))
            {
                (void)patchNode;
                return QString("Patch");
            }
            
            return QString::fromStdString(node->name());
        }
        else if (role == Qt::DecorationRole)
        {
            if (dynamic_cast<mdl::GroupNode*>(node))
            {
                return m_groupIcon;
            }
            else if (dynamic_cast<mdl::EntityNode*>(node))
            {
                 return m_entityIcon; 
            }
            else if (dynamic_cast<mdl::BrushNode*>(node))
            {
                return m_brushIcon;
            }
        }
    }
    else if (index.column() == 1) // Visibility
    {
        if (role == Qt::DecorationRole)
        {
            // Check visibility state
            // If hidden, show Hidden_on (Crossed Eye)
            // If visible, show Hidden_off (Open Eye)
            // Note: We might want to distinguish between Explicitly Hidden and Inherited Hidden?
            // For now simple toggle.
            if (node->visibilityState() == mdl::VisibilityState::Hidden)
                return m_hiddenIcon;
            else
                return m_visibleIcon;
        }
    }
    else if (index.column() == 2) // Lock
    {
        if (role == Qt::DecorationRole)
        {
            if (node->lockState() == mdl::LockState::Locked)
                return m_lockedIcon;
            else
                return m_unlockedIcon;
        }
    }

    return {};
}

QVariant OutlinerModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        if (section == 0) return QString("Name");
        if (section == 1) return QString("Vis");
        if (section == 2) return QString("Lock");
    }
    return {};
}

Qt::ItemFlags OutlinerModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags defaultFlags = QAbstractItemModel::flags(index);
    if (index.isValid())
    {
        return defaultFlags | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    }
    return defaultFlags | Qt::ItemIsDropEnabled;
}

QStringList OutlinerModel::mimeTypes() const
{
    return {"application/x-trenchbroom-nodes"};
}

QMimeData* OutlinerModel::mimeData(const QModelIndexList& indexes) const
{
    QMimeData* mimeData = new QMimeData;
    QByteArray encodedData;
    QDataStream stream(&encodedData, QIODevice::WriteOnly);

    auto uniqueNodes = QSet<mdl::Node*>{};
    for (const QModelIndex& index : indexes)
    {
        if (index.isValid())
        {
            if (mdl::Node* node = nodeFromIndex(index))
                uniqueNodes.insert(node);
        }
    }

    for (auto* node : uniqueNodes)
    {
        stream << (quint64)node;
    }

    mimeData->setData("application/x-trenchbroom-nodes", encodedData);
    return mimeData;
}

bool OutlinerModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent)
{
    (void)row;
    (void)column;
    if (action == Qt::IgnoreAction)
        return true;

    if (!data->hasFormat("application/x-trenchbroom-nodes"))
        return false;

    QByteArray encodedData = data->data("application/x-trenchbroom-nodes");
    QDataStream stream(&encodedData, QIODevice::ReadOnly);
    
    std::vector<mdl::Node*> nodes;
    while (!stream.atEnd()) {
        quint64 ptrVal;
        stream >> ptrVal;
        nodes.push_back((mdl::Node*)ptrVal);
    }

    if (nodes.empty()) return false;

    mdl::Node* parentNode = nodeFromIndex(parent); 
    // If parent is invalid, it means dropping on root (World).
    if (!parentNode) parentNode = &m_map.worldNode();
    
    // Validate reparenting
    // We cannot reparent a node to itself or its descendant.
    for (auto* node : nodes) {
        if (node == parentNode) return false;
        if (parentNode->isDescendantOf(*node)) return false;
    }

    std::map<mdl::Node*, std::vector<mdl::Node*>> reparentMap;
    reparentMap[parentNode] = nodes;
    
    return mdl::reparentNodes(m_map, reparentMap);
}

Qt::DropActions OutlinerModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

mdl::Node* OutlinerModel::nodeFromIndex(const QModelIndex& index) const
{
    if (index.isValid())
    {
        return static_cast<mdl::Node*>(index.internalPointer());
    }
    return &m_map.worldNode();
}

QModelIndex OutlinerModel::indexFromNode(const mdl::Node* node) const
{
    if (!node || node == &m_map.worldNode())
        return {};

    mdl::Node* parentNode = node->parent();
    if (!parentNode) return {};

    const auto& siblings = parentNode->children();
    auto it = std::find(siblings.begin(), siblings.end(), node);
    if (it != siblings.end())
    {
        int row = static_cast<int>(std::distance(siblings.begin(), it));
        return createIndex(row, 0, const_cast<mdl::Node*>(node));
    }
    return {};
}

void OutlinerModel::onNodesWereAdded(const std::vector<mdl::Node*>& nodes)
{
    // Group nodes by parent to batch updates
    // For simplicity in this first pass, we emit begin/endInsertRows for each node
    // A better approach would be to group by parent and contiguous ranges
    
    for (auto* node : nodes)
    {
        if (!node) continue;
        mdl::Node* parent = node->parent();
        if (!parent) continue;

        QModelIndex parentIndex = indexFromNode(parent);
        
        // Find the index of the new node in its parent
        const auto& siblings = parent->children();
        auto it = std::find(siblings.begin(), siblings.end(), node);
        if (it != siblings.end())
        {
            int row = static_cast<int>(std::distance(siblings.begin(), it));
            beginInsertRows(parentIndex, row, row);
            endInsertRows();
        }
    }
}

void OutlinerModel::onNodesWillBeRemoved(const std::vector<mdl::Node*>& nodes)
{
    for (auto* node : nodes)
    {
        if (!node) continue;
        mdl::Node* parent = node->parent();
        if (!parent) continue;
        
        // We need to find the row BEFORE removal
        QModelIndex parentIndex = indexFromNode(parent);
        const auto& siblings = parent->children();
        auto it = std::find(siblings.begin(), siblings.end(), node);
        if (it != siblings.end())
        {
            int row = static_cast<int>(std::distance(siblings.begin(), it));
            beginRemoveRows(parentIndex, row, row);
        }
    }
}

void OutlinerModel::onNodesWereRemoved(const std::vector<mdl::Node*>& nodes)
{
    // endRemoveRows must match beginRemoveRows calls order-wise or count-wise
    // Since we did one-by-one in onNodesWillBeRemoved, we should do one-by-one here too?
    // Actually, Qt requires begin/end to be nested or sequential. 
    // The notification split (WillBeRemoved / WereRemoved) is tricky for multiple nodes if not processed in same order.
    // However, TrenchBroom usually batches these.
    
    // For now, let's assume one batch. But we called beginRemoveRows N times.
    // We need to call endRemoveRows N times.
    
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        endRemoveRows();
    }
}

void OutlinerModel::onNodeDidChange(const std::vector<mdl::Node*>& nodes)
{
    for (auto* node : nodes)
    {
        QModelIndex index = indexFromNode(node);
        if (index.isValid())
        {
            emit dataChanged(index, index);
        }
    }
}

void OutlinerModel::onNodeVisibilityDidChange(const std::vector<mdl::Node*>& nodes)
{
    for (auto* node : nodes)
    {
        QModelIndex index = indexFromNode(node);
        if (index.isValid())
        {
            emit dataChanged(index.sibling(index.row(), 1), index.sibling(index.row(), 1));
        }
    }
}

void OutlinerModel::onNodeLockingDidChange(const std::vector<mdl::Node*>& nodes)
{
    for (auto* node : nodes)
    {
        QModelIndex index = indexFromNode(node);
        if (index.isValid())
        {
            emit dataChanged(index.sibling(index.row(), 2), index.sibling(index.row(), 2));
        }
    }
}

} // namespace tb::ui
