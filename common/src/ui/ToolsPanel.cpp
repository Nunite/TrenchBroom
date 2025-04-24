//Added by Lws

#include "ToolsPanel.h"

#include <QVBoxLayout>

#include "ui/CurveGenInspector.h"
#include "ui/MapDocument.h"
#include "ui/QtUtils.h"
#include "ui/Splitter.h"
#include "ui/SwitchableTitledPanel.h"

namespace tb::ui
{

ToolsPanel::ToolsPanel(
  std::weak_ptr<MapDocument> document, GLContextManager& contextManager, QWidget* parent)
  : TabBookPage{parent}
{
  createGui(std::move(document), contextManager);
}

ToolsPanel::~ToolsPanel()
{
  saveWindowState(m_splitter);
}

void ToolsPanel::createGui(
  std::weak_ptr<MapDocument> document, GLContextManager& contextManager)
{
  m_splitter = new Splitter{Qt::Vertical};
  m_splitter->setObjectName("ToolsPanel_Splitter");

  m_splitter->addWidget(createCurveGenInspector(m_splitter, document, contextManager));

  // when the window resizes, keep the tool panels size constant
  m_splitter->setStretchFactor(0, 0);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(m_splitter, 1);
  setLayout(layout);

  restoreWindowState(m_splitter);
}

QWidget* ToolsPanel::createCurveGenInspector(
  QWidget* parent, std::weak_ptr<MapDocument> document, GLContextManager& contextManager)
{
  auto* panel = new SwitchableTitledPanel{
    tr("Curve Generator"), {{tr("Curve Gen")}}, parent};

  m_curveGenInspector = new CurveGenInspector{document, contextManager};

  auto* curveGenLayout = new QVBoxLayout{};
  curveGenLayout->setContentsMargins(0, 0, 0, 0);
  curveGenLayout->addWidget(m_curveGenInspector, 1);
  panel->getPanel(0)->setLayout(curveGenLayout);

  return panel;
}

} // namespace tb::ui 