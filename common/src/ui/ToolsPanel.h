//Added by Lws

#pragma once

#include "ui/TabBook.h"
#include "ui/Selection.h" // 新增

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

public slots:
    void updateCurveGenSelectionContent(const Selection& selection); // 只声明，不实现
};

} // namespace tb::ui