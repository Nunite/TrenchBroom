#pragma once

#include "ui/SmartPropertyEditor.h"

class QPushButton;
class QWidget;

namespace tb::mdl
{
class Map;
}

namespace tb::ui
{

class SmartModelEditor : public SmartPropertyEditor
{
  Q_OBJECT
private:
  QPushButton* m_browseButton = nullptr;

public:
  explicit SmartModelEditor(mdl::Map& map, QWidget* parent = nullptr);

private:
  void createGui();
  void doUpdateVisual(const std::vector<mdl::EntityNodeBase*>& nodes) override;

  void browseFile();
};

} // namespace tb::ui
