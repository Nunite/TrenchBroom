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

#include <QApplication>
#include <QLabel>
#include <QToolButton>
#include <QtTest/QTest>

#include "ui/ClickableTitleBar.h"
#include "ui/CollapsibleTitledPanel.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("CollapsibleTitledPanel")
{
  auto panel = CollapsibleTitledPanel{"Properties"};
  panel.resize(360, 240);
  panel.show();
  QApplication::processEvents();

  auto* titleBar = panel.findChild<ClickableTitleBar*>();
  auto* stateIcon = panel.findChild<QToolButton*>("ClickableTitleBar_StateIcon");
  auto* stateText = panel.findChild<QLabel*>("ClickableTitleBar_StateText");

  REQUIRE(titleBar != nullptr);
  REQUIRE(stateIcon != nullptr);
  REQUIRE(stateText != nullptr);

  SECTION("uses an icon-only expanded state")
  {
    CHECK(panel.expanded());
    CHECK(panel.property("expanded").toBool());
    CHECK(titleBar->property("expanded").toBool());
    CHECK(!panel.getPanel()->isHidden());
    CHECK(!stateIcon->isHidden());
    CHECK(!stateIcon->icon().isNull());
    CHECK(stateIcon->toolTip() == "Collapse");
    CHECK(stateText->isHidden());
  }

  SECTION("toggles from the full title row")
  {
    QTest::mouseClick(titleBar, Qt::LeftButton);
    QApplication::processEvents();

    CHECK_FALSE(panel.expanded());
    CHECK_FALSE(panel.property("expanded").toBool());
    CHECK_FALSE(titleBar->property("expanded").toBool());
    CHECK(panel.getPanel()->isHidden());
    CHECK(stateIcon->toolTip() == "Expand");
  }

  SECTION("supports keyboard toggling")
  {
    CHECK(titleBar->focusPolicy() == Qt::StrongFocus);

    QTest::keyClick(titleBar, Qt::Key_Space);
    CHECK_FALSE(panel.expanded());

    QTest::keyClick(titleBar, Qt::Key_Return);
    CHECK(panel.expanded());
  }

  SECTION("restores the collapsed state")
  {
    panel.collapse();
    const auto state = panel.saveState();

    auto restoredPanel = CollapsibleTitledPanel{"Restored"};
    REQUIRE(restoredPanel.restoreState(state));
    CHECK_FALSE(restoredPanel.expanded());
    CHECK(restoredPanel.getPanel()->isHidden());
  }
}

} // namespace tb::ui
