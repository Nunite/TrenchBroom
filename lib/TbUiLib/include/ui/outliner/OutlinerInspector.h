#pragma once

#include "ui/TabBook.h"
#include "base/NotifierConnection.h"
#include <memory>

namespace tb::mdl
{
class Map;
struct SelectionChange;
}

namespace tb::ui
{
class OutlinerTreeWidget;
class MapDocument;
class OutlinerEntityPropertyEditor;
}

class QLineEdit;
class QComboBox;
class QSplitter;

namespace tb::ui
{
class OutlinerInspector : public TabBookPage
{
  Q_OBJECT
private:
  MapDocument& m_document; 
  
  QSplitter* m_splitter = nullptr;
  OutlinerTreeWidget* m_treeWidget = nullptr;
  OutlinerEntityPropertyEditor* m_propertyEditor = nullptr;
  QLineEdit* m_searchField = nullptr;
  QComboBox* m_sortBox = nullptr;

public:
  explicit OutlinerInspector(MapDocument& document, QWidget* parent = nullptr);
  ~OutlinerInspector() override;
};

} // namespace tb::ui
