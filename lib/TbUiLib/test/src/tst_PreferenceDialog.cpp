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
#include <QColor>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMetaObject>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QTableView>
#include <QTimer>
#include <QtTest/QTest>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/AppControllerFixture.h"
#include "ui/ColorsPreferencePane.h"
#include "ui/GamesPreferencePane.h"
#include "ui/KeyboardPreferencePane.h"
#include "ui/McpPreferencePane.h"
#include "ui/MiscPreferencePane.h"
#include "ui/MousePreferencePane.h"
#include "ui/PreferenceDialog.h"
#include "ui/PreferencePane.h"
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
    CHECK(dialog->objectName() == "PreferenceDialog_Dialog");

    auto* navigation = dialog->findChild<QListWidget*>("PreferenceDialog_NavigationList");
    auto* pages = dialog->findChild<QStackedWidget*>("PreferenceDialog_Pages");
    auto* pageTitle = dialog->findChild<QLabel*>("PreferenceDialog_PageTitle");
    auto* buttonBar = dialog->findChild<QWidget*>("DialogButtonBar");
    REQUIRE(navigation != nullptr);
    REQUIRE(pages != nullptr);
    REQUIRE(pageTitle != nullptr);
    REQUIRE(buttonBar != nullptr);

    auto paneNames = QStringList{};
    for (auto i = 0; i < navigation->count(); ++i)
    {
      paneNames << navigation->item(i)->text();
      navigation->setCurrentRow(i);
      CHECK(pages->currentIndex() == i);
      CHECK(pageTitle->text() == navigation->item(i)->text());
    }

    CHECK(
      paneNames
      == QStringList{
        "Games", "View", "Colors", "Mouse", "Keyboard", "Misc", "MCP", "Update"});

    navigation->setCurrentRow(0);
    QTest::keyClick(navigation, Qt::Key_Down);
    CHECK(navigation->currentRow() == 1);
    CHECK(pages->currentIndex() == 1);
  }

  SECTION("opens and closes through AppController")
  {
    auto dialogWasClosed = false;
    QTimer::singleShot(0, [&]() {
      auto* modalDialog =
        qobject_cast<PreferenceDialog*>(QApplication::activeModalWidget());
      if (modalDialog != nullptr)
      {
        dialogWasClosed = true;
        modalDialog->close();
      }
    });

    fixture.appController().showPreferences();

    CHECK(dialogWasClosed);
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

    auto* themeCombo =
      pane->findChild<QComboBox*>(QStringLiteral("ViewPreference_ThemeCombo"));
    REQUIRE(themeCombo != nullptr);
    CHECK(themeCombo->count() == 3);
    CHECK(themeCombo->itemText(0).toStdString() == Preferences::SystemTheme);
    CHECK(themeCombo->itemText(1).toStdString() == Preferences::LightTheme);
    CHECK(themeCombo->itemText(2).toStdString() == Preferences::DarkTheme);

    auto& prefs = PreferenceManager::instance();
    const auto previousTheme = prefs.get(Preferences::Theme);
    themeCombo->setCurrentIndex(1);
    REQUIRE(QMetaObject::invokeMethod(
      themeCombo, "activated", Qt::DirectConnection, Q_ARG(int, 1)));
    CHECK(prefs.getPendingValue(Preferences::Theme) == Preferences::LightTheme);
    prefs.set(Preferences::Theme, previousTheme);

    pane.reset();
  }

  SECTION("Colors")
  {
    auto pane = std::make_unique<ColorsPreferencePane>();

    auto* table = pane->findChild<QTableView*>(QStringLiteral("ColorsPreference_Table"));
    REQUIRE(table != nullptr);
    REQUIRE(table->model() != nullptr);
    REQUIRE(table->model()->rowCount() > 0);
    CHECK(table->verticalHeader()->isHidden());

    const auto swatch = table->model()
                          ->data(table->model()->index(0, 0), Qt::DecorationRole)
                          .value<QColor>();
    CHECK(swatch.isValid());
    CHECK(table->iconSize() == QSize{48, 12});

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

    auto* table =
      pane->findChild<QTableView*>(QStringLiteral("KeyboardPreference_Table"));
    REQUIRE(table != nullptr);
    auto* header = table->horizontalHeader();
    REQUIRE(header != nullptr);

    CHECK(table->verticalHeader()->isHidden());
    CHECK(header->logicalIndex(0) == 3);
    CHECK(header->sectionResizeMode(3) == QHeaderView::Stretch);
    CHECK(header->sectionResizeMode(2) == QHeaderView::Fixed);
    CHECK(header->sectionSize(2) == 190);

    auto* proxy = qobject_cast<QSortFilterProxyModel*>(table->model());
    REQUIRE(proxy != nullptr);
    CHECK(proxy->filterKeyColumn() == -1);
    CHECK(
      proxy->headerData(3, Qt::Horizontal).toString() == QStringLiteral("Description"));
    REQUIRE(proxy->rowCount() > 0);
    CHECK_FALSE(proxy->data(proxy->index(0, 3)).toString().isEmpty());

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
    auto& preferencePane = static_cast<PreferencePane&>(*pane);
    CHECK(preferencePane.canResetToDefaults());
    preferencePane.resetToDefaults();
    pane.reset();
  }

  SECTION("Update")
  {
    auto pane = std::make_unique<UpdatePreferencePane>(fixture.appController());
    pane.reset();
  }
}

} // namespace tb::ui
