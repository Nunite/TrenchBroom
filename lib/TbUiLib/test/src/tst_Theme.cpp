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

#include <QColor>
#include <QPalette>
#include <QString>

#include "ui/Theme.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("Theme")
{
  SECTION("makeLightThemeTokens")
  {
    const auto tokens = makeLightThemeTokens();

    CHECK(tokens.windowBackground == (QColor{243, 243, 243}));
    CHECK(tokens.editorBackground == (QColor{255, 255, 255}));
    CHECK(tokens.focusBorder == (QColor{0, 120, 212}));
    CHECK(tokens.text == (QColor{31, 31, 31}));
  }

  SECTION("makeDarkThemeTokens")
  {
    const auto tokens = makeDarkThemeTokens();

    CHECK(tokens.windowBackground == (QColor{24, 24, 24}));
    CHECK(tokens.editorBackground == (QColor{31, 31, 31}));
    CHECK(tokens.focusBorder == (QColor{0, 127, 212}));
    CHECK(tokens.text == (QColor{204, 204, 204}));
  }

  SECTION("makeThemePalette")
  {
    const auto tokens = makeDarkThemeTokens();
    const auto palette = makeThemePalette(tokens);

    CHECK(palette.color(QPalette::Active, QPalette::Window) == tokens.windowBackground);
    CHECK(palette.color(QPalette::Active, QPalette::Base) == tokens.editorBackground);
    CHECK(
      palette.color(QPalette::Active, QPalette::Highlight) == tokens.selectionBackground);
    CHECK(
      palette.color(QPalette::Inactive, QPalette::Highlight)
      == tokens.inactiveSelectionBackground);
    CHECK(palette.color(QPalette::Disabled, QPalette::Text) == tokens.disabledText);
  }

  SECTION("expandThemeStyleSheet")
  {
    const auto tokens = makeDarkThemeTokens();
    auto styleSheet =
      QStringLiteral("QWidget { color: @tb-text; background: @tb-window-background; }");
    auto error = QString{};

    CHECK(expandThemeStyleSheet(styleSheet, tokens, &error));
    CHECK(error.isEmpty());
    CHECK(
      styleSheet == QStringLiteral("QWidget { color: #cccccc; background: #181818; }"));
  }

  SECTION("expandThemeStyleSheet rejects unknown tokens without changing the input")
  {
    const auto tokens = makeDarkThemeTokens();
    const auto original = QStringLiteral("QWidget { color: @tb-unknown-color; }");
    auto styleSheet = original;
    auto error = QString{};

    CHECK_FALSE(expandThemeStyleSheet(styleSheet, tokens, &error));
    CHECK(styleSheet == original);
    CHECK(error == QStringLiteral("Unknown theme token: @tb-unknown-color"));
  }
}

} // namespace tb::ui
