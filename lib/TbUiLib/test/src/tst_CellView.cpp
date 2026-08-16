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

#include <QScrollBar>
#include <QtTest/QTest>

#include "gl/GlInterface.h"
#include "ui/AppControllerFixture.h"
#include "ui/CellView.h"
#include "ui/InputEvent.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

class TestCellView : public CellView
{
private:
  bool m_collapsed = false;
  size_t m_toggleCount = 0u;

public:
  TestCellView(AppController& appController, QScrollBar* scrollBar)
    : CellView{appController, scrollBar}
  {
  }

  bool collapsed() const { return m_collapsed; }
  size_t toggleCount() const { return m_toggleCount; }

private:
  void doInitLayout(Layout& layout) override
  {
    layout.setWidth(float(width()));
    layout.setGroupMargin(6.0f);
    layout.setRowMargin(8.0f);
    layout.setCellWidth(50.0f, 50.0f);
    layout.setCellHeight(40.0f, 40.0f);
  }

  void doReloadLayout(Layout& layout) override
  {
    layout.addGroup("Group", 24.0f, 3u);
    if (!m_collapsed)
    {
      layout.addItem(1, "Item", 40.0f, 40.0f, 50.0f, 10.0f);
    }
    layout.addGroup("Other", 24.0f, 1u);
    layout.addItem(2, "Other item", 40.0f, 40.0f, 50.0f, 10.0f);
  }

  void doRender(gl::Gl&, Layout&, float, float) override {}

  bool isGroupCollapsible(const Group&) const override { return true; }
  bool isGroupCollapsed(const Group& group) const override
  {
    return group.title() == "Group" && m_collapsed;
  }

  void doToggleGroup(const Group&) override
  {
    m_collapsed = !m_collapsed;
    ++m_toggleCount;
    invalidate();
  }

  bool shouldRenderFocusIndicator() const override { return false; }
};

} // namespace

TEST_CASE("CellView")
{
  auto appControllerFixture = AppControllerFixture{};
  auto scrollBar = QScrollBar{Qt::Vertical};
  auto view = TestCellView{appControllerFixture.appController(), &scrollBar};
  view.resize(200, 160);

  SECTION("toggles a group by clicking its complete title row")
  {
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, QPoint{100, 12});

    CHECK(view.collapsed());
    CHECK(view.toggleCount() == 1u);
  }

  SECTION("supports keyboard group navigation and disclosure keys")
  {
    view.processEvent(KeyEvent{KeyEvent::Type::Down, Qt::Key_Down});
    view.processEvent(KeyEvent{KeyEvent::Type::Down, Qt::Key_Space});
    CHECK(view.collapsed());

    view.processEvent(KeyEvent{KeyEvent::Type::Down, Qt::Key_Right});
    CHECK_FALSE(view.collapsed());

    view.processEvent(KeyEvent{KeyEvent::Type::Down, Qt::Key_Left});
    CHECK(view.collapsed());

    view.processEvent(KeyEvent{KeyEvent::Type::Down, Qt::Key_Return});
    CHECK_FALSE(view.collapsed());
    CHECK(view.toggleCount() == 4u);
  }

  SECTION("keeps a sticky title anchored while collapsing")
  {
    view.resize(200, 100);
    view.processEvent(KeyEvent{KeyEvent::Type::Down, Qt::Key_Down});
    scrollBar.setValue(20);
    REQUIRE(scrollBar.value() == 20);

    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, QPoint{100, 12});

    CHECK(view.collapsed());
    CHECK(scrollBar.value() == 0);
  }
}

} // namespace tb::ui
