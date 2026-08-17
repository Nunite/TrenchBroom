#include "ui/outliner/OutlinerInspector.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSettings>
#include <QSize>
#include <QSizePolicy>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "mdl/LayerNode.h"
#include "mdl/Map_Layers.h"
#include "ui/BitmapButton.h"
#include "ui/MapDocument.h"
#include "ui/QWidgetUtils.h"
#include "ui/SearchBox.h"
#include "ui/Splitter.h"
#include "ui/ViewUtils.h"
#include "ui/WidgetState.h"
#include "ui/outliner/OutlinerEntityPropertyEditor.h"
#include "ui/outliner/OutlinerTreeWidget.h"

namespace tb::ui
{

OutlinerInspector::OutlinerInspector(MapDocument& document, QWidget* parent)
  : TabBookPage(parent)
  , m_document(document)
{
  if (objectName().isEmpty())
  {
    setObjectName("OutlinerInspector");
  }

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto* topRowWidget = new QWidget{this};
  topRowWidget->setObjectName("OutlinerInspector_Toolbar");
  topRowWidget->setAttribute(Qt::WA_StyledBackground, true);
  topRowWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

  auto* topRow = new QHBoxLayout(topRowWidget);
  topRow->setContentsMargins(8, 5, 8, 5);
  topRow->setSpacing(6);

  m_searchField = createSearchBox(topRowWidget);
  m_searchField->setObjectName("OutlinerInspector_Search");
  topRow->addWidget(m_searchField, 10);

  m_sortBox = new QComboBox(topRowWidget);
  m_sortBox->setObjectName("OutlinerInspector_Sort");
  m_sortBox->setToolTip(tr("Sort outliner"));
  m_sortBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_sortBox->addItem(
    tr("Default"), static_cast<int>(OutlinerTreeWidget::SortMode::Default));
  m_sortBox->addItem(tr("Type"), static_cast<int>(OutlinerTreeWidget::SortMode::Type));
  m_sortBox->addItem(
    tr("File Order"), static_cast<int>(OutlinerTreeWidget::SortMode::FileOrder));
  {
    const auto settings = QSettings{};
    const auto sortMode = settings
                            .value(
                              widgetSettingsPath(this, "SortMode"),
                              static_cast<int>(OutlinerTreeWidget::SortMode::Default))
                            .toInt();
    const auto index = m_sortBox->findData(sortMode);
    m_sortBox->setCurrentIndex(index >= 0 ? index : 0);
  }
  topRow->addWidget(m_sortBox);

  auto* addLayerButton =
    createBitmapButton("Add.svg", tr("Add a new layer"), topRowWidget);
  addLayerButton->setObjectName("OutlinerInspector_AddLayer");
  addLayerButton->setAccessibleName(tr("Add a new layer"));
  addLayerButton->setIconSize(QSize{16, 16});
  addLayerButton->setFixedSize(QSize{28, 28});
  topRow->addWidget(addLayerButton);

  auto* propertiesToggle =
    createBitmapToggleButton("Map_entity.svg", tr("Toggle properties panel"), this);
  propertiesToggle->setObjectName("OutlinerInspector_PropertiesToggle");
  propertiesToggle->setIconSize(QSize{16, 16});
  propertiesToggle->setFixedSize(QSize{28, 28});
  topRow->addWidget(propertiesToggle);

  topRowWidget->setFixedHeight(38);

  layout->addWidget(topRowWidget, 0);

  m_splitter = new Splitter{Qt::Vertical};
  m_splitter->setObjectName("OutlinerInspector_Splitter");

  m_treeWidget = new OutlinerTreeWidget(m_document, m_splitter);
  m_splitter->addWidget(m_treeWidget);

  m_propertyEditor = new OutlinerEntityPropertyEditor{m_document, m_splitter};
  m_splitter->addWidget(m_propertyEditor);

  connect(addLayerButton, &QAbstractButton::clicked, this, [this]() {
    const auto name = queryLayerName(this, "Unnamed");
    if (name.empty())
    {
      return;
    }

    if (auto* layerNode = mdl::createLayer(m_document.map(), name))
    {
      m_searchField->clear();
      m_treeWidget->revealNode(layerNode);
    }
  });

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

  auto* searchTimer = new QTimer{this};
  searchTimer->setInterval(120);
  searchTimer->setSingleShot(true);
  connect(
    m_searchField,
    &QLineEdit::textChanged,
    this,
    [this, searchTimer](const QString& text) {
      if (text.trimmed().isEmpty())
      {
        searchTimer->stop();
        m_treeWidget->setFilterText({});
      }
      else
      {
        searchTimer->start();
      }
    });
  connect(searchTimer, &QTimer::timeout, this, [this]() {
    m_treeWidget->setFilterText(m_searchField->text());
  });
  connect(m_sortBox, &QComboBox::currentIndexChanged, this, [this](int index) {
    const auto modeValue = m_sortBox->itemData(index).toInt();
    m_treeWidget->setSortMode(static_cast<OutlinerTreeWidget::SortMode>(modeValue));
    auto settings = QSettings{};
    settings.setValue(widgetSettingsPath(this, "SortMode"), modeValue);
  });

  m_treeWidget->setSortMode(
    static_cast<OutlinerTreeWidget::SortMode>(m_sortBox->currentData().toInt()));
}

OutlinerInspector::~OutlinerInspector()
{
  if (m_propertyEditor && m_propertyEditor->isVisible())
  {
    saveWidgetState(m_splitter);
  }
}

} // namespace tb::ui
