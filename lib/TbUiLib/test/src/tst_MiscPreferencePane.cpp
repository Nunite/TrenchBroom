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

#include "PreferenceManager.h"
#include "Preferences.h"
#include "ui/ActionManager.h"
#include "ui/MiscPreferencePane.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
QListWidget* listWithItems(MiscPreferencePane& pane, const QStringList& items)
{
  for (auto* list : pane.findChildren<QListWidget*>())
  {
    if (list->count() != items.size())
    {
      continue;
    }

    auto matches = true;
    for (auto i = 0; i < list->count(); ++i)
    {
      matches = matches && list->item(i)->text() == items[i];
    }

    if (matches)
    {
      return list;
    }
  }

  return nullptr;
}
} // namespace

TEST_CASE("MiscPreferencePane")
{
  auto actionManager = ActionManager{};
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::DefaultPluginPaths, "C:/tb/plugin_a.py|D:/tools/plugin_b.py");
  prefs.set(Preferences::PieMenuAction, "Menu/Edit/Undo|Menu/Edit/Redo");

  auto pane = MiscPreferencePane{};

  SECTION("loads default plugin and pie menu preferences")
  {
    auto* pluginList = listWithItems(pane, {"C:/tb/plugin_a.py", "D:/tools/plugin_b.py"});
    auto* pieMenuList = listWithItems(pane, {"Undo", "Redo"});

    REQUIRE(pluginList != nullptr);
    REQUIRE(pieMenuList != nullptr);

    CHECK(pluginList->item(0)->text() == "C:/tb/plugin_a.py");
    CHECK(pluginList->item(1)->text() == "D:/tools/plugin_b.py");
    CHECK(pieMenuList->item(0)->data(Qt::UserRole).toString() == "Menu/Edit/Undo");
    CHECK(pieMenuList->item(1)->data(Qt::UserRole).toString() == "Menu/Edit/Redo");
  }

  SECTION("reset clears custom plugin and pie menu preferences")
  {
    pane.resetToDefaults();

    CHECK(prefs.get(Preferences::DefaultPluginPaths).empty());
    CHECK(prefs.get(Preferences::PieMenuAction).empty());
    CHECK(listWithItems(pane, {"C:/tb/plugin_a.py", "D:/tools/plugin_b.py"}) == nullptr);
    CHECK(listWithItems(pane, {"Undo", "Redo"}) == nullptr);
  }
}

} // namespace tb::ui
