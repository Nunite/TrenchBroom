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
#include <QToolButton>
#include <QtGlobal>

#include "mdl/Game.h" // IWYU pragma: keep
#include "mdl/Map.h"
#include "mdl/Map_Brushes.h"
#include "mdl/UpdateBrushFaceAttributes.h"
#include "ui/QtUtils.h"
#include "ui/UVView.h"
#include "ui/ViewConstants.h"

namespace tb::ui
{

namespace
{

QToolButton* createTextButton(const QString& text, const QString& tooltip, QWidget* parent)
{
  auto* button = new QToolButton{parent};
  button->setAutoRaise(true);
  button->setText(text);
  button->setToolTip(tooltip);
  return button;
}

} // namespace

UVEditor::UVEditor(mdl::Map& map, GLContextManager& contextManager, QWidget* parent)
  : QWidget{parent}
  , m_map{map}
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
  const bool enabled = !m_map.selection().allBrushFaces().empty();

  m_resetUVButton->setEnabled(enabled);
  m_resetUVToWorldButton->setEnabled(enabled);
  m_flipUAxisButton->setEnabled(enabled);
  m_flipVAxisButton->setEnabled(enabled);
  m_rotateUVCCWButton->setEnabled(enabled);
  m_rotateUVCWButton->setEnabled(enabled);
  m_fitUVButton->setEnabled(enabled);
  m_alignUVLeftButton->setEnabled(enabled);
  m_alignUVTopButton->setEnabled(enabled);
  m_alignUVCenterButton->setEnabled(enabled);
  m_alignUVRightButton->setEnabled(enabled);
  m_alignUVBottomButton->setEnabled(enabled);
}

void UVEditor::createGui(GLContextManager& contextManager)
{
  m_uvView = new UVView{m_map, contextManager};

  m_resetUVButton = createBitmapButton("ResetUV.svg", tr("Reset UV alignment"), this);
  m_resetUVToWorldButton = createBitmapButton(
    "ResetUVToWorld.svg", tr("Reset UV alignment to world aligned"), this);
  m_flipUAxisButton = createBitmapButton("FlipUAxis.svg", tr("Flip U axis"), this);
  m_flipVAxisButton = createBitmapButton("FlipVAxis.svg", tr("Flip V axis"), this);
  m_rotateUVCCWButton =
    createBitmapButton("RotateUVCCW.svg", tr("Rotate UV 90° counter-clockwise"), this);
  m_rotateUVCWButton =
    createBitmapButton("RotateUVCW.svg", tr("Rotate UV 90° clockwise"), this);
  m_fitUVButton = createTextButton(tr("Fit"), tr("Fit UVs to texture"), this);
  m_alignUVLeftButton = createTextButton(tr("L"), tr("Align UVs left"), this);
  m_alignUVTopButton = createTextButton(tr("T"), tr("Align UVs top"), this);
  m_alignUVCenterButton = createTextButton(tr("C"), tr("Align UVs center"), this);
  m_alignUVRightButton = createTextButton(tr("R"), tr("Align UVs right"), this);
  m_alignUVBottomButton = createTextButton(tr("B"), tr("Align UVs bottom"), this);

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
  connect(m_fitUVButton, &QAbstractButton::clicked, this, &UVEditor::fitUVClicked);
  connect(
    m_alignUVLeftButton, &QAbstractButton::clicked, this, &UVEditor::alignUVLeftClicked);
  connect(m_alignUVTopButton, &QAbstractButton::clicked, this, &UVEditor::alignUVTopClicked);
  connect(
    m_alignUVCenterButton,
    &QAbstractButton::clicked,
    this,
    &UVEditor::alignUVCenterClicked);
  connect(
    m_alignUVRightButton,
    &QAbstractButton::clicked,
    this,
    &UVEditor::alignUVRightClicked);
  connect(
    m_alignUVBottomButton,
    &QAbstractButton::clicked,
    this,
    &UVEditor::alignUVBottomClicked);

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
  bottomLayout->addWidget(m_fitUVButton);
  bottomLayout->addWidget(m_alignUVLeftButton);
  bottomLayout->addWidget(m_alignUVTopButton);
  bottomLayout->addWidget(m_alignUVCenterButton);
  bottomLayout->addWidget(m_alignUVRightButton);
  bottomLayout->addWidget(m_alignUVBottomButton);
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

void UVEditor::documentDidChange()
{
  updateButtons();
}

void UVEditor::connectObservers()
{
  m_notifierConnection +=
    m_map.documentDidChangeNotifier.connect(this, &UVEditor::documentDidChange);
}

void UVEditor::resetUVClicked()
{
  setBrushFaceAttributes(
    m_map, mdl::resetAll(m_map.game()->config().faceAttribsConfig.defaults));
}

void UVEditor::resetUVToWorldClicked()
{
  setBrushFaceAttributes(
    m_map, mdl::resetAllToParaxial(m_map.game()->config().faceAttribsConfig.defaults));
}

void UVEditor::flipUVHClicked()
{
  setBrushFaceAttributes(m_map, {.xScale = mdl::MultiplyValue{-1.0f}});
}

void UVEditor::flipUVVClicked()
{
  setBrushFaceAttributes(m_map, {.yScale = mdl::MultiplyValue{-1.0f}});
}

void UVEditor::rotateUVCCWClicked()
{
  setBrushFaceAttributes(m_map, {.rotation = mdl::AddValue{90.0f}});
}

void UVEditor::rotateUVCWClicked()
{
  setBrushFaceAttributes(m_map, {.rotation = mdl::AddValue{-90.0f}});
}

void UVEditor::fitUVClicked()
{
  mdl::fitUV(m_map);
}

void UVEditor::alignUVLeftClicked()
{
  mdl::alignUV(m_map, mdl::UVAlign::Left);
}

void UVEditor::alignUVTopClicked()
{
  mdl::alignUV(m_map, mdl::UVAlign::Top);
}

void UVEditor::alignUVCenterClicked()
{
  mdl::alignUV(m_map, mdl::UVAlign::Center);
}

void UVEditor::alignUVRightClicked()
{
  mdl::alignUV(m_map, mdl::UVAlign::Right);
}

void UVEditor::alignUVBottomClicked()
{
  mdl::alignUV(m_map, mdl::UVAlign::Bottom);
}

void UVEditor::subDivisionChanged()
{
  const auto x = m_xSubDivisionEditor->value();
  const auto y = m_ySubDivisionEditor->value();
  m_uvView->setSubDivisions(vm::vec2i(x, y));
}

} // namespace tb::ui
