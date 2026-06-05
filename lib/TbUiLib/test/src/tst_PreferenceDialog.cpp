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

#include <QGroupBox>
#include <QLabel>
#include <QToolBar>

#include "ui/AppControllerFixture.h"
#include "ui/PreferenceDialog.h"

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
    CHECK(dialog->height() >= 560);
  }

  SECTION("contains the migrated misc preferences pane")
  {
    auto* toolBar = dialog->findChild<QToolBar*>();
    REQUIRE(toolBar != nullptr);

    auto* miscAction = [&]() -> QAction* {
      for (auto* action : toolBar->actions())
      {
        if (action->text() == "Misc")
        {
          return action;
        }
      }
      return nullptr;
    }();

    REQUIRE(miscAction != nullptr);

    miscAction->trigger();

    auto foundLanguageLabel = false;
    for (auto* label : dialog->findChildren<QLabel*>())
    {
      foundLanguageLabel =
        foundLanguageLabel || label->text() == QStringLiteral("UI Language");
    }

    auto foundPieMenuGroup = false;
    for (auto* groupBox : dialog->findChildren<QGroupBox*>())
    {
      foundPieMenuGroup =
        foundPieMenuGroup || groupBox->title() == QStringLiteral("Pie Menu Actions");
    }

    CHECK(foundLanguageLabel);
    CHECK(foundPieMenuGroup);
  }

  dialog.reset();
}

} // namespace tb::ui
