#include "OutlinerInspector.h"
#include "OutlinerModel.h"

#include <QVBoxLayout>
#include <QTreeView>
#include <QSortFilterProxyModel>
#include <QLineEdit>

#include "mdl/Map.h"
#include "mdl/Map_NodeVisibility.h"
#include "mdl/Map_NodeLocking.h"
#include "mdl/Node.h"
#include "mdl/BrushNode.h"
#include "mdl/PatchNode.h"
#include "mdl/Selection.h"
#include "mdl/SelectionChange.h"
#include "mdl/SelectionCommand.h"

namespace tb::ui
{

namespace 
{
class OutlinerFilterProxy : public QSortFilterProxyModel
{
public:
    explicit OutlinerFilterProxy(QObject* parent = nullptr) : QSortFilterProxyModel(parent) 
    {
        setFilterCaseSensitivity(Qt::CaseInsensitive);
        setRecursiveFilteringEnabled(true);
    }
};
}

OutlinerInspector::OutlinerInspector(mdl::Map& map, GLContextManager& contextManager, QWidget* parent) :
    TabBookPage(parent),
    m_map(map)
{
    (void)contextManager;

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_searchField = new QLineEdit(this);
    m_searchField->setPlaceholderText("Search...");
    m_searchField->setClearButtonEnabled(true);
    layout->addWidget(m_searchField);

    m_model = new OutlinerModel(map, this);
    
    // Use a proxy model
    // auto* proxy = new OutlinerFilterProxy(this);
    // proxy->setSourceModel(m_model);
    
    // connect(m_searchField, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);

    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_model);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeView->setHeaderHidden(true);
    m_treeView->setDragEnabled(true);
    m_treeView->setAcceptDrops(true);
    m_treeView->setDropIndicatorShown(true);
    m_treeView->setDragDropMode(QAbstractItemView::DragDrop);

    layout->addWidget(m_treeView);

    connect(m_treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &OutlinerInspector::onSelectionChanged);
    connect(m_treeView, &QTreeView::clicked, this, &OutlinerInspector::onItemClicked);

    m_treeView->setColumnWidth(1, 24);
    m_treeView->setColumnWidth(2, 24);

    m_notifierConnection += m_map.selectionDidChangeNotifier.connect([this](const mdl::SelectionChange& change) { onMapSelectionChanged(change); });
}

OutlinerInspector::~OutlinerInspector() = default;

void OutlinerInspector::onItemClicked(const QModelIndex& index)
{
    if (!index.isValid()) return;
    
    // auto* proxy = qobject_cast<QSortFilterProxyModel*>(m_treeView->model());
    // QModelIndex sourceIndex = proxy ? proxy->mapToSource(index) : index;
    QModelIndex sourceIndex = index;
    
    mdl::Node* node = m_model->nodeFromIndex(sourceIndex);
    if (!node) return;

    if (index.column() == 1) // Visibility
    {
        if (node->visibilityState() == mdl::VisibilityState::Hidden)
            mdl::showNodes(m_map, {node});
        else
            mdl::hideNodes(m_map, {node});
    }
    else if (index.column() == 2) // Lock
    {
        if (node->lockState() == mdl::LockState::Locked)
            mdl::unlockNodes(m_map, {node});
        else
            mdl::lockNodes(m_map, {node});
    }
}

