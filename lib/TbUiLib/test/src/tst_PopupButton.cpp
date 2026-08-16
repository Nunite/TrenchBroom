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
#include <QLabel>
#include <QMouseEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest/QTest>

#include "ui/PopupButton.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("PopupButton")
{
  auto popupButton = PopupButton{"View Options"};
  popupButton.resize(160, 40);

  auto* popupLayout = new QVBoxLayout{};
  popupLayout->addWidget(new QLabel{"Popup content"});
  popupButton.GetPopupWindow()->setLayout(popupLayout);

  popupButton.show();
  QApplication::processEvents();

  auto* button = popupButton.findChild<QToolButton*>();
  REQUIRE(button != nullptr);

  SECTION("a second click closes the popup")
  {
    QTest::mouseClick(button, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(popupButton.GetPopupWindow()->isVisible(), 1000);
    CHECK(button->isChecked());

    const auto localPos = QPointF{button->rect().center()};
    const auto globalPos = QPointF{button->mapToGlobal(button->rect().center())};
    auto pressEvent = QMouseEvent{
      QEvent::MouseButtonPress,
      localPos,
      globalPos,
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier};
    QApplication::sendEvent(button, &pressEvent);
    QApplication::processEvents();

    CHECK_FALSE(popupButton.GetPopupWindow()->isVisible());

    auto releaseEvent = QMouseEvent{
      QEvent::MouseButtonRelease,
      localPos,
      globalPos,
      Qt::LeftButton,
      Qt::NoButton,
      Qt::NoModifier};
    QApplication::sendEvent(button, &releaseEvent);
    QTRY_VERIFY_WITH_TIMEOUT(!popupButton.GetPopupWindow()->isVisible(), 1000);
    CHECK_FALSE(button->isChecked());

    QTest::mouseClick(button, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(popupButton.GetPopupWindow()->isVisible(), 1000);
    CHECK(button->isChecked());
  }
}

} // namespace tb::ui
