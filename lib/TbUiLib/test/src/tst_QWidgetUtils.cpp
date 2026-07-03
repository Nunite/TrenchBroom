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

#include <QLabel>
#include <QPointer>
#include <QVBoxLayout>
#include <QWidget>

#include "ui/QWidgetUtils.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("QWidgetUtils")
{
  SECTION("deleteChildWidgetsAndDeleteLayout deletes direct child widgets immediately")
  {
    auto widget = QWidget{};
    auto* layout = new QVBoxLayout{};
    auto* label = new QLabel{"old child", &widget};
    auto labelPointer = QPointer<QLabel>{label};

    layout->addWidget(label);
    widget.setLayout(layout);

    deleteChildWidgetsAndDeleteLayout(&widget);

    CHECK(widget.layout() == nullptr);
    CHECK(labelPointer == nullptr);
    CHECK(widget.findChildren<QWidget*>("", Qt::FindDirectChildrenOnly).empty());
  }
}

} // namespace tb::ui
