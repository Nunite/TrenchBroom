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

#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "ui/AppController.h"
#include "ui/AppControllerFixture.h"
#include "ui/MiscPreferencePane.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("MiscPreferencePane")
{
  auto appControllerFixture = AppControllerFixture{};
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::Language, Preferences::languageChinese());
  prefs.set(Preferences::PrefixWorldspawnHeaderOnCopy, true);
  prefs.set(Preferences::Enable2DBoxSelection, true);
  prefs.set(Preferences::PythonPluginDirectories, "C:/tb/plugin");
  prefs.set(Preferences::PieMenuAction, "Menu/Edit/Undo|Menu/Edit/Redo");
  prefs.set(Preferences::PrefabDirectory, "C:/tb/prefabs");

  auto pane = MiscPreferencePane{appControllerFixture.appController()};

  SECTION("loads general misc preferences")
  {
    auto* prefixCheckBox = static_cast<QCheckBox*>(nullptr);
    auto* boxSelectionCheckBox = static_cast<QCheckBox*>(nullptr);
    for (auto* checkBox : pane.findChildren<QCheckBox*>())
    {
      if (checkBox->text() == QStringLiteral("Prefix worldspawn header on copy"))
      {
        prefixCheckBox = checkBox;
      }
      if (checkBox->text() == QStringLiteral("Enable 2D box selection with Ctrl+drag"))
      {
        boxSelectionCheckBox = checkBox;
      }
    }

    REQUIRE(prefixCheckBox != nullptr);
    CHECK(prefixCheckBox->isChecked());
    REQUIRE(boxSelectionCheckBox != nullptr);
    CHECK(boxSelectionCheckBox->isChecked());

    auto foundChinese = false;
    for (auto* radioButton : pane.findChildren<QRadioButton*>())
    {
      if (radioButton->text() == QString::fromStdString(Preferences::languageChinese()))
      {
        foundChinese = radioButton->isChecked();
      }
    }
    CHECK(foundChinese);
  }

  SECTION("reset only resets lightweight misc preferences")
  {
    pane.resetToDefaults();

    CHECK(prefs.get(Preferences::Language) == Preferences::languageEnglish());
    CHECK_FALSE(prefs.get(Preferences::PrefixWorldspawnHeaderOnCopy));
    CHECK_FALSE(prefs.get(Preferences::Enable2DBoxSelection));
    CHECK(prefs.get(Preferences::PythonPluginDirectories) == "C:/tb/plugin");
    CHECK(prefs.get(Preferences::PieMenuAction) == "Menu/Edit/Undo|Menu/Edit/Redo");
    CHECK(prefs.get(Preferences::PrefabDirectory).empty());
  }

  SECTION("loads prefab directory preference")
  {
    auto* prefabDirectoryEdit = pane.findChild<QLineEdit*>("prefabDirectoryEdit");

    REQUIRE(prefabDirectoryEdit != nullptr);
    CHECK(prefabDirectoryEdit->text() == QStringLiteral("C:/tb/prefabs"));
  }

  SECTION("reset clears prefab directory preference")
  {
    pane.resetToDefaults();

    CHECK(prefs.get(Preferences::PrefabDirectory).empty());
  }

  SECTION("links to separate management dialogs")
  {
    auto foundPieMenuButton = false;
    auto foundPluginButton = false;

    for (auto* button : pane.findChildren<QPushButton*>())
    {
      foundPieMenuButton =
        foundPieMenuButton || button->text() == QStringLiteral("Pie Menu Settings...");
      foundPluginButton =
        foundPluginButton || button->text() == QStringLiteral("Python Plugin Manager...");
    }

    CHECK(foundPieMenuButton);
    CHECK(foundPluginButton);
  }
}

} // namespace tb::ui
