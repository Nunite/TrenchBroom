/*
 Copyright (C) 2026 XiangXtreme

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

#include "ui/SmartFaceSelectionPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <utility>

namespace tb::ui
{

SmartFaceSelectionPanel::SmartFaceSelectionPanel(
  std::vector<mdl::BrushFaceHandle> initialSelection,
  std::function<void()> parametersDidChange,
  std::function<void()> confirm,
  std::function<void()> cancel,
  QWidget* parent)
  : QFrame{parent}
  , m_initialSelection{std::move(initialSelection)}
  , m_candidates{m_initialSelection}
  , m_parametersDidChange{std::move(parametersDidChange)}
  , m_confirm{std::move(confirm)}
  , m_cancel{std::move(cancel)}
{
  createGui();
}

const std::vector<mdl::BrushFaceHandle>& SmartFaceSelectionPanel::initialSelection() const
{
  return m_initialSelection;
}

const std::vector<mdl::BrushFaceHandle>& SmartFaceSelectionPanel::candidates() const
{
  return m_candidates;
}

void SmartFaceSelectionPanel::setCandidates(std::vector<mdl::BrushFaceHandle> candidates)
{
  m_candidates = std::move(candidates);
  updateCount();
}

mdl::SmartFaceSelectionOptions SmartFaceSelectionPanel::options() const
{
  const auto* followSeedDirection =
    findChild<QCheckBox*>(QStringLiteral("smartFaceSelectionFollowSeedDirection"));
  const auto* stopAtBranches =
    findChild<QCheckBox*>(QStringLiteral("smartFaceSelectionStopAtBranches"));
  const auto* sameMaterial =
    findChild<QCheckBox*>(QStringLiteral("smartFaceSelectionSameMaterial"));

  return {
    mdl::SmartFaceSelectionMode(m_mode->currentData().toInt()),
    m_angle->value(),
    m_gap->value(),
    followSeedDirection->isEnabled() && followSeedDirection->isChecked(),
    stopAtBranches->isChecked(),
    sameMaterial->isChecked(),
  };
}

SmartFaceSelectionOperation SmartFaceSelectionPanel::operation() const
{
  return SmartFaceSelectionOperation(m_operation->currentData().toInt());
}

bool SmartFaceSelectionPanel::expanded() const
{
  return !m_controls->isHidden();
}

void SmartFaceSelectionPanel::setExpanded(const bool expanded)
{
  if (this->expanded() == expanded)
  {
    return;
  }
  m_controls->setVisible(expanded);
  m_collapseButton->setIcon(
    style()->standardIcon(expanded ? QStyle::SP_ArrowDown : QStyle::SP_ArrowRight));
  m_collapseButton->setToolTip(expanded ? tr("Collapse") : tr("Expand"));
  adjustSize();
  m_parametersDidChange();
}

void SmartFaceSelectionPanel::createGui()
{
  setObjectName(QStringLiteral("smartFaceSelectionPanel"));
  setAttribute(Qt::WA_DeleteOnClose);
  setFrameShape(QFrame::StyledPanel);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  setMinimumWidth(292);

  m_collapseButton = new QToolButton{};
  m_collapseButton->setObjectName(QStringLiteral("smartFaceSelectionCollapse"));
  m_collapseButton->setAutoRaise(true);

  auto* title = new QLabel{tr("Smart Face Selection")};
  auto titleFont = title->font();
  titleFont.setBold(true);
  title->setFont(titleFont);

  m_countLabel = new QLabel{};
  m_countLabel->setObjectName(QStringLiteral("smartFaceSelectionCount"));
  m_countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

  auto* headerLayout = new QHBoxLayout{};
  headerLayout->setContentsMargins(4, 3, 7, 3);
  headerLayout->setSpacing(4);
  headerLayout->addWidget(m_collapseButton);
  headerLayout->addWidget(title, 1);
  headerLayout->addWidget(m_countLabel);

  m_controls = new QWidget{};
  m_controls->setObjectName(QStringLiteral("smartFaceSelectionControls"));

  m_mode = new QComboBox{};
  m_mode->setObjectName(QStringLiteral("smartFaceSelectionMode"));
  m_mode->addItem(tr("Face Strip"), int(mdl::SmartFaceSelectionMode::FaceStrip));
  m_mode->addItem(tr("Parallel Faces"), int(mdl::SmartFaceSelectionMode::Parallel));

  m_operation = new QComboBox{};
  m_operation->setObjectName(QStringLiteral("smartFaceSelectionOperation"));
  m_operation->addItem(tr("Replace"), int(SmartFaceSelectionOperation::Replace));
  m_operation->addItem(tr("Add"), int(SmartFaceSelectionOperation::Add));
  m_operation->addItem(tr("Remove"), int(SmartFaceSelectionOperation::Remove));

  m_angle = new QDoubleSpinBox{};
  m_angle->setObjectName(QStringLiteral("smartFaceSelectionAngle"));
  m_angle->setRange(0.0, 90.0);
  m_angle->setDecimals(1);
  m_angle->setSingleStep(1.0);
  m_angle->setSuffix(tr(" deg"));
  m_angle->setValue(15.0);

  m_gap = new QDoubleSpinBox{};
  m_gap->setObjectName(QStringLiteral("smartFaceSelectionGap"));
  m_gap->setRange(0.0, 64.0);
  m_gap->setDecimals(2);
  m_gap->setSingleStep(0.25);
  m_gap->setSuffix(tr(" u"));

  auto* followSeedDirection = new QCheckBox{tr("Follow Seed Direction")};
  followSeedDirection->setObjectName(
    QStringLiteral("smartFaceSelectionFollowSeedDirection"));
  followSeedDirection->setToolTip(
    tr("Follow the path defined by at least two selected seed faces"));
  followSeedDirection->setEnabled(m_initialSelection.size() >= 2u);

  auto* stopAtBranches = new QCheckBox{tr("Stop at Branches")};
  stopAtBranches->setObjectName(QStringLiteral("smartFaceSelectionStopAtBranches"));
  stopAtBranches->setToolTip(
    tr("Stop before crossing junctions with more than two matching neighbors"));

  auto* sameMaterial = new QCheckBox{tr("Same Material")};
  sameMaterial->setObjectName(QStringLiteral("smartFaceSelectionSameMaterial"));
  sameMaterial->setToolTip(tr("Only include faces whose material matches a seed face"));

  auto* modeLabel = new QLabel{tr("Match")};
  modeLabel->setBuddy(m_mode);
  auto* operationLabel = new QLabel{tr("Selection")};
  operationLabel->setBuddy(m_operation);
  auto* angleLabel = new QLabel{tr("Angle")};
  angleLabel->setBuddy(m_angle);
  auto* gapLabel = new QLabel{tr("Gap")};
  gapLabel->setBuddy(m_gap);

  auto* cancelButton =
    new QPushButton{style()->standardIcon(QStyle::SP_DialogCancelButton), tr("Cancel")};
  cancelButton->setObjectName(QStringLiteral("smartFaceSelectionCancel"));
  auto* applyButton =
    new QPushButton{style()->standardIcon(QStyle::SP_DialogApplyButton), tr("Apply")};
  applyButton->setObjectName(QStringLiteral("smartFaceSelectionApply"));

  auto* buttonLayout = new QHBoxLayout{};
  buttonLayout->setContentsMargins(0, 3, 0, 0);
  buttonLayout->setSpacing(6);
  buttonLayout->addStretch(1);
  buttonLayout->addWidget(cancelButton);
  buttonLayout->addWidget(applyButton);

  auto* controlsLayout = new QGridLayout{};
  controlsLayout->setContentsMargins(10, 4, 10, 6);
  controlsLayout->setHorizontalSpacing(8);
  controlsLayout->setVerticalSpacing(5);
  controlsLayout->addWidget(modeLabel, 0, 0);
  controlsLayout->addWidget(m_mode, 0, 1);
  controlsLayout->addWidget(operationLabel, 1, 0);
  controlsLayout->addWidget(m_operation, 1, 1);
  controlsLayout->addWidget(angleLabel, 2, 0);
  controlsLayout->addWidget(m_angle, 2, 1);
  controlsLayout->addWidget(gapLabel, 3, 0);
  controlsLayout->addWidget(m_gap, 3, 1);
  controlsLayout->addWidget(followSeedDirection, 4, 0, 1, 2);
  controlsLayout->addWidget(stopAtBranches, 5, 0, 1, 2);
  controlsLayout->addWidget(sameMaterial, 6, 0, 1, 2);
  controlsLayout->addLayout(buttonLayout, 7, 0, 1, 2);
  controlsLayout->setColumnStretch(1, 1);
  m_controls->setLayout(controlsLayout);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addLayout(headerLayout);
  layout->addWidget(m_controls);
  setLayout(layout);

  const auto parametersChanged = [this] { m_parametersDidChange(); };
  connect(
    m_collapseButton, &QToolButton::clicked, this, [this] { setExpanded(!expanded()); });
  connect(
    m_mode,
    qOverload<int>(&QComboBox::currentIndexChanged),
    this,
    [this, followSeedDirection] {
      const auto faceStrip = mdl::SmartFaceSelectionMode(m_mode->currentData().toInt())
                             == mdl::SmartFaceSelectionMode::FaceStrip;
      followSeedDirection->setEnabled(faceStrip && m_initialSelection.size() >= 2u);
      m_parametersDidChange();
    });
  connect(
    m_operation,
    qOverload<int>(&QComboBox::currentIndexChanged),
    this,
    parametersChanged);
  connect(
    m_angle, qOverload<double>(&QDoubleSpinBox::valueChanged), this, parametersChanged);
  connect(
    m_gap, qOverload<double>(&QDoubleSpinBox::valueChanged), this, parametersChanged);
  connect(followSeedDirection, &QCheckBox::toggled, this, parametersChanged);
  connect(stopAtBranches, &QCheckBox::toggled, this, parametersChanged);
  connect(sameMaterial, &QCheckBox::toggled, this, parametersChanged);
  connect(cancelButton, &QPushButton::clicked, this, m_cancel);
  connect(applyButton, &QPushButton::clicked, this, m_confirm);

  auto* confirmShortcut = new QShortcut{QKeySequence{Qt::Key_Return}, this};
  confirmShortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(confirmShortcut, &QShortcut::activated, this, m_confirm);
  auto* enterShortcut = new QShortcut{QKeySequence{Qt::Key_Enter}, this};
  enterShortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(enterShortcut, &QShortcut::activated, this, m_confirm);
  auto* cancelShortcut = new QShortcut{QKeySequence{Qt::Key_Escape}, this};
  cancelShortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(cancelShortcut, &QShortcut::activated, this, m_cancel);

  updateCount();
  m_controls->show();
  m_collapseButton->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
  m_collapseButton->setToolTip(tr("Collapse"));
}

void SmartFaceSelectionPanel::updateCount()
{
  const auto count = static_cast<int>(std::min<size_t>(
    m_candidates.size(), static_cast<size_t>(std::numeric_limits<int>::max())));
  m_countLabel->setText(tr("%n face(s)", nullptr, count));
}

} // namespace tb::ui
