/*
 Copyright (C) 2010 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "MaterialBrowser.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QtGlobal>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "mdl/Material.h"
#include "mdl/MaterialManager.h"
#include "ui/MapDocument.h"
#include "ui/MaterialBrowserView.h"
#include "ui/QtUtils.h"
#include "ui/SliderWithLabel.h"
#include "ui/ViewConstants.h"

#include "kdl/memory_utils.h"

// for use in QVariant
Q_DECLARE_METATYPE(tb::ui::MaterialSortOrder)

namespace tb::ui
{

MaterialBrowser::MaterialBrowser(
  std::weak_ptr<MapDocument> document, GLContextManager& contextManager, QWidget* parent)
  : QWidget{parent}
  , m_document{std::move(document)}
{
  createGui(contextManager);
  bindEvents();
  connectObservers();
  reload();
}

const mdl::Material* MaterialBrowser::selectedMaterial() const
{
  return m_view->selectedMaterial();
}

void MaterialBrowser::setSelectedMaterial(const mdl::Material* selectedMaterial)
{
  m_view->setSelectedMaterial(selectedMaterial);
}

void MaterialBrowser::revealMaterial(const mdl::Material* material)
{
  setFilterText("");
  m_view->revealMaterial(material);
}

void MaterialBrowser::setSortOrder(const MaterialSortOrder sortOrder)
{
  m_view->setSortOrder(sortOrder);
  switch (sortOrder)
  {
  case MaterialSortOrder::Name:
    m_sortOrderChoice->setCurrentIndex(0);
    break;
  case MaterialSortOrder::Usage:
    m_sortOrderChoice->setCurrentIndex(1);
    break;
    switchDefault();
  }
}

void MaterialBrowser::setGroup(const bool group)
{
  m_view->setGroup(group);
  m_groupButton->setChecked(group);
}

void MaterialBrowser::setHideUnused(const bool hideUnused)
{
  m_view->setHideUnused(hideUnused);
  m_usedButton->setChecked(hideUnused);
}

void MaterialBrowser::setFilterText(const std::string& filterText)
{
  m_view->setFilterText(filterText);
  m_filterBox->setText(QString::fromStdString(filterText));
}

/**
 * See EntityBrowser::createGui
 */
void MaterialBrowser::createGui(GLContextManager& contextManager)
{
  auto* browserPanel = new QWidget{};
  m_scrollBar = new QScrollBar{Qt::Vertical};

  auto document = kdl::mem_lock(m_document);
  m_view = new MaterialBrowserView{m_scrollBar, contextManager, document};

  auto* browserPanelSizer = new QHBoxLayout{};
  browserPanelSizer->setContentsMargins(0, 0, 0, 0);
  browserPanelSizer->setSpacing(0);
  browserPanelSizer->addWidget(m_view, 1);
  browserPanelSizer->addWidget(m_scrollBar, 0);
  browserPanel->setLayout(browserPanelSizer);

  m_sortOrderChoice = new QComboBox{};
  m_sortOrderChoice->addItem(tr("Name"), QVariant::fromValue(MaterialSortOrder::Name));
  m_sortOrderChoice->addItem(tr("Usage"), QVariant::fromValue(MaterialSortOrder::Usage));
  m_sortOrderChoice->setCurrentIndex(0);
  m_sortOrderChoice->setToolTip(tr("Select ordering criterion"));
  connect(
    m_sortOrderChoice, QOverload<int>::of(&QComboBox::activated), this, [&](int index) {
      auto sortOrder =
        static_cast<MaterialSortOrder>(m_sortOrderChoice->itemData(index).toInt());
      m_view->setSortOrder(sortOrder);
    });

  m_groupButton = new QPushButton{tr("Group")};
  m_groupButton->setToolTip(tr("Group materials by material collection"));
  m_groupButton->setCheckable(true);
  connect(m_groupButton, &QAbstractButton::clicked, this, [&]() {
    m_view->setGroup(m_groupButton->isChecked());
  });

  m_usedButton = new QPushButton{tr("Used")};
  m_usedButton->setToolTip(tr("Only show materials currently in use"));
  m_usedButton->setCheckable(true);
  connect(m_usedButton, &QAbstractButton::clicked, this, [&]() {
    m_view->setHideUnused(m_usedButton->isChecked());
  });

  m_filterBox = createSearchBox();
  connect(m_filterBox, &QLineEdit::textEdited, this, [&]() {
    m_view->setFilterText(m_filterBox->text().toStdString());
  });
  
  m_sizeSlider = new SliderWithLabel(50, 600);
  m_sizeSlider->setToolTip(tr("Adjust material preview size"));
  m_sizeSlider->setValue(int(pref(Preferences::MaterialBrowserIconSize) * 100.0f));
  
  // 确保滑块只能以50为步长移动
  QSlider* slider = m_sizeSlider->findChild<QSlider*>();
  if (slider) {
    slider->setSingleStep(50);
    slider->setPageStep(50);
    slider->setTickInterval(50);
  }
  
  connect(m_sizeSlider, &SliderWithLabel::valueChanged, this, &MaterialBrowser::sizeSliderChanged);

  auto* controlLayout = new QHBoxLayout{};
  controlLayout->setContentsMargins(
    LayoutConstants::NarrowHMargin,
    LayoutConstants::NarrowVMargin,
    LayoutConstants::NarrowHMargin,
    LayoutConstants::NarrowVMargin);
  controlLayout->setSpacing(LayoutConstants::NarrowHMargin);
  controlLayout->addWidget(m_sortOrderChoice);
  controlLayout->addWidget(m_groupButton);
  controlLayout->addWidget(m_usedButton);
  controlLayout->addWidget(m_filterBox, 1);
  
  auto* sizeLayout = new QHBoxLayout{};
  sizeLayout->setContentsMargins(
    LayoutConstants::NarrowHMargin,
    0,
    LayoutConstants::NarrowHMargin,
    LayoutConstants::NarrowVMargin);
  sizeLayout->addWidget(new QLabel(tr("Size:")));
  sizeLayout->addWidget(m_sizeSlider, 1);

  auto* outerLayout = new QVBoxLayout{};
  outerLayout->setContentsMargins(0, 0, 0, 0);
  outerLayout->setSpacing(0);
  outerLayout->addWidget(browserPanel, 1);
  outerLayout->addLayout(controlLayout, 0);
  outerLayout->addLayout(sizeLayout, 0);

  setLayout(outerLayout);
}

