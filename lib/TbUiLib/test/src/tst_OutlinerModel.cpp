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

#include <QMimeData>

#include "base/Logger.h"
#include "gl/ResourceManager.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFixture.h"
#include "mdl/Map_NodeLocking.h"
#include "mdl/Map_NodeVisibility.h"
#include "mdl/Map_Nodes.h"
#include "mdl/WorldNode.h"
#include "ui/CatchConfig.h"
#include "ui/outliner/OutlinerModel.h"

#include "kd/task_manager.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

namespace
{
struct OutlinerModelFixture
{
  mdl::MapFixture mapFixture;
  mdl::Map& map;
  mdl::LayerNode* defaultLayer;
  mdl::EntityNode* lightEntity;
  mdl::EntityNode* infoEntity;
  mdl::GroupNode* group;
  mdl::EntityNode* groupedEntity;

  OutlinerModelFixture()
    : map{mapFixture.create()}
    , defaultLayer{map.worldNode().defaultLayer()}
    , lightEntity{new mdl::EntityNode{mdl::Entity{{{"classname", "light"}}}}}
    , infoEntity{new mdl::EntityNode{mdl::Entity{{{"classname", "info_player_start"}}}}}
    , group{new mdl::GroupNode{mdl::Group{"Detail group"}}}
    , groupedEntity{new mdl::EntityNode{mdl::Entity{{{"classname", "path_corner"}}}}}
  {
    group->addChild(groupedEntity);
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {lightEntity, infoEntity, group}}});
  }
};
} // namespace

TEST_CASE("OutlinerModel")
{
  auto fixture = OutlinerModelFixture{};
  auto& map = fixture.map;
  auto model = OutlinerModel{map};

  SECTION("exposes world layer children")
  {
    CHECK(model.columnCount() == 3);
    CHECK(model.headerData(0, Qt::Horizontal).toString() == "Name");
    CHECK(model.headerData(1, Qt::Horizontal).toString() == "Vis");
    CHECK(model.headerData(2, Qt::Horizontal).toString() == "Lock");

    REQUIRE(model.rowCount() == 1);
    CHECK(model.nodeFromIndex({}) == &map.worldNode());

    const auto layerIndex = model.index(0, 0);
    REQUIRE(layerIndex.isValid());
    CHECK(model.nodeFromIndex(layerIndex) == fixture.defaultLayer);
    REQUIRE(model.rowCount(layerIndex) == 3);

    const auto lightIndex = model.index(0, 0, layerIndex);
    const auto infoIndex = model.index(1, 0, layerIndex);
    const auto groupIndex = model.index(2, 0, layerIndex);

    REQUIRE(lightIndex.isValid());
    REQUIRE(infoIndex.isValid());
    REQUIRE(groupIndex.isValid());

    CHECK(model.data(lightIndex).toString() == "light");
    CHECK(model.data(infoIndex).toString() == "info_player_start");
    CHECK(model.data(groupIndex).toString() == "Detail group");
    CHECK(model.nodeFromIndex(lightIndex) == fixture.lightEntity);
    CHECK(model.nodeFromIndex(infoIndex) == fixture.infoEntity);
    CHECK(model.nodeFromIndex(groupIndex) == fixture.group);
    CHECK(model.parent(lightIndex) == layerIndex);
  }

  SECTION("maps nested indexes to nodes and parents")
  {
    const auto groupIndex = model.indexFromNode(fixture.group);
    REQUIRE(groupIndex.isValid());
    REQUIRE(model.rowCount(groupIndex) == 1);

    const auto groupedEntityIndex = model.index(0, 0, groupIndex);
    REQUIRE(groupedEntityIndex.isValid());

    CHECK(model.data(groupedEntityIndex).toString() == "path_corner");
    CHECK(model.nodeFromIndex(groupedEntityIndex) == fixture.groupedEntity);
    CHECK(model.parent(groupedEntityIndex) == groupIndex);
    CHECK(model.indexFromNode(fixture.groupedEntity) == groupedEntityIndex);
    CHECK_FALSE(model.indexFromNode(&map.worldNode()).isValid());
  }

  SECTION("updates display data after node changes")
  {
    const auto lightIndex = model.indexFromNode(fixture.lightEntity);
    REQUIRE(lightIndex.isValid());

    fixture.lightEntity->setEntity(mdl::Entity{{{"classname", "light_spot"}}});
    const auto changedNodes = std::vector<mdl::Node*>{fixture.lightEntity};
    map.nodesDidChangeNotifier(changedNodes);

    CHECK(model.data(lightIndex).toString() == "light_spot");
  }

  SECTION("tracks visibility and lock state columns")
  {
    const auto lightIndex = model.indexFromNode(fixture.lightEntity);
    REQUIRE(lightIndex.isValid());

    const auto visibleIcon =
      model.data(lightIndex.sibling(lightIndex.row(), 1), Qt::DecorationRole);
    const auto unlockedIcon =
      model.data(lightIndex.sibling(lightIndex.row(), 2), Qt::DecorationRole);
    CHECK(visibleIcon.canConvert<QIcon>());
    CHECK(unlockedIcon.canConvert<QIcon>());

    mdl::hideNodes(map, {fixture.lightEntity});
    mdl::lockNodes(map, {fixture.lightEntity});

    const auto hiddenIcon =
      model.data(lightIndex.sibling(lightIndex.row(), 1), Qt::DecorationRole);
    const auto lockedIcon =
      model.data(lightIndex.sibling(lightIndex.row(), 2), Qt::DecorationRole);
    CHECK(hiddenIcon.canConvert<QIcon>());
    CHECK(lockedIcon.canConvert<QIcon>());
    CHECK(hiddenIcon.value<QIcon>().cacheKey() != visibleIcon.value<QIcon>().cacheKey());
    CHECK(lockedIcon.value<QIcon>().cacheKey() != unlockedIcon.value<QIcon>().cacheKey());
  }

  SECTION("creates drag mime data with unique nodes")
  {
    const auto lightIndex = model.indexFromNode(fixture.lightEntity);
    REQUIRE(lightIndex.isValid());

    const auto mimeData =
      std::unique_ptr<QMimeData>{model.mimeData({lightIndex, lightIndex})};

    REQUIRE(mimeData);
    CHECK(mimeData->hasFormat("application/x-trenchbroom-nodes"));
    CHECK(
      mimeData->data("application/x-trenchbroom-nodes").size() == int{sizeof(quint64)});
  }

  SECTION("drops nodes onto a new parent")
  {
    const auto lightIndex = model.indexFromNode(fixture.lightEntity);
    const auto groupIndex = model.indexFromNode(fixture.group);
    REQUIRE(lightIndex.isValid());
    REQUIRE(groupIndex.isValid());

    const auto mimeData = std::unique_ptr<QMimeData>{model.mimeData({lightIndex})};
    REQUIRE(mimeData);

    CHECK(model.dropMimeData(mimeData.get(), Qt::MoveAction, -1, -1, groupIndex));
    CHECK(fixture.lightEntity->parent() == fixture.group);
    CHECK(model.rowCount(groupIndex) == 2);
    CHECK(
      model.parent(model.indexFromNode(fixture.lightEntity)).internalPointer()
      == fixture.group);
  }
}

} // namespace tb::ui
