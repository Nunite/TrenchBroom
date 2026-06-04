#include "ui/outliner/OutlinerInspector.h"
#include "ui/outliner/OutlinerTreeWidget.h"
#include "ui/outliner/OutlinerEntityPropertyEditor.h"
#include "ui/BitmapButton.h"
#include "ui/MapDocument.h"
#include "ui/QWidgetUtils.h"
#include "ui/SearchBox.h"
#include "ui/Splitter.h"
#include "ui/WidgetState.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QAbstractButton>
#include <QLineEdit>
#include <QComboBox>
#include <QToolButton>
#include <QSettings>
#include <QSizePolicy>
#include <QSize>
#include <QWidget>

namespace tb::ui
{

OutlinerInspector::OutlinerInspector(MapDocument& document, QWidget* parent) :
    TabBookPage(parent),
    m_document(document)
{
    if (objectName().isEmpty())
    {
        setObjectName("OutlinerInspector");
    }

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* topRowWidget = new QWidget{this};
    topRowWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto* topRow = new QHBoxLayout(topRowWidget);
    topRow->setContentsMargins(0, 0, 0, 0);

    m_searchField = createSearchBox();
    topRow->addWidget(m_searchField, 10);

    m_sortBox = new QComboBox(this);
    m_sortBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_sortBox->addItem(tr("Default"), static_cast<int>(OutlinerTreeWidget::SortMode::Default));
    m_sortBox->addItem(tr("Type"), static_cast<int>(OutlinerTreeWidget::SortMode::Type));
    m_sortBox->addItem(tr("File Order"), static_cast<int>(OutlinerTreeWidget::SortMode::FileOrder));
    topRow->addWidget(m_sortBox);

    auto* propertiesToggle = createBitmapToggleButton(
        "Map_entity.svg",
        tr("Toggle properties panel"),
        this);
    propertiesToggle->setObjectName("toolButton_withBorder");
    propertiesToggle->setIconSize(QSize{16, 16});
    propertiesToggle->setFixedSize(QSize{24, 24});
    topRow->addWidget(propertiesToggle);

    const auto rowHeight = std::max(
        {m_searchField->sizeHint().height(),
         m_sortBox->sizeHint().height(),
         propertiesToggle->sizeHint().height()});
    topRowWidget->setMinimumHeight(rowHeight);
    topRowWidget->setMaximumHeight(rowHeight);

    layout->addWidget(topRowWidget, 0);

    m_splitter = new Splitter{Qt::Vertical};
    m_splitter->setObjectName("OutlinerInspector_Splitter");

    m_treeWidget = new OutlinerTreeWidget(m_document, m_splitter);
    m_splitter->addWidget(m_treeWidget);

    m_propertyEditor = new OutlinerEntityPropertyEditor{m_document, m_splitter};
    m_splitter->addWidget(m_propertyEditor);

    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);

    layout->addWidget(m_splitter, 1);

    restoreWidgetState(m_splitter);

    {
        auto settings = QSettings{};
        const auto visiblePath = widgetSettingsPath(this, "PropertiesVisible");
        const auto propertiesVisible = settings.value(visiblePath, false).toBool();

        propertiesToggle->setChecked(propertiesVisible);
        m_propertyEditor->setVisible(propertiesVisible);
        if (!propertiesVisible)
        {
            m_splitter->setSizes({1, 0});
        }
    }

    connect(propertiesToggle, &QAbstractButton::clicked, this, [this](const bool checked) {
        auto settings = QSettings{};
        const auto visiblePath = widgetSettingsPath(this, "PropertiesVisible");
        settings.setValue(visiblePath, checked);

        if (checked)
        {
            m_propertyEditor->setVisible(true);
            restoreWidgetState(m_splitter);
            const auto sizes = m_splitter->sizes();
            if (sizes.size() >= 2 && sizes.at(1) == 0)
            {
                m_splitter->setSizes({3, 2});
            }
        }
        else
        {
            saveWidgetState(m_splitter);
            m_propertyEditor->setVisible(false);
            m_splitter->setSizes({1, 0});
        }
    });

    connect(m_searchField, &QLineEdit::textChanged, m_treeWidget, &OutlinerTreeWidget::setFilterText);
    connect(m_sortBox, &QComboBox::currentIndexChanged, this, [this](int index) {
        const auto modeValue = m_sortBox->itemData(index).toInt();
        m_treeWidget->setSortMode(static_cast<OutlinerTreeWidget::SortMode>(modeValue));
    });
}

OutlinerInspector::~OutlinerInspector()
{
    if (m_propertyEditor && m_propertyEditor->isVisible())
    {
        saveWidgetState(m_splitter);
    }
}

} // namespace tb::ui
