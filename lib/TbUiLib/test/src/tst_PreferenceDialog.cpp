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

#include <QToolBar>

#include "ui/AppControllerFixture.h"
#include "ui/ColorsPreferencePane.h"
#include "ui/GamesPreferencePane.h"
#include "ui/KeyboardPreferencePane.h"
#include "ui/McpPreferencePane.h"
#include "ui/MiscPreferencePane.h"
#include "ui/MousePreferencePane.h"
#include "ui/PreferenceDialog.h"
#include "ui/UpdatePreferencePane.h"
#include "ui/ViewPreferencePane.h"

#include <memory>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("PreferenceDialog")
{
  auto fixture = AppControllerFixture{};
  auto dialog = std::make_unique<PreferenceDialog>(fixture.appController(), nullptr);

  SECTION("opens at a usable size")
  {
    CHECK(dialog->width() >= 800);
    CHECK(dialog->height() >= 300);
  }

  SECTION("uses stable preference panes")
  {
    auto* toolBar = dialog->findChild<QToolBar*>();
    REQUIRE(toolBar != nullptr);

    auto actionNames = QStringList{};
    for (auto* action : toolBar->actions())
    {
      actionNames << action->text();
      action->trigger();
    }

    CHECK(
      actionNames
      == QStringList{
        "Games", "View", "Colors", "Mouse", "Keyboard", "Misc", "MCP", "Update"});
  }

  dialog.reset();
}

TEST_CASE("PreferenceDialog.preferencePanes")
{
  auto fixture = AppControllerFixture{};

  SECTION("Games")
  {
    auto pane = std::make_unique<GamesPreferencePane>(fixture.appController(), nullptr);
    pane.reset();
  }

  SECTION("View")
  {
    auto pane = std::make_unique<ViewPreferencePane>();
    pane.reset();
  }

  SECTION("Colors")
  {
    auto pane = std::make_unique<ColorsPreferencePane>();
    pane.reset();
  }

  SECTION("Mouse")
  {
    auto pane = std::make_unique<MousePreferencePane>();
    pane.reset();
  }

  SECTION("Keyboard")
  {
    auto pane =
      std::make_unique<KeyboardPreferencePane>(fixture.appController(), nullptr);
    pane.reset();
  }

  SECTION("Misc")
  {
    auto pane = std::make_unique<MiscPreferencePane>(fixture.appController());
    pane.reset();
  }

  SECTION("MCP")
  {
    auto pane = std::make_unique<McpPreferencePane>(fixture.appController());
    pane.reset();
  }

  SECTION("Update")
  {
    auto pane = std::make_unique<UpdatePreferencePane>(fixture.appController());
    pane.reset();
  }
}

} // namespace tb::ui
