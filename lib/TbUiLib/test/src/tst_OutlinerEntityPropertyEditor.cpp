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

#include <QAbstractButton>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QtTest/QTest>

#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/outliner/OutlinerEntityPropertyEditor.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
void processOutlinerUpdates()
{
  QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QApplication::processEvents();
  QApplication::processEvents();
}

std::vector<QWidget*> propertyRows(OutlinerEntityPropertyEditor& editor)
{
  auto result = std::vector<QWidget*>{};
  for (auto* widget : editor.findChildren<QWidget*>("outlinerPropertyRow"))
  {
    if (widget->parentWidget() != nullptr)
    {
      result.push_back(widget);
    }
  }
  return result;
}

QWidget* propertyRow(OutlinerEntityPropertyEditor& editor, const QString& key)
{
  for (auto* row : propertyRows(editor))
  {
    if (const auto* keyLabel = row->findChild<QLabel*>("outlinerPropertyKey");
        keyLabel != nullptr && keyLabel->text() == key)
    {
      return row;
    }
  }
  return nullptr;
}

bool hasInfoLabel(OutlinerEntityPropertyEditor& editor, const QString& text)
{
  for (const auto* label : editor.findChildren<QLabel*>())
  {
    if (label->text() == text)
    {
      return true;
    }
  }
  return false;
}

QLineEdit* propertyValueEdit(OutlinerEntityPropertyEditor& editor, const QString& key)
{
  if (auto* row = propertyRow(editor, key))
  {
    return row->findChild<QLineEdit*>("outlinerPropertyValue");
  }
  return nullptr;
}

std::vector<QAbstractButton*> propertyRowButtons(
  OutlinerEntityPropertyEditor& editor, const QString& key)
{
  auto result = std::vector<QAbstractButton*>{};
  if (auto* row = propertyRow(editor, key))
  {
    for (auto* button : row->findChildren<QAbstractButton*>())
    {
      if (button->property("propertyKey").toString() == key)
      {
        result.push_back(button);
      }
    }
  }
  return result;
}

mdl::EntityNodeBase& selectedEntity(mdl::Map& map)
{
  const auto& entities = map.selection().allEntities();
  REQUIRE(entities.size() == 1);
  return *entities.front();
}
} // namespace

TEST_CASE("OutlinerEntityPropertyEditor")
{
  auto fixture = MapDocumentFixture{};
  auto& document = fixture.create();
  auto& map = document.map();

  auto editor = OutlinerEntityPropertyEditor{document};
  processOutlinerUpdates();

  SECTION("shows worldspawn properties without explicit entity selection")
  {
    CHECK(!hasInfoLabel(editor, "No entity selected"));

    auto* valueEdit = propertyValueEdit(editor, "classname");
    REQUIRE(valueEdit != nullptr);
    CHECK(valueEdit->text() == "worldspawn");
  }

  SECTION("updates rows from MapDocument selection notifications")
  {
    auto* entityNode = new mdl::EntityNode{mdl::Entity{{
      {"classname", "light"},
      {"targetname", "light_1"},
    }}};
    mdl::addNodes(map, {{mdl::parentForNodes(map), {entityNode}}});
    mdl::selectNodes(map, {entityNode});
    processOutlinerUpdates();

    auto* valueEdit = propertyValueEdit(editor, "targetname");
    REQUIRE(valueEdit != nullptr);
    CHECK(valueEdit->text() == "light_1");

    valueEdit->setText("renamed_light");
    emit valueEdit->editingFinished();
    processOutlinerUpdates();

    auto& entity = selectedEntity(map).entity();
    REQUIRE(entity.property("targetname") != nullptr);
    CHECK(*entity.property("targetname") == "renamed_light");
  }

  SECTION("adds and removes selected entity properties")
  {
    auto* entityNode = new mdl::EntityNode{mdl::Entity{{
      {"classname", "light"},
      {"targetname", "light_1"},
    }}};
    mdl::addNodes(map, {{mdl::parentForNodes(map), {entityNode}}});
    mdl::selectNodes(map, {entityNode});
    processOutlinerUpdates();

    auto* addKey = editor.findChild<QLineEdit*>("outlinerPropertyAddKey");
    auto* addValue = editor.findChild<QLineEdit*>("outlinerPropertyAddValue");
    REQUIRE(addKey != nullptr);
    REQUIRE(addValue != nullptr);

    addKey->setText("message");
    addValue->setText("hello");

    auto* addButton = static_cast<QAbstractButton*>(nullptr);
    for (auto* button : editor.findChildren<QAbstractButton*>())
    {
      if (button->toolTip() == "Add property")
      {
        addButton = button;
        break;
      }
    }
    REQUIRE(addButton != nullptr);

    QTest::mouseClick(addButton, Qt::LeftButton);
    processOutlinerUpdates();

    auto& addedEntity = selectedEntity(map).entity();
    REQUIRE(addedEntity.property("message") != nullptr);
    CHECK(*addedEntity.property("message") == "hello");

    addKey = editor.findChild<QLineEdit*>("outlinerPropertyAddKey");
    addValue = editor.findChild<QLineEdit*>("outlinerPropertyAddValue");
    REQUIRE(addKey != nullptr);
    REQUIRE(addValue != nullptr);
    CHECK(addKey->text().isEmpty());
    CHECK(addValue->text().isEmpty());

    auto* removeButton = static_cast<QAbstractButton*>(nullptr);
    for (auto* button : propertyRowButtons(editor, "message"))
    {
      if (!button->isHidden())
      {
        removeButton = button;
      }
    }
    REQUIRE(removeButton != nullptr);
    QTest::mouseClick(removeButton, Qt::LeftButton);
    processOutlinerUpdates();

    CHECK(selectedEntity(map).entity().property("message") == nullptr);
    CHECK(propertyRow(editor, "message") == nullptr);
  }
}

} // namespace tb::ui
