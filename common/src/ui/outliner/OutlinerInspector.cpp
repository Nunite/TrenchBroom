#include "OutlinerInspector.h"
#include "OutlinerTreeWidget.h"
#include "ui/EntityPropertyEditor.h"
#include "ui/MapDocument.h"
#include "ui/QtUtils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QSplitter>

namespace tb::ui
{

OutlinerInspector::OutlinerInspector(MapDocument& document, GLContextManager& contextManager, QWidget* parent) :
    TabBookPage(parent),
    m_document(document)
{
    (void)contextManager;

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);

    m_searchField = createSearchBox();
    topRow->addWidget(m_searchField, 1);

    m_sortBox = new QComboBox(this);
    m_sortBox->addItem(tr("Default"), static_cast<int>(OutlinerTreeWidget::SortMode::Default));
    m_sortBox->addItem(tr("Name"), static_cast<int>(OutlinerTreeWidget::SortMode::NameAsc));
    m_sortBox->addItem(tr("Type"), static_cast<int>(OutlinerTreeWidget::SortMode::Type));
    topRow->addWidget(m_sortBox);

    layout->addLayout(topRow);

    m_splitter = new QSplitter(Qt::Vertical, this);

    m_treeWidget = new OutlinerTreeWidget(m_document, m_splitter);
    m_splitter->addWidget(m_treeWidget);

    m_propertyEditor = new EntityPropertyEditor{m_document.map(), m_splitter};
    m_splitter->addWidget(m_propertyEditor);

    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);

    layout->addWidget(m_splitter, 1);

    connect(m_searchField, &QLineEdit::textChanged, m_treeWidget, &OutlinerTreeWidget::setFilterText);
    connect(m_sortBox, &QComboBox::currentIndexChanged, this, [this](int index) {
        const auto modeValue = m_sortBox->itemData(index).toInt();
        m_treeWidget->setSortMode(static_cast<OutlinerTreeWidget::SortMode>(modeValue));
    });
}

OutlinerInspector::~OutlinerInspector() = default;

} // namespace tb::ui
