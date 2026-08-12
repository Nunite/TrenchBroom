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
#include <QMouseEvent>
#include <QPoint>
#include <QtTest/QTest>

#include "ui/Action.h"
#include "ui/PieMenu.h"

#include <filesystem>
#include <unordered_map>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

void sendMouseMove(QWidget& widget, const QPoint& pos)
{
  auto event = QMouseEvent{
    QEvent::MouseMove,
    QPointF{pos},
    QPointF{widget.mapToGlobal(pos)},
    Qt::NoButton,
    Qt::NoButton,
    Qt::NoModifier};
  QApplication::sendEvent(&widget, &event);
}

void sendMouseRelease(QWidget& widget, const QPoint& pos)
{
  auto event = QMouseEvent{
    QEvent::MouseButtonRelease,
    QPointF{pos},
    QPointF{widget.mapToGlobal(pos)},
    Qt::LeftButton,
    Qt::LeftButton,
    Qt::NoModifier};
  QApplication::sendEvent(&widget, &event);
}

} // namespace

TEST_CASE("PieMenu")
{
  SECTION("maps angles to wedge indexes")
  {
    CHECK_FALSE(pieMenuIndexForAngle(0.0, 0u));

    CHECK(pieMenuIndexForAngle(90.0, 4u) == 0u);
    CHECK(pieMenuIndexForAngle(180.0, 4u) == 1u);
    CHECK(pieMenuIndexForAngle(270.0, 4u) == 2u);
    CHECK(pieMenuIndexForAngle(0.0, 4u) == 3u);

    CHECK(pieMenuIndexForAngle(44.9, 4u) == 3u);
    CHECK(pieMenuIndexForAngle(45.0, 4u) == 0u);
    CHECK(pieMenuIndexForAngle(134.9, 4u) == 0u);
    CHECK(pieMenuIndexForAngle(135.0, 4u) == 1u);
    CHECK(pieMenuIndexForAngle(315.0, 4u) == 3u);

    CHECK(pieMenuIndexForAngle(-90.0, 4u) == 2u);
    CHECK(pieMenuIndexForAngle(450.0, 4u) == 0u);
  }

  SECTION("builds items from configured action paths")
  {
    auto actions = std::unordered_map<std::filesystem::path, Action, kdl::path_hash>{};

    auto executedCount = 0;
    auto makeAction =
      [&](const std::string& path, const std::string& label, const bool enabled) {
        return Action{
          std::filesystem::path{path},
          label,
          ActionContext::Any,
          [](auto&) {},
          [enabled](const auto&) { return enabled; }};
      };

    actions.emplace(
      std::filesystem::path{"Menu/Edit/Undo"},
      makeAction("Menu/Edit/Undo", "Undo", true));
    actions.emplace(
      std::filesystem::path{"Menu/Edit/Redo"},
      makeAction("Menu/Edit/Redo", "Redo", false));

    const auto items = buildPieMenuItems(
      "Menu/Edit/Undo||Missing/Action|Menu/Edit/Redo|Menu/Edit/Undo",
      actions,
      [](const auto& action) { return action.preference().path != "Menu/Edit/Redo"; },
      [&](const Action&) { ++executedCount; });

    REQUIRE(items.size() == 3u);
    CHECK(items[0].label == "Undo");
    CHECK(items[0].enabled);
    CHECK(items[1].label == "Redo");
    CHECK_FALSE(items[1].enabled);
    CHECK(items[2].label == "Undo");
    CHECK(items[2].enabled);

    items[0].action();
    items[2].action();
    CHECK(executedCount == 2);
  }

  SECTION("executes hovered enabled item on mouse release")
  {
    auto menu = PieMenu{};
    auto executed = false;

    menu.addItem("One", [&]() { executed = true; });
    menu.addItem("Two", []() {});
    menu.showAt({300, 300});
    CHECK(QTest::qWaitForWindowExposed(&menu));

    const auto center = QPoint{menu.width() / 2, menu.height() / 2};
    sendMouseMove(menu, center + QPoint{0, -80});
    sendMouseRelease(menu, center + QPoint{0, -80});

    CHECK(executed);
    CHECK_FALSE(menu.isVisible());
  }

  SECTION("does not execute hovered disabled item on mouse release")
  {
    auto menu = PieMenu{};
    auto executed = false;

    menu.addItem("Disabled", [&]() { executed = true; }, false);
    menu.showAt({300, 300});
    CHECK(QTest::qWaitForWindowExposed(&menu));

    const auto center = QPoint{menu.width() / 2, menu.height() / 2};
    sendMouseMove(menu, center + QPoint{0, -80});
    sendMouseRelease(menu, center + QPoint{0, -80});

    CHECK_FALSE(executed);
    CHECK_FALSE(menu.isVisible());
  }
}

} // namespace tb::ui
