//Added by Lws

#pragma once

#include "ui/TabBook.h"

#include <memory>

class QSplitter;

namespace tb::ui
{
class CurveGenInspector;
class GLContextManager;
class MapDocument;

class ToolsPanel : public TabBookPage
{
  Q_OBJECT
private:
  QSplitter* m_splitter = nullptr;
  CurveGenInspector* m_curveGenInspector = nullptr;

public:
  ToolsPanel(
    std::weak_ptr<MapDocument> document,
    GLContextManager& contextManager,
    QWidget* parent = nullptr);
  ~ToolsPanel() override;

private:
  void createGui(std::weak_ptr<MapDocument> document, GLContextManager& contextManager);
  QWidget* createCurveGenInspector(
    QWidget* parent, 
    std::weak_ptr<MapDocument> document,
    GLContextManager& contextManager);
};

} // namespace tb::ui 