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

#include "base/Logger.h"
#include "gl/ResourceManager.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFixture.h"
#include "mdl/Map_Selection.h"
#include "mdl/WorldNode.h"
#include "ui/CatchConfig.h"
#include "ui/InputEvent.h"
#include "ui/InputState.h"
#include "ui/PathTool.h"
#include "ui/PathToolController.h"

#include "kd/task_manager.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

namespace tb::ui
{
using namespace Catch::Matchers;

namespace
{
auto makePathToolMap()
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create();
  map.entityDefinitionManager().setDefinitions({
    {
      "path_corner",
      Color{},
      "path node",
      {},
      mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}},
    },
  });

  return std::tuple{std::move(fixture), std::ref(map)};
}

std::vector<mdl::EntityNode*> selectedEntityNodes(mdl::Map& map)
{
  auto result = std::vector<mdl::EntityNode*>{};
  for (auto* node : map.selection().nodes)
  {
    if (auto* entityNode = dynamic_cast<mdl::EntityNode*>(node))
    {
      result.push_back(entityNode);
    }
  }
  return result;
}

std::optional<std::string> propertyValue(
  const mdl::EntityNode& entityNode, const std::string& key)
{
  if (const auto* value = entityNode.entity().property(key))
  {
    return *value;
  }
  return std::nullopt;
}
} // namespace

TEST_CASE("PathTool")
{
  auto [fixture, mapRef] = makePathToolMap();
  auto& map = mapRef.get();

  SECTION("manages points and redo stack")
  {
    auto tool = PathTool{map};

    CHECK_FALSE(tool.hasPoints());
    CHECK_FALSE(tool.canRedoPoint());

    tool.addPoint({1.0, 2.0, 3.0});
    tool.addPoint({4.0, 5.0, 6.0});

    CHECK(tool.hasPoints());
    CHECK_THAT(
      tool.points(), Equals(std::vector<vm::vec3d>{{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}}));
    CHECK_FALSE(tool.canRedoPoint());

    tool.removeLastPoint();

    CHECK_THAT(tool.points(), Equals(std::vector<vm::vec3d>{{1.0, 2.0, 3.0}}));
    CHECK(tool.canRedoPoint());

    tool.redoLastPoint();

    CHECK_THAT(
      tool.points(), Equals(std::vector<vm::vec3d>{{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}}));
    CHECK_FALSE(tool.canRedoPoint());

    tool.removeLastPoint();
    tool.addPoint({7.0, 8.0, 9.0});

    CHECK_THAT(
      tool.points(), Equals(std::vector<vm::vec3d>{{1.0, 2.0, 3.0}, {7.0, 8.0, 9.0}}));
    CHECK_FALSE(tool.canRedoPoint());

    tool.clearPoints();

    CHECK_FALSE(tool.hasPoints());
    CHECK_FALSE(tool.canRedoPoint());
    CHECK(tool.points().empty());
  }

  SECTION("creates selected path_corner chain")
  {
    auto tool = PathTool{map};

    tool.addPoint({64.0, 0.0, 0.0});
    tool.addPoint({128.0, 0.0, 0.0});
    tool.addPoint({128.0, 64.0, 0.0});

    tool.createPathEntities();

    CHECK(tool.points().empty());
    CHECK_FALSE(tool.canRedoPoint());

    const auto entityNodes = selectedEntityNodes(map);
    REQUIRE(entityNodes.size() == 3u);
    CHECK(map.worldNode().defaultLayer()->children().size() == 3u);

    CHECK(entityNodes[0]->entity().classname() == "path_corner");
    CHECK(entityNodes[0]->entity().origin() == vm::vec3d{64.0, 0.0, 0.0});
    const auto firstTargetName = propertyValue(*entityNodes[0], "targetname");
    const auto firstTarget = propertyValue(*entityNodes[0], "target");
    REQUIRE(firstTargetName);
    REQUIRE(firstTarget);

    CHECK(entityNodes[1]->entity().classname() == "path_corner");
    CHECK(entityNodes[1]->entity().origin() == vm::vec3d{128.0, 0.0, 0.0});
    const auto secondTargetName = propertyValue(*entityNodes[1], "targetname");
    const auto secondTarget = propertyValue(*entityNodes[1], "target");
    REQUIRE(secondTargetName);
    REQUIRE(secondTarget);
    CHECK(*firstTarget == *secondTargetName);

    CHECK(entityNodes[2]->entity().classname() == "path_corner");
    CHECK(entityNodes[2]->entity().origin() == vm::vec3d{128.0, 64.0, 0.0});
    const auto thirdTargetName = propertyValue(*entityNodes[2], "targetname");
    REQUIRE(thirdTargetName);
    CHECK(*secondTarget == *thirdTargetName);
    CHECK_FALSE(propertyValue(*entityNodes[2], "target"));

    map.undoCommand();

    CHECK(map.worldNode().defaultLayer()->children().empty());
    CHECK_FALSE(map.selection().hasNodes());

    map.redoCommand();

    CHECK(map.worldNode().defaultLayer()->children().size() == 3u);
    CHECK(selectedEntityNodes(map).size() == 3u);
  }

  SECTION("does not create entities without path_corner definition")
  {
    map.entityDefinitionManager().setDefinitions({});

    auto tool = PathTool{map};
    tool.addPoint({64.0, 0.0, 0.0});

    tool.createPathEntities();

    CHECK(map.worldNode().defaultLayer()->children().empty());
    CHECK_THAT(tool.points(), Equals(std::vector<vm::vec3d>{{64.0, 0.0, 0.0}}));
  }

  SECTION("controller handles keyboard shortcuts while active")
  {
    auto tool = PathTool{map};
    auto controller = PathToolController{tool};
    auto inputState = InputState{0.0f, 0.0f};

    tool.activate();
    tool.addPoint({64.0, 0.0, 0.0});
    tool.addPoint({128.0, 0.0, 0.0});

    CHECK(controller.keyPress(inputState, {KeyEvent::Type::Down, Qt::Key_Left}));
    CHECK_THAT(tool.points(), Equals(std::vector<vm::vec3d>{{64.0, 0.0, 0.0}}));
    CHECK(tool.canRedoPoint());

    CHECK(controller.keyPress(inputState, {KeyEvent::Type::Down, Qt::Key_Right}));
    CHECK_THAT(
      tool.points(), Equals(std::vector<vm::vec3d>{{64.0, 0.0, 0.0}, {128.0, 0.0, 0.0}}));
    CHECK_FALSE(tool.canRedoPoint());

    CHECK(controller.keyPress(inputState, {KeyEvent::Type::Down, Qt::Key_Return}));
    CHECK(tool.points().empty());
    CHECK(map.worldNode().defaultLayer()->children().size() == 2u);
    CHECK(selectedEntityNodes(map).size() == 2u);

    CHECK(controller.keyPress(inputState, {KeyEvent::Type::Down, Qt::Key_Escape}));
    CHECK_FALSE(tool.active());
  }
}

} // namespace tb::ui
