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

#include <QListWidget>
#include <QPushButton>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "ui/ActionManager.h"
#include "ui/PieMenuSettingsDialog.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("PieMenuSettingsDialog")
{
  auto actionManager = ActionManager{};
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::PieMenuAction, "Menu/Edit/Undo|Menu/Edit/Redo");

  auto dialog = PieMenuSettingsDialog{};

  SECTION("loads configured pie menu actions")
  {
    auto* list = dialog.findChild<QListWidget*>();
    REQUIRE(list != nullptr);
    REQUIRE(list->count() == 2);
    CHECK(list->item(0)->text() == QStringLiteral("Undo"));
    CHECK(
      list->item(0)->data(Qt::UserRole).toString() == QStringLiteral("Menu/Edit/Undo"));
    CHECK(list->item(1)->text() == QStringLiteral("Redo"));
    CHECK(
      list->item(1)->data(Qt::UserRole).toString() == QStringLiteral("Menu/Edit/Redo"));
  }

  SECTION("remove selected action updates preference")
  {
    auto* list = dialog.findChild<QListWidget*>();
    REQUIRE(list != nullptr);
    list->setCurrentRow(0);

    auto* removeButton = static_cast<QPushButton*>(nullptr);
    for (auto* button : dialog.findChildren<QPushButton*>())
    {
      if (button->text() == QStringLiteral("Remove Selected"))
      {
        removeButton = button;
      }
    }
    REQUIRE(removeButton != nullptr);

    removeButton->click();
    CHECK(prefs.get(Preferences::PieMenuAction) == "Menu/Edit/Redo");
  }

  SECTION("remove all actions updates preference")
  {
    auto* clearButton = static_cast<QPushButton*>(nullptr);
    for (auto* button : dialog.findChildren<QPushButton*>())
    {
      if (button->text() == QStringLiteral("Remove All"))
      {
        clearButton = button;
      }
    }
    REQUIRE(clearButton != nullptr);

    clearButton->click();
    CHECK(prefs.get(Preferences::PieMenuAction).empty());
  }
}

} // namespace tb::ui