void OutlinerInspector::onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
{
    if (m_updatingSelection) return;

    std::vector<mdl::Node*> nodesToSelect;
    std::vector<mdl::Node*> nodesToDeselect;

    // Helper to get nodes from selection
    auto getNodes = [&](const QItemSelection& selection, std::vector<mdl::Node*>& nodes) {
        for (const auto& index : selection.indexes())
        {
            // QTreeView select rows, so we might get multiple columns per row if we had them.
            // But we only have column 0 for now.
            // Also need to map through proxy
            // auto* proxy = qobject_cast<QSortFilterProxyModel*>(m_treeView->model());
            // QModelIndex sourceIndex = proxy ? proxy->mapToSource(index) : index;
            QModelIndex sourceIndex = index;
            
            if (mdl::Node* node = m_model->nodeFromIndex(sourceIndex))
            {
                nodes.push_back(node);
            }
        }
    };

    getNodes(selected, nodesToSelect);
    getNodes(deselected, nodesToDeselect);

    // To properly support multi-selection and avoid clearing existing selection unless intended,
    // we need to be careful. However, QTreeView extended selection usually handles the logic
    // of what is "selected" and "deselected" in a batch.
    
    // But TrenchBroom's SelectionCommand API is command based.
    // If we simply want to sync the UI selection to the Map selection:
    
    // Strategy: 
    // 1. If we are deselecting nodes, execute a deselect command.
    // 2. If we are selecting nodes, execute a select command.
    // Note: SelectionCommand::select adds to selection if not clearing? 
    // Actually SelectionCommand::select replaces selection? No, let's check.
    // Usually "select" means "set selection". "add" means add.
    
    // Looking at SelectionCommand::select(nodes):
    // It seems it might be "select these nodes".
    
    // Let's look at SelectionCommand.h again.
    // It has select, deselect, selectAll, etc.
    // It doesn't explicitly say "Add". 
    
    // However, for typical Outliner behavior:
    // Click -> Select only this (clear others)
    // Ctrl+Click -> Toggle
    // Shift+Click -> Range
    
    // QTreeView handles the UI logic (Ctrl/Shift). The `selected` and `deselected` arguments tell us the delta.
    
    // If we want to synchronize perfectly:
    // We should probably construct a new Selection object representing the entire state of the TreeView
    // and set it on the map.
    // OR, we use the deltas to issue Add/Remove commands.
    
    // Let's try using deltas.
    
    if (!nodesToDeselect.empty())
    {
        m_map.execute(mdl::SelectionCommand::deselect(nodesToDeselect));
    }
    
    if (!nodesToSelect.empty())
    {
        m_map.execute(mdl::SelectionCommand::select(nodesToSelect));
    }
}

void OutlinerInspector::onMapSelectionChanged(const mdl::SelectionChange& change)
{
    (void)change;
    m_updatingSelection = true;
    
    QItemSelectionModel* selModel = m_treeView->selectionModel();
    // auto* proxy = qobject_cast<QSortFilterProxyModel*>(m_treeView->model());
    
    // We need to reflect Map selection to UI
    // The Change object tells us what changed.
    
    // Helper to map node to index
    auto selectNode = [&](mdl::Node* node, QItemSelectionModel::SelectionFlags flag) {
        if (!node) return;
        QModelIndex sourceIndex = m_model->indexFromNode(node);
        if (sourceIndex.isValid())
        {
            // QModelIndex proxyIndex = proxy ? proxy->mapFromSource(sourceIndex) : sourceIndex;
            QModelIndex proxyIndex = sourceIndex;
            if (proxyIndex.isValid())
            {
                selModel->select(proxyIndex, flag);
                // Also scroll to it if it's a single selection or the last one
                if (flag & QItemSelectionModel::Current)
                    m_treeView->scrollTo(proxyIndex);
            }
        }
    };

    // If change has specific added/removed nodes, use them.
    // mdl::SelectionChange usually has `added` and `removed`.
    // Let's check SelectionChange definition if we could.
    // Assuming it has vectors or similar.
    
    // If we can't see SelectionChange definition easily, let's look at what we read.
    // We read Map.h but not SelectionChange.h full content.
    // But we can guess or read it.
    
    // Let's blindly assume it has `nodes` and `type`? Or `addedNodes`, `removedNodes`?
    // Actually, let's just sync the WHOLE selection from Map if we are unsure of the delta format.
    // m_map.selection().nodes() gives all selected nodes.
    
    // Safest and easiest for V1: Clear UI selection and re-select everything from Map.
    // This handles all cases (Set, Add, Remove).
    
    selModel->clearSelection();
    
    const auto& selection = m_map.selection();
    for (auto* node : selection.nodes)
    {
        selectNode(node, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
    
    m_updatingSelection = false;
}

} // namespace tb::ui
