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
#include <QtTest/QTest>

#include "ui/TabBar.h"
#include "ui/TabBook.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

TabBarButton* tabButton(TabBook& tabBook, const QString& accessibleName)
{
  for (auto* button : tabBook.findChildren<TabBarButton*>())
  {
    if (button->accessibleName() == accessibleName)
    {
      return button;
    }
  }
  return nullptr;
}

void addTestPages(TabBook& tabBook)
{
  tabBook.addPage(new TabBookPage{}, "First");
  tabBook.addPage(new TabBookPage{}, "Second");
}

} // namespace

TEST_CASE("TabBook")
{
  auto tabBook = TabBook{};
  addTestPages(tabBook);
  tabBook.resize(400, 240);
  tabBook.show();
  QApplication::processEvents();

  auto* firstButton = tabButton(tabBook, "First");
  auto* secondButton = tabButton(tabBook, "Second");
  REQUIRE(firstButton != nullptr);
  REQUIRE(secondButton != nullptr);

  SECTION("switches pages with mouse and keyboard")
  {
    CHECK(tabBook.currentPageIndex() == 0);
    CHECK(firstButton->property("active").toBool());

    secondButton->setFocus(Qt::OtherFocusReason);
    REQUIRE(secondButton->hasFocus());
    QTest::keyClick(secondButton, Qt::Key_Space);

    CHECK(tabBook.currentPageIndex() == 1);
    CHECK(secondButton->property("active").toBool());
    CHECK_FALSE(firstButton->property("active").toBool());

    QTest::mouseClick(firstButton, Qt::LeftButton);
    CHECK(tabBook.currentPageIndex() == 0);
  }

  SECTION("restores the current page")
  {
    tabBook.switchToPage(1);
    const auto state = tabBook.saveState();

    auto restoredTabBook = TabBook{};
    addTestPages(restoredTabBook);
    REQUIRE(restoredTabBook.restoreState(state));
    CHECK(restoredTabBook.currentPageIndex() == 1);
  }
}

} // namespace tb::ui
