#pragma once

#include "ui/SmartPropertyEditor.h"

class QPushButton;
class QWidget;

namespace tb::mdl
{
class EntityNodeBase;
}

namespace tb::ui
{
class MapDocument;

class SmartModelEditor : public SmartPropertyEditor
{
  Q_OBJECT
private:
  QPushButton* m_browseButton = nullptr;

public:
  explicit SmartModelEditor(MapDocument& document, QWidget* parent = nullptr);

private:
  void createGui();
  void doUpdateVisual(const std::vector<mdl::EntityNodeBase*>& nodes) override;

  void browseFile();
};

} // namespace tb::ui
