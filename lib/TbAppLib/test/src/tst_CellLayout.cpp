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

#include "ui/CellLayout.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("CellLayout")
{
  auto layout = CellLayout{};
  layout.setWidth(200.0f);
  layout.setOuterMargin(5.0f);
  layout.setGroupMargin(6.0f);
  layout.setRowMargin(10.0f);
  layout.setCellWidth(50.0f, 50.0f);
  layout.setCellHeight(40.0f, 40.0f);

  SECTION("keeps groups without rows compact")
  {
    layout.addGroup("collapsed", 24.0f, 12u);
    layout.addGroup("next", 24.0f, 3u);

    const auto& groups = layout.groups();
    REQUIRE(groups.size() == 2u);
    CHECK(groups[0].bounds().height == 24.0f);
    CHECK(groups[1].titleBounds().top() == 30.0f);
    CHECK(groups[0].itemCount() == 12u);
    CHECK(groups[1].itemCount() == 3u);
    CHECK(layout.titleBoundsForVisibleRect(groups[0], 10.0f, 100.0f).top() == 0.0f);
  }

  SECTION("adds the content margin when a group has rows")
  {
    layout.addGroup("expanded", 24.0f, 1u);
    layout.addItem(1, "item", 40.0f, 40.0f, 50.0f, 10.0f);

    const auto& group = layout.groups().front();
    REQUIRE(group.rows().size() == 1u);
    CHECK(group.bounds().height == 84.0f);
    CHECK(group.contentBounds().top() == 34.0f);
    CHECK(group.contentBounds().height == 50.0f);
  }

  SECTION("preserves group metadata while relayouting")
  {
    layout.addGroup("collection", 24.0f, 7u);
    layout.addItem(1, "item", 40.0f, 40.0f, 50.0f, 10.0f);

    layout.setWidth(260.0f);

    const auto& group = layout.groups().front();
    CHECK(group.title() == "collection");
    CHECK(group.itemCount() == 7u);
    CHECK(group.titleBounds().width == 260.0f);
  }
}

} // namespace tb::ui
