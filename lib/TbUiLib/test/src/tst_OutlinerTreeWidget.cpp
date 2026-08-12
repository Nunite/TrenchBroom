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
#include <QTreeWidgetItem>
#include <QtTest/QTest>

#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/WorldNode.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/outliner/OutlinerTreeWidget.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
void processOutlinerTreeUpdates()
{
  QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QApplication::processEvents();
  QApplication::processEvents();
}

std::vector<QTreeWidgetItem*> allItems(QTreeWidget& tree)
{
  auto result = std::vector<QTreeWidgetItem*>{};
  auto stack = std::vector<QTreeWidgetItem*>{};

  for (int i = 0; i < tree.topLevelItemCount(); ++i)
  {
    if (auto* item = tree.topLevelItem(i))
    {
      stack.push_back(item);
    }
  }

  while (!stack.empty())
  {
    auto* item = stack.back();
    stack.pop_back();
    result.push_back(item);

    for (int i = 0; i < item->childCount(); ++i)
    {
      if (auto* child = item->child(i))
      {
        stack.push_back(child);
      }
    }
  }

  return result;
}

QTreeWidgetItem* itemForNode(QTreeWidget& tree, const mdl::Node* node)
{
  for (auto* item : allItems(tree))
  {
    if (item->data(0, Qt::UserRole).value<mdl::Node*>() == node)
    {
      return item;
    }
  }
  return nullptr;
}

QTreeWidgetItem* itemWithText(QTreeWidget& tree, const QString& text)
{
  for (auto* item : allItems(tree))
  {
    if (item->text(0) == text)
    {
      return item;
    }
  }
  return nullptr;
}

std::vector<QString> childNames(const QTreeWidgetItem& item)
{
  auto result = std::vector<QString>{};
  for (int i = 0; i < item.childCount(); ++i)
  {
    result.push_back(item.child(i)->text(0));
  }
  return result;
}

struct OutlinerTreeFixture
{
  MapDocumentFixture fixture;
  MapDocument& document;
  mdl::Map& map;
  mdl::LayerNode* defaultLayer;
  mdl::EntityNode* lightEntity;
  mdl::EntityNode* infoEntity;
  mdl::GroupNode* group;
  mdl::EntityNode* groupedEntity;

  OutlinerTreeFixture()
    : document{fixture.create()}
    , map{document.map()}
    , defaultLayer{map.worldNode().defaultLayer()}
    , lightEntity{new mdl::EntityNode{mdl::Entity{{{"classname", "z_light"}}}}}
    , infoEntity{new mdl::EntityNode{mdl::Entity{{{"classname", "a_info_player_start"}}}}}
    , group{new mdl::GroupNode{mdl::Group{"Detail group"}}}
    , groupedEntity{new mdl::EntityNode{mdl::Entity{{{"classname", "path_corner"}}}}}
  {
    group->addChild(groupedEntity);
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {lightEntity, infoEntity, group}}});
  }
};
} // namespace

TEST_CASE("OutlinerTreeWidget")
{
  auto fixture = OutlinerTreeFixture{};
  auto& document = fixture.document;
  auto& map = fixture.map;

  auto tree = OutlinerTreeWidget{document};
  processOutlinerTreeUpdates();

  SECTION("builds layer tree with sorted node contents")
  {
    REQUIRE(tree.topLevelItemCount() == 1);

    auto* layerItem = itemForNode(tree, fixture.defaultLayer);
    REQUIRE(layerItem != nullptr);
    CHECK(layerItem->text(0) == "Default Layer");

    const auto names = childNames(*layerItem);
    REQUIRE(names.size() == 3);
    CHECK(names[0] == "Detail group");
    CHECK(names[1] == "a_info_player_start");
    CHECK(names[2] == "z_light");

    auto* groupedItem = itemForNode(tree, fixture.groupedEntity);
    REQUIRE(groupedItem != nullptr);
    CHECK(groupedItem->parent() == itemForNode(tree, fixture.group));
  }

  SECTION("filters by text and type")
  {
    tree.setFilterText("a_info");
    processOutlinerTreeUpdates();

    REQUIRE(itemForNode(tree, fixture.infoEntity) != nullptr);
    REQUIRE(itemForNode(tree, fixture.lightEntity) != nullptr);
    CHECK(!itemForNode(tree, fixture.infoEntity)->isHidden());
    CHECK(itemForNode(tree, fixture.lightEntity)->isHidden());

    tree.setFilterText("type:group");
    processOutlinerTreeUpdates();

    CHECK(!itemForNode(tree, fixture.group)->isHidden());
    CHECK(itemForNode(tree, fixture.infoEntity)->isHidden());

    tree.setFilterText("");
    processOutlinerTreeUpdates();

    CHECK(!itemForNode(tree, fixture.infoEntity)->isHidden());
    CHECK(!itemForNode(tree, fixture.lightEntity)->isHidden());
  }

  SECTION("syncs document selection into the tree")
  {
    mdl::selectNodes(map, {fixture.infoEntity});
    processOutlinerTreeUpdates();

    auto* infoItem = itemForNode(tree, fixture.infoEntity);
    REQUIRE(infoItem != nullptr);
    CHECK(infoItem->isSelected());
  }

  SECTION("syncs tree selection into the document")
  {
    auto* lightItem = itemForNode(tree, fixture.lightEntity);
    REQUIRE(lightItem != nullptr);

    tree.setCurrentItem(lightItem);
    lightItem->setSelected(true);
    processOutlinerTreeUpdates();

    CHECK(map.selection().hasOnlyEntities());
    REQUIRE(map.selection().entities.size() == 1);
    CHECK(map.selection().entities.front() == fixture.lightEntity);
  }

  SECTION("escape clears tree and document selection")
  {
    mdl::selectNodes(map, {fixture.infoEntity});
    processOutlinerTreeUpdates();
    REQUIRE(map.selection().hasAny());

    tree.setFocus();
    QTest::keyClick(&tree, Qt::Key_Escape);
    processOutlinerTreeUpdates();

    CHECK(!map.selection().hasAny());
    CHECK(tree.selectedItems().empty());
  }

  SECTION("rebuilds when map nodes are added")
  {
    auto* addedEntity = new mdl::EntityNode{mdl::Entity{{{"classname", "trigger_once"}}}};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {addedEntity}}});
    processOutlinerTreeUpdates();

    auto* addedItem = itemWithText(tree, "trigger_once");
    REQUIRE(addedItem != nullptr);
    CHECK(addedItem->data(0, Qt::UserRole).value<mdl::Node*>() == addedEntity);
  }
}

} // namespace tb::ui
