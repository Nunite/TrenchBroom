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

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>

#include "mdl/ModelUtils.h"
#include "ui/SmartFaceSelectionPanel.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("SmartFaceSelectionPanel")
{
  auto changeCount = 0;
  auto confirmCount = 0;
  auto cancelCount = 0;
  auto panel = SmartFaceSelectionPanel{
    {}, [&] { ++changeCount; }, [&] { ++confirmCount; }, [&] { ++cancelCount; }};

  CHECK(panel.options().mode == mdl::SmartFaceSelectionMode::FaceStrip);
  CHECK(panel.options().angleTolerance == 15.0);
  CHECK(panel.options().gapTolerance == 0.0);
  CHECK(panel.operation() == SmartFaceSelectionOperation::Replace);
  CHECK(panel.expanded());
  CHECK(changeCount == 0);
  CHECK(confirmCount == 0);
  CHECK(cancelCount == 0);

  SECTION("Controls update options and operation")
  {
    auto* mode = panel.findChild<QComboBox*>(QStringLiteral("smartFaceSelectionMode"));
    auto* operation =
      panel.findChild<QComboBox*>(QStringLiteral("smartFaceSelectionOperation"));
    auto* angle =
      panel.findChild<QDoubleSpinBox*>(QStringLiteral("smartFaceSelectionAngle"));
    auto* gap = panel.findChild<QDoubleSpinBox*>(QStringLiteral("smartFaceSelectionGap"));
    REQUIRE(mode != nullptr);
    REQUIRE(operation != nullptr);
    REQUIRE(angle != nullptr);
    REQUIRE(gap != nullptr);

    mode->setCurrentIndex(1);
    operation->setCurrentIndex(2);
    angle->setValue(22.5);
    gap->setValue(1.25);

    CHECK(panel.options().mode == mdl::SmartFaceSelectionMode::Parallel);
    CHECK(panel.options().angleTolerance == 22.5);
    CHECK(panel.options().gapTolerance == 1.25);
    CHECK(panel.operation() == SmartFaceSelectionOperation::Remove);
    CHECK(changeCount == 4);
  }

  SECTION("Panel can be collapsed and expanded")
  {
    panel.setExpanded(false);
    CHECK_FALSE(panel.expanded());
    panel.setExpanded(true);
    CHECK(panel.expanded());
    CHECK(changeCount == 2);
  }

  SECTION("Apply and cancel buttons invoke their callbacks")
  {
    auto* apply =
      panel.findChild<QPushButton*>(QStringLiteral("smartFaceSelectionApply"));
    auto* cancel =
      panel.findChild<QPushButton*>(QStringLiteral("smartFaceSelectionCancel"));
    REQUIRE(apply != nullptr);
    REQUIRE(cancel != nullptr);

    apply->click();
    cancel->click();

    CHECK(confirmCount == 1);
    CHECK(cancelCount == 1);
  }
}

} // namespace tb::ui
