#pragma once

#include <QAbstractItemModel>

#include "NotifierConnection.h"

namespace tb::mdl
{
class Map;
class Node;
}

namespace tb::ui
{

class OutlinerModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    explicit OutlinerModel(mdl::Map& map, QObject* parent = nullptr);
    ~OutlinerModel() override;

    // QAbstractItemModel interface
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Drag and Drop
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) override;
    Qt::DropActions supportedDropActions() const override;

    mdl::Node* nodeFromIndex(const QModelIndex& index) const;
    QModelIndex indexFromNode(const mdl::Node* node) const;

private:
    mdl::Map& m_map;
    NotifierConnection m_notifierConnection;

    void onNodesWereAdded(const std::vector<mdl::Node*>& nodes);
    void onNodesWillBeRemoved(const std::vector<mdl::Node*>& nodes);
    void onNodesWereRemoved(const std::vector<mdl::Node*>& nodes);
    void onNodeDidChange(const std::vector<mdl::Node*>& nodes);
    void onNodeVisibilityDidChange(const std::vector<mdl::Node*>& nodes);
    void onNodeLockingDidChange(const std::vector<mdl::Node*>& nodes);
};

} // namespace tb::ui
