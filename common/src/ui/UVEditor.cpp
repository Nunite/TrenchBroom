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

#include "UVEditor.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QComboBox>
#include <QToolButton>
#include <QApplication>
#include <QClipboard>
#include <QtGlobal>

#include "mdl/ChangeBrushFaceAttributesRequest.h"
#include "ui/MapDocument.h"
#include "ui/QtUtils.h"
#include "ui/UVView.h"
#include "ui/ViewConstants.h"

#include "kdl/memory_utils.h"

namespace tb::ui
{

UVEditor::UVEditor(
  std::weak_ptr<MapDocument> document, GLContextManager& contextManager, QWidget* parent)
  : QWidget{parent}
  , m_document{std::move(document)}
{
  createGui(contextManager);
  connectObservers();
}

bool UVEditor::cancelMouseDrag()
{
  return m_uvView->cancelDrag();
}

void UVEditor::updateButtons()
{
  auto document = kdl::mem_lock(m_document);
  const bool enabled = !document->allSelectedBrushFaces().empty();

  m_resetUVButton->setEnabled(enabled);
  m_resetUVToWorldButton->setEnabled(enabled);
  m_flipUAxisButton->setEnabled(enabled);
  m_flipVAxisButton->setEnabled(enabled);
  m_rotateUVCCWButton->setEnabled(enabled);
  m_rotateUVCWButton->setEnabled(enabled);
}

void UVEditor::createGui(GLContextManager& contextManager)
{
  m_uvView = new UVView{m_document, contextManager};

  m_resetUVButton = createBitmapButton("ResetUV.svg", tr("Reset UV alignment"), this);
  m_resetUVToWorldButton = createBitmapButton(
    "ResetUVToWorld.svg", tr("Reset UV alignment to world aligned"), this);
  m_flipUAxisButton = createBitmapButton("FlipUAxis.svg", tr("Flip U axis"), this);
  m_flipVAxisButton = createBitmapButton("FlipVAxis.svg", tr("Flip V axis"), this);
  m_rotateUVCCWButton =
    createBitmapButton("RotateUVCCW.svg", tr("Rotate UV 90° counter-clockwise"), this);
  m_rotateUVCWButton =
    createBitmapButton("RotateUVCW.svg", tr("Rotate UV 90° clockwise"), this);

  connect(m_resetUVButton, &QAbstractButton::clicked, this, &UVEditor::resetUVClicked);
  connect(
    m_resetUVToWorldButton,
    &QAbstractButton::clicked,
    this,
    &UVEditor::resetUVToWorldClicked);
  connect(m_flipUAxisButton, &QAbstractButton::clicked, this, &UVEditor::flipUVHClicked);
  connect(m_flipVAxisButton, &QAbstractButton::clicked, this, &UVEditor::flipUVVClicked);
  connect(
    m_rotateUVCCWButton, &QAbstractButton::clicked, this, &UVEditor::rotateUVCCWClicked);
  connect(
    m_rotateUVCWButton, &QAbstractButton::clicked, this, &UVEditor::rotateUVCWClicked);

  // Create material operation combo box
  m_materialOperationComboBox = new QComboBox{};
  m_materialOperationComboBox->addItem(tr("Material Operations"));
  m_materialOperationComboBox->addItem(tr("Select Faces with Material"));
  m_materialOperationComboBox->addItem(tr("Select Faces with Material in Selected Brushes"));
  m_materialOperationComboBox->addItem(tr("Select Brushes with Material"));
  m_materialOperationComboBox->addItem(tr("Copy Material Name"));
  connect(m_materialOperationComboBox, QOverload<int>::of(&QComboBox::activated), this, &UVEditor::materialOperationChanged);

  auto* gridLabel = new QLabel{"Grid "};
  makeEmphasized(gridLabel);
  m_xSubDivisionEditor = new QSpinBox{};
  m_xSubDivisionEditor->setRange(1, 16);
  m_xSubDivisionEditor->setValue(1);

  m_ySubDivisionEditor = new QSpinBox{};
  m_ySubDivisionEditor->setRange(1, 16);
  m_ySubDivisionEditor->setValue(1);

  connect(
    m_xSubDivisionEditor,
    QOverload<int>::of(&QSpinBox::valueChanged),
    this,
    &UVEditor::subDivisionChanged);
  connect(
    m_ySubDivisionEditor,
    QOverload<int>::of(&QSpinBox::valueChanged),
    this,
    &UVEditor::subDivisionChanged);

  auto* bottomLayout = new QHBoxLayout{};
  bottomLayout->setContentsMargins(
    LayoutConstants::NarrowHMargin, 0, LayoutConstants::NarrowHMargin, 0);
  bottomLayout->setSpacing(LayoutConstants::NarrowHMargin);
  bottomLayout->addWidget(m_resetUVButton);
  bottomLayout->addWidget(m_resetUVToWorldButton);
  bottomLayout->addWidget(m_flipUAxisButton);
  bottomLayout->addWidget(m_flipVAxisButton);
  bottomLayout->addWidget(m_rotateUVCCWButton);
  bottomLayout->addWidget(m_rotateUVCWButton);
  bottomLayout->addWidget(m_materialOperationComboBox);
  bottomLayout->addStretch();
  bottomLayout->addWidget(gridLabel);
  bottomLayout->addWidget(new QLabel{"X:"});
  bottomLayout->addWidget(m_xSubDivisionEditor);
  bottomLayout->addSpacing(
    LayoutConstants::MediumHMargin - LayoutConstants::NarrowHMargin);
  bottomLayout->addWidget(new QLabel{"Y:"});
  bottomLayout->addWidget(m_ySubDivisionEditor);

  auto* outerLayout = new QVBoxLayout{};
  outerLayout->setContentsMargins(0, 0, 0, 0);
  outerLayout->setSpacing(LayoutConstants::NarrowVMargin);
  outerLayout->addWidget(m_uvView, 1);
  outerLayout->addLayout(bottomLayout);
  setLayout(outerLayout);

  updateButtons();
}

void UVEditor::selectionDidChange(const Selection&)
{
  updateButtons();
}

void UVEditor::connectObservers()
{
  auto document = kdl::mem_lock(m_document);
  m_notifierConnection +=
    document->selectionDidChangeNotifier.connect(this, &UVEditor::selectionDidChange);
}

void UVEditor::resetUVClicked()
{
  auto request = mdl::ChangeBrushFaceAttributesRequest{};

  auto document = kdl::mem_lock(m_document);
  request.resetAll(document->game()->config().faceAttribsConfig.defaults);
  document->setFaceAttributes(request);
}

void UVEditor::resetUVToWorldClicked()
{
  auto request = mdl::ChangeBrushFaceAttributesRequest{};

  auto document = kdl::mem_lock(m_document);
  request.resetAllToParaxial(document->game()->config().faceAttribsConfig.defaults);
  document->setFaceAttributes(request);
}

void UVEditor::flipUVHClicked()
{
  auto request = mdl::ChangeBrushFaceAttributesRequest{};
  request.mulXScale(-1.0f);

  auto document = kdl::mem_lock(m_document);
  document->setFaceAttributes(request);
}

void UVEditor::flipUVVClicked()
{
  auto request = mdl::ChangeBrushFaceAttributesRequest{};
  request.mulYScale(-1.0f);

  auto document = kdl::mem_lock(m_document);
  document->setFaceAttributes(request);
}

void UVEditor::rotateUVCCWClicked()
{
  auto request = mdl::ChangeBrushFaceAttributesRequest{};
  request.addRotation(90.0f);

  auto document = kdl::mem_lock(m_document);
  document->setFaceAttributes(request);
}

void UVEditor::rotateUVCWClicked()
{
  auto request = mdl::ChangeBrushFaceAttributesRequest{};
  request.addRotation(-90.0f);

  auto document = kdl::mem_lock(m_document);
  document->setFaceAttributes(request);
}

void UVEditor::subDivisionChanged()
{
  const auto x = m_xSubDivisionEditor->value();
  const auto y = m_ySubDivisionEditor->value();
  m_uvView->setSubDivisions(vm::vec2i(x, y));
}

void UVEditor::materialOperationChanged(int index)
{
  // Reset combo box to default selection after operation
  m_materialOperationComboBox->setCurrentIndex(0);
  
  auto document = kdl::mem_lock(m_document);
  if (!document->allSelectedBrushFaces().empty())
  {
    const mdl::BrushFace& face = document->allSelectedBrushFaces().front().face();
    const mdl::Material* material = face.material();
    
    if (material)
    {
      switch (index)
      {
        case 1: // Select Faces with Material
          document->selectFacesWithMaterial(material);
          break;
        case 2: // Select Faces with Material in Selected Brushes
          document->selectFacesWithMaterialInSelectedBrushes(material);
          break;
        case 3: // Select Brushes with Material
          document->selectBrushesWithMaterial(material);
          break;
        case 4: // Copy Material Name
          {
            QClipboard* clipboard = QApplication::clipboard();
            clipboard->setText(QString::fromStdString(material->name()));
          }
          break;
      }
    }
  }
}

} // namespace tb::ui