void MaterialBrowser::bindEvents()
{
  connect(
    m_view,
    &MaterialBrowserView::materialSelected,
    this,
    &MaterialBrowser::materialSelected);
}

void MaterialBrowser::connectObservers()
{
  auto document = kdl::mem_lock(m_document);
  m_notifierConnection +=
    document->documentWasNewedNotifier.connect(this, &MaterialBrowser::documentWasNewed);
  m_notifierConnection += document->documentWasLoadedNotifier.connect(
    this, &MaterialBrowser::documentWasLoaded);
  m_notifierConnection +=
    document->nodesWereAddedNotifier.connect(this, &MaterialBrowser::nodesWereAdded);
  m_notifierConnection +=
    document->nodesWereRemovedNotifier.connect(this, &MaterialBrowser::nodesWereRemoved);
  m_notifierConnection +=
    document->nodesDidChangeNotifier.connect(this, &MaterialBrowser::nodesDidChange);
  m_notifierConnection += document->brushFacesDidChangeNotifier.connect(
    this, &MaterialBrowser::brushFacesDidChange);
  m_notifierConnection += document->materialCollectionsDidChangeNotifier.connect(
    this, &MaterialBrowser::materialCollectionsDidChange);
  m_notifierConnection += document->currentMaterialNameDidChangeNotifier.connect(
    this, &MaterialBrowser::currentMaterialNameDidChange);

  auto& prefs = PreferenceManager::instance();
  m_notifierConnection += prefs.preferenceDidChangeNotifier.connect(
    this, &MaterialBrowser::preferenceDidChange);
}

void MaterialBrowser::documentWasNewed(MapDocument*)
{
  reload();
}

void MaterialBrowser::documentWasLoaded(MapDocument*)
{
  reload();
}

void MaterialBrowser::nodesWereAdded(const std::vector<mdl::Node*>&)
{
  reload();
}

void MaterialBrowser::nodesWereRemoved(const std::vector<mdl::Node*>&)
{
  reload();
}

void MaterialBrowser::nodesDidChange(const std::vector<mdl::Node*>&)
{
  reload();
}

void MaterialBrowser::brushFacesDidChange(const std::vector<mdl::BrushFaceHandle>&)
{
  reload();
}

void MaterialBrowser::materialCollectionsDidChange()
{
  reload();
}

void MaterialBrowser::currentMaterialNameDidChange(const std::string& /* materialName */)
{
  updateSelectedMaterial();
}

void MaterialBrowser::preferenceDidChange(const std::filesystem::path& path)
{
  auto document = kdl::mem_lock(m_document);
  if (
    path == Preferences::MaterialBrowserIconSize.path()
    || document->isGamePathPreference(path))
  {
    if (path == Preferences::MaterialBrowserIconSize.path()) {
      const float scaleFactor = pref(Preferences::MaterialBrowserIconSize);
      m_sizeSlider->setValue(int(scaleFactor * 100.0f));
    }
    reload();
  }
  else
  {
    m_view->update();
  }
}

void MaterialBrowser::reload()
{
  if (m_view)
  {
    updateSelectedMaterial();
    m_view->invalidate();
    m_view->update();
  }
}

void MaterialBrowser::updateSelectedMaterial()
{
  auto document = kdl::mem_lock(m_document);
  const auto& materialName = document->currentMaterialName();
  const auto* material = document->materialManager().material(materialName);
  m_view->setSelectedMaterial(material);
}

void MaterialBrowser::sizeSliderChanged(int value)
{
  // 将值调整为最近的50的倍数
  int roundedValue = ((value + 25) / 50) * 50;
  if (roundedValue < 50) roundedValue = 50;
  if (roundedValue > 600) roundedValue = 600;
  
  // 如果值被调整了，更新滑块但不触发valueChanged信号
  if (value != roundedValue) {
    m_sizeSlider->blockSignals(true);
    m_sizeSlider->setValue(roundedValue);
    m_sizeSlider->blockSignals(false);
    value = roundedValue;
  }
  
  const float scaleFactor = static_cast<float>(value) / 100.0f;
  
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::MaterialBrowserIconSize, scaleFactor);
  
  // 直接刷新视图，确保滑块值改变时立即更新材质预览大小
  reload();
}

} // namespace tb::ui
