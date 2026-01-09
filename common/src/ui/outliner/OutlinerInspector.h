#pragma once

#include "ui/TabBook.h"
#include "NotifierConnection.h"

namespace tb::mdl
{
class Map;
struct SelectionChange;
}

namespace tb::ui
{
class GLContextManager;
class OutlinerModel;
}

class QTreeView;
class QItemSelection;
class QLineEdit;

namespace tb::ui
{
class OutlinerInspector : public TabBookPage
{
  Q_OBJECT
private:
  mdl::Map& m_map;
  OutlinerModel* m_model = nullptr;
  QTreeView* m_treeView = nullptr;
  QLineEdit* m_searchField = nullptr;
  NotifierConnection m_notifierConnection;
  bool m_updatingSelection = false;

public:
  OutlinerInspector(mdl::Map& map, GLContextManager& contextManager, QWidget* parent = nullptr);
  ~OutlinerInspector() override;

private:
  void onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
  void onMapSelectionChanged(const mdl::SelectionChange& change);
  void onItemClicked(const QModelIndex& index);
};

} // namespace tb::ui
