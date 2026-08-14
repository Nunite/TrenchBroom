/*
 Copyright (C) 2026 Jackson Palmer

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

#include "ui/SweepToolPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>

#include "ui/BorderLine.h"
#include "ui/SweepTool.h"
#include "ui/ViewConstants.h"

#include <array>
#include <memory>

namespace tb::ui
{

SweepToolPage::SweepToolPage(SweepTool& tool, QWidget* parent)
  : QWidget{parent}
  , m_tool{tool}
{
  createGui();

  m_notifierConnection += m_tool.sweepResultDidChangeNotifier.connect([this]() {
    updateControls();
    updateStatus();
  });
  m_notifierConnection +=
    m_tool.toolActivatedNotifier.connect([this](const auto&) { toolActivated(); });

  toolActivated();
}

void SweepToolPage::toolActivated()
{
  const auto& parameters = m_tool.parameters();
  const auto blockers = std::array{
    std::make_unique<QSignalBlocker>(m_mode),
    std::make_unique<QSignalBlocker>(m_segments),
    std::make_unique<QSignalBlocker>(m_iterations),
    std::make_unique<QSignalBlocker>(m_pathMode),
    std::make_unique<QSignalBlocker>(m_uvMode),
    std::make_unique<QSignalBlocker>(m_snapToInteger),
  };
  m_mode->setCurrentIndex(m_mode->findData(int(m_tool.constructionMode())));
  m_segments->setValue(int(parameters.segments));
  m_iterations->setValue(int(parameters.iterations));
  m_pathMode->setCurrentIndex(m_pathMode->findData(int(parameters.pathMode)));
  m_uvMode->setCurrentIndex(m_uvMode->findData(int(parameters.uvMode)));
  m_snapToInteger->setChecked(parameters.alignment == SweepAlignment::Integer);
  updateControls();
  updateStatus();
}

void SweepToolPage::createGui()
{
  auto* modeText = new QLabel{tr("Mode")};
  m_mode = new QComboBox{};
  m_mode->setObjectName(QStringLiteral("sweepMode"));
  m_mode->addItem(tr("Sweep"), int(SweepConstructionMode::Sweep));
  m_mode->addItem(tr("Bridge"), int(SweepConstructionMode::Bridge));
  m_mode->setToolTip(tr(
    "Bridge connects two disconnected sets of selected faces and locks the destination "
    "to its exact vertices"));

  auto* segmentsText = new QLabel{tr("Segments")};
  m_segments = new QSpinBox{this};
  m_segments->setRange(1, 64);
  m_segments->setToolTip(
    tr("Number of brushes between the source faces and the destination cap"));

  auto* pathText = new QLabel{tr("Path")};
  m_pathMode = new QComboBox{};
  m_pathMode->addItem(tr("Arc"), int(SweepPathMode::Arc));
  m_pathMode->addItem(tr("Straight"), int(SweepPathMode::Straight));
  m_pathMode->addItem(tr("S-bend"), int(SweepPathMode::SBend));
  m_pathMode->setToolTip(
    tr("Arc revolves around an axis, Straight is a linear loft, S-bend follows an "
       "S-curve; the destination cap ends up in the same place in each mode"));

  auto* iterationsText = new QLabel{tr("Iterations")};
  m_iterations = new QSpinBox{this};
  m_iterations->setObjectName(QStringLiteral("sweepIterations"));
  m_iterations->setRange(1, 8);
  m_iterations->setToolTip(
    tr("Repeats the sweep, continuing from the previous destination cap"));

  auto* uvText = new QLabel{tr("UV")};
  m_uvMode = new QComboBox{};
  m_uvMode->addItem(tr("Preserve"), int(SweepUvMode::Preserve));
  m_uvMode->addItem(tr("Continuous"), int(SweepUvMode::Continuous));
  m_uvMode->setToolTip(
    tr("Continuous globally unfolds connected boundary faces across the sweep while "
       "preserving the source texture scale; requires a Valve-style map format"));

  m_snapToInteger = new QCheckBox{tr("Snap to integer grid")};
  m_snapToInteger->setToolTip(tr("Round generated vertices to integer coordinates"));

  m_swapEndsButton = new QPushButton{tr("Swap ends")};
  m_swapEndsButton->setObjectName(QStringLiteral("sweepSwapEnds"));
  m_swapEndsButton->setToolTip(tr("Use the other selected face as the bridge entrance"));

  m_resetButton = new QPushButton{tr("Reset")};
  m_resetButton->setObjectName(QStringLiteral("sweepReset"));
  m_resetButton->setToolTip(tr("Move the destination cap back onto the selected faces"));

  m_statusIcon = new QLabel{this};
  const auto iconSize = style()->pixelMetric(QStyle::PM_SmallIconSize);
  m_statusIcon->setPixmap(
    style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(iconSize, iconSize));
  m_statusIcon->setFixedSize(iconSize, iconSize);
  m_statusLabel = new QLabel{this};

  connect(
    m_mode,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    &SweepToolPage::modeChanged);
  connect(
    m_segments,
    QOverload<int>::of(&QSpinBox::valueChanged),
    this,
    &SweepToolPage::segmentsChanged);
  connect(
    m_pathMode,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    &SweepToolPage::pathModeChanged);
  connect(
    m_iterations,
    QOverload<int>::of(&QSpinBox::valueChanged),
    this,
    &SweepToolPage::iterationsChanged);
  connect(
    m_uvMode,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    &SweepToolPage::uvModeChanged);
  connect(
    m_snapToInteger, &QCheckBox::toggled, this, &SweepToolPage::snapToIntegerChanged);
  connect(
    m_swapEndsButton, &QAbstractButton::clicked, this, &SweepToolPage::swapEndsClicked);
  connect(m_resetButton, &QAbstractButton::clicked, this, &SweepToolPage::resetClicked);

  auto* layout = new QHBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  layout->addWidget(modeText, 0, Qt::AlignVCenter);
  layout->addSpacing(LayoutConstants::NarrowHMargin);
  layout->addWidget(m_mode, 0, Qt::AlignVCenter);
  layout->addSpacing(LayoutConstants::WideHMargin);
  layout->addWidget(segmentsText, 0, Qt::AlignVCenter);
  layout->addSpacing(LayoutConstants::NarrowHMargin);
  layout->addWidget(m_segments, 0, Qt::AlignVCenter);
  layout->addSpacing(LayoutConstants::WideHMargin);
  layout->addWidget(pathText, 0, Qt::AlignVCenter);
  layout->addSpacing(LayoutConstants::NarrowHMargin);
  layout->addWidget(m_pathMode, 0, Qt::AlignVCenter);
  layout->addSpacing(LayoutConstants::WideHMargin);
  layout->addWidget(iterationsText, 0, Qt::AlignVCenter);
  layout->addSpacing(LayoutConstants::NarrowHMargin);
  layout->addWidget(m_iterations, 0, Qt::AlignVCenter);
  layout->addSpacing(LayoutConstants::WideHMargin);
  layout->addWidget(uvText, 0, Qt::AlignVCenter);
  layout->addSpacing(LayoutConstants::NarrowHMargin);
  layout->addWidget(m_uvMode, 0, Qt::AlignVCenter);
  layout->addSpacing(LayoutConstants::WideHMargin);
  layout->addWidget(new BorderLine{BorderLine::Direction::Vertical}, 0);
  layout->addSpacing(LayoutConstants::WideHMargin);
  layout->addWidget(m_snapToInteger, 0, Qt::AlignVCenter);
  layout->addSpacing(LayoutConstants::WideHMargin);
  layout->addWidget(m_swapEndsButton, 0, Qt::AlignVCenter);
  layout->addSpacing(LayoutConstants::WideHMargin);
  layout->addWidget(m_resetButton, 0, Qt::AlignVCenter);
  layout->addSpacing(LayoutConstants::WideHMargin);
  layout->addWidget(m_statusIcon, 0, Qt::AlignVCenter);
  layout->addSpacing(LayoutConstants::NarrowHMargin);
  layout->addWidget(m_statusLabel, 0, Qt::AlignVCenter);
  layout->addStretch(1);

  setLayout(layout);
}

void SweepToolPage::updateControls()
{
  const auto bridge = m_tool.constructionMode() == SweepConstructionMode::Bridge;
  m_iterations->setEnabled(!bridge);
  m_swapEndsButton->setVisible(bridge);
  m_swapEndsButton->setEnabled(bridge && m_tool.bridgeAvailable());
  m_resetButton->setEnabled(!bridge);
}

void SweepToolPage::updateStatus()
{
  const auto& issues = m_tool.sweepIssues();
  const auto hasIssues = !issues.empty();
  m_statusIcon->setVisible(hasIssues);
  m_statusLabel->setVisible(hasIssues);

  if (!hasIssues)
  {
    m_statusLabel->clear();
    m_statusLabel->setToolTip({});
    return;
  }

  m_statusLabel->setText(
    tr("%n sweep issue(s) - hover for details", "", int(issues.size())));
  m_statusLabel->setToolTip(QString::fromStdString(issues.front().message));
  m_statusIcon->setToolTip(m_statusLabel->toolTip());
}

void SweepToolPage::modeChanged(const int index)
{
  if (index >= 0)
  {
    m_tool.setConstructionMode(SweepConstructionMode(m_mode->itemData(index).toInt()));
  }
}

void SweepToolPage::segmentsChanged(const int value)
{
  if (value >= 0)
  {
    auto parameters = m_tool.parameters();
    parameters.segments = size_t(value);
    m_tool.setParameters(parameters);
  }
}

void SweepToolPage::pathModeChanged(const int index)
{
  if (index >= 0)
  {
    auto parameters = m_tool.parameters();
    parameters.pathMode = SweepPathMode(m_pathMode->itemData(index).toInt());
    m_tool.setParameters(parameters);
  }
}

void SweepToolPage::iterationsChanged(const int value)
{
  if (value >= 0)
  {
    auto parameters = m_tool.parameters();
    parameters.iterations = size_t(value);
    m_tool.setParameters(parameters);
  }
}

void SweepToolPage::uvModeChanged(const int index)
{
  if (index >= 0)
  {
    auto parameters = m_tool.parameters();
    parameters.uvMode = SweepUvMode(m_uvMode->itemData(index).toInt());
    m_tool.setParameters(parameters);
  }
}

void SweepToolPage::snapToIntegerChanged(const bool checked)
{
  auto parameters = m_tool.parameters();
  parameters.alignment = checked ? SweepAlignment::Integer : SweepAlignment::Free;
  m_tool.setParameters(parameters);
}

void SweepToolPage::swapEndsClicked()
{
  m_tool.swapBridgeEnds();
}

void SweepToolPage::resetClicked()
{
  m_tool.reset();
}

} // namespace tb::ui
