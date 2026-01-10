#include "OutlinerInspector.h"
#include "OutlinerTreeWidget.h"
#include "ui/MapDocument.h"

#include <QVBoxLayout>
#include <QLineEdit>

namespace tb::ui
{

OutlinerInspector::OutlinerInspector(MapDocument& document, GLContextManager& contextManager, QWidget* parent) :
    TabBookPage(parent),
    m_document(document)
{
    (void)contextManager;

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_searchField = new QLineEdit(this);
    m_searchField->setPlaceholderText("Search...");
    m_searchField->setClearButtonEnabled(true);
    layout->addWidget(m_searchField);

    m_treeWidget = new OutlinerTreeWidget(m_document, this);
    layout->addWidget(m_treeWidget);

    connect(m_searchField, &QLineEdit::textChanged, m_treeWidget, &OutlinerTreeWidget::setFilterText);
}

OutlinerInspector::~OutlinerInspector() = default;

} // namespace tb::ui
