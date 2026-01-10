#pragma once

#include "ui/TabBook.h"
#include "NotifierConnection.h"
#include <memory>

namespace tb::mdl
{
class Map;
struct SelectionChange;
}

namespace tb::ui
{
class GLContextManager;
class OutlinerTreeWidget;
class MapDocument;
}

class QLineEdit;
class QComboBox;

namespace tb::ui
{
class OutlinerInspector : public TabBookPage
{
  Q_OBJECT
private:
  MapDocument& m_document; 
  
  OutlinerTreeWidget* m_treeWidget = nullptr;
  QLineEdit* m_searchField = nullptr;
  QComboBox* m_sortBox = nullptr;

public:
  OutlinerInspector(MapDocument& document, GLContextManager& contextManager, QWidget* parent = nullptr);
  ~OutlinerInspector() override;
};

} // namespace tb::ui
