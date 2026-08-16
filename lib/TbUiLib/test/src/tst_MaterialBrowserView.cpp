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
#include <QPoint>
#include <QPointF>
#include <QScrollBar>
#include <QWheelEvent>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/AppControllerFixture.h"
#include "ui/MapDocumentFixture.h"
#include "ui/MaterialBrowserView.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

void sendWheelEvent(
  MaterialBrowserView& view,
  const Qt::KeyboardModifiers modifiers,
  const QPoint& pixelDelta,
  const QPoint& angleDelta)
{
  auto event = QWheelEvent{
    QPointF{10.0, 10.0},
    QPointF{10.0, 10.0},
    pixelDelta,
    angleDelta,
    Qt::NoButton,
    modifiers,
    Qt::ScrollUpdate,
    false};
  QApplication::sendEvent(&view, &event);
}

} // namespace

TEST_CASE("MaterialBrowserView")
{
  const auto originalIconSize = pref(Preferences::MaterialBrowserIconSize);
  auto appControllerFixture = AppControllerFixture{};
  auto documentFixture = MapDocumentFixture{};
  auto& document = documentFixture.create();
  auto scrollBar = QScrollBar{Qt::Vertical};
  auto view =
    MaterialBrowserView{appControllerFixture.appController(), &scrollBar, document};
  view.resize(320, 240);

  SECTION("Ctrl+wheel steps through material thumbnail sizes")
  {
    setPref(Preferences::MaterialBrowserIconSize, 1.0f);

    sendWheelEvent(view, Qt::ControlModifier, {}, QPoint{0, 120});
    CHECK(pref(Preferences::MaterialBrowserIconSize) == 1.5f);

    sendWheelEvent(view, Qt::ControlModifier, {}, QPoint{0, -120});
    CHECK(pref(Preferences::MaterialBrowserIconSize) == 1.0f);
  }

  SECTION("ordinary wheel scrolling does not resize thumbnails")
  {
    setPref(Preferences::MaterialBrowserIconSize, 1.0f);

    sendWheelEvent(view, Qt::NoModifier, {}, QPoint{0, 120});
    CHECK(pref(Preferences::MaterialBrowserIconSize) == 1.0f);
  }

  SECTION("high resolution wheel input accumulates and respects size limits")
  {
    setPref(Preferences::MaterialBrowserIconSize, 1.0f);

    sendWheelEvent(view, Qt::ControlModifier, QPoint{0, 30}, {});
    CHECK(pref(Preferences::MaterialBrowserIconSize) == 1.0f);
    sendWheelEvent(view, Qt::ControlModifier, QPoint{0, 30}, {});
    CHECK(pref(Preferences::MaterialBrowserIconSize) == 1.5f);

    setPref(Preferences::MaterialBrowserIconSize, 3.0f);
    sendWheelEvent(view, Qt::ControlModifier, {}, QPoint{0, 120});
    CHECK(pref(Preferences::MaterialBrowserIconSize) == 3.0f);

    setPref(Preferences::MaterialBrowserIconSize, 0.25f);
    sendWheelEvent(view, Qt::ControlModifier, {}, QPoint{0, -120});
    CHECK(pref(Preferences::MaterialBrowserIconSize) == 0.25f);
  }

  setPref(Preferences::MaterialBrowserIconSize, originalIconSize);
}

} // namespace tb::ui
