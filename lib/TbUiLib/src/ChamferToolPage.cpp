/*
 Copyright (C) 2026 Kristian Duske

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

#include "ui/ChamferToolPage.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>

#include "ui/ChamferTool.h"
#include "ui/MapDocument.h"
#include "ui/ViewConstants.h"

#include <algorithm>
#include <limits>

namespace tb::ui
{

ChamferToolPage::ChamferToolPage(
  MapDocument& document, ChamferTool& tool, QWidget* parent)
  : QWidget{parent}
  , m_document{document}
  , m_tool{tool}
{
  createGui();
  connectObservers();
  updateGui();
}

void ChamferToolPage::createGui()
{
  auto* title = new QLabel{tr("Chamfer")};

  m_target = new QComboBox{};
  m_target->setObjectName(QStringLiteral("chamferTarget"));
  m_target->addItem(tr("Edges"), int(ChamferTarget::Edges));
  m_target->addItem(tr("Vertices"), int(ChamferTarget::Vertices));

  auto* distanceLabel = new QLabel{tr("Distance")};
  m_distance = new QDoubleSpinBox{};
  m_distance->setObjectName(QStringLiteral("chamferDistance"));
  m_distance->setRange(0.01, 100000.0);
  m_distance->setDecimals(2);
  m_distance->setSingleStep(1.0);
  m_distance->setValue(m_tool.parameters().distance);
  m_distance->setKeyboardTracking(false);

  m_segmentsLabel = new QLabel{tr("Segments")};
  m_segments = new QSpinBox{};
  m_segments->setObjectName(QStringLiteral("chamferSegments"));
  m_segments->setRange(1, 64);
  m_segments->setValue(m_tool.parameters().segments);

  m_status = new QLabel{};
  m_status->setObjectName(QStringLiteral("chamferStatus"));

  m_apply = new QPushButton{tr("Apply")};
  m_apply->setObjectName(QStringLiteral("chamferApply"));

  connect(
    m_target,
    qOverload<int>(&QComboBox::currentIndexChanged),
    this,
    &ChamferToolPage::targetChanged);
  connect(
    m_distance,
    qOverload<double>(&QDoubleSpinBox::valueChanged),
    this,
    &ChamferToolPage::distanceChanged);
  connect(
    m_segments,
    qOverload<int>(&QSpinBox::valueChanged),
    this,
    &ChamferToolPage::segmentsChanged);
  connect(m_apply, &QPushButton::clicked, this, &ChamferToolPage::applyChamfer);

  auto* layout = new QHBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(LayoutConstants::MediumHMargin);
  layout->addWidget(title, 0, Qt::AlignVCenter);
  layout->addWidget(m_target, 0, Qt::AlignVCenter);
  layout->addWidget(distanceLabel, 0, Qt::AlignVCenter);
  layout->addWidget(m_distance, 0, Qt::AlignVCenter);
  layout->addWidget(m_segmentsLabel, 0, Qt::AlignVCenter);
  layout->addWidget(m_segments, 0, Qt::AlignVCenter);
  layout->addWidget(m_apply, 0, Qt::AlignVCenter);
  layout->addWidget(m_status, 0, Qt::AlignVCenter);
  layout->addStretch(1);
  setLayout(layout);
}

void ChamferToolPage::connectObservers()
{
  m_notifierConnection +=
    m_document.documentWasLoadedNotifier.connect([this]() { updateGui(); });
  m_notifierConnection +=
    m_document.selectionDidChangeNotifier.connect([this](const auto&) { updateGui(); });
  m_notifierConnection +=
    m_tool.toolActivatedNotifier.connect([this](const auto&) { updateGui(); });
  m_notifierConnection +=
    m_tool.targetDidChangeNotifier.connect([this](const auto&) { updateGui(); });
  m_notifierConnection +=
    m_tool.parametersDidChangeNotifier.connect([this](const auto&) { updateGui(); });
  m_notifierConnection += m_tool.toolHandleSelectionChangedNotifier.connect(
    [this](const auto&) { updateGui(); });
}

void ChamferToolPage::targetChanged(const int index)
{
  m_tool.setTarget(ChamferTarget(m_target->itemData(index).toInt()));
}

void ChamferToolPage::distanceChanged(const double value)
{
  auto parameters = m_tool.parameters();
  parameters.distance = value;
  m_tool.setParameters(parameters);
}

void ChamferToolPage::segmentsChanged(const int value)
{
  auto parameters = m_tool.parameters();
  parameters.segments = value;
  m_tool.setParameters(parameters);
}

void ChamferToolPage::applyChamfer()
{
  if (!m_tool.apply())
  {
    m_status->setText(tr("Chamfer failed"));
    return;
  }
  updateGui();
}

void ChamferToolPage::updateGui()
{
  const auto target = m_tool.target();
  const auto targetIndex = m_target->findData(int(target));
  if (targetIndex >= 0)
  {
    const auto blocker = QSignalBlocker{m_target};
    m_target->setCurrentIndex(targetIndex);
  }

  {
    const auto blocker = QSignalBlocker{m_distance};
    m_distance->setValue(m_tool.parameters().distance);
  }
  {
    const auto blocker = QSignalBlocker{m_segments};
    m_segments->setValue(m_tool.parameters().segments);
  }

  const auto edges = target == ChamferTarget::Edges;
  m_segmentsLabel->setVisible(edges);
  m_segments->setVisible(edges);

  const auto count = static_cast<int>(std::min<size_t>(
    m_tool.selectedHandleCount(), static_cast<size_t>(std::numeric_limits<int>::max())));
  if (m_tool.previewFailed())
  {
    m_status->setText(tr("Chamfer cannot be applied"));
  }
  else
  {
    m_status->setText(
      edges ? tr("%n edge(s) selected", nullptr, count)
            : tr("%n vertex/vertices selected", nullptr, count));
  }
  m_apply->setEnabled(m_tool.canApply());
}

} // namespace tb::ui
