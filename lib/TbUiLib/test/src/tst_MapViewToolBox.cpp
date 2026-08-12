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

#include <QStackedLayout>
#include <QWidget>

#include "mdl/Entity.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFixture.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/PropertyDefinition.h"
#include "mdl/WorldNode.h"
#include "ui/CatchConfig.h"
#include "ui/CreateEntityTool.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/MapViewToolBox.h"
#include "ui/PathTool.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

namespace tb::ui
{
using namespace Catch::Matchers;

namespace
{
void setPathCornerDefinition(mdl::Map& map)
{
  map.entityDefinitionManager().setDefinitions({
    {
      "path_corner",
      Color{},
      "path node",
      {},
      mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}},
    },
  });
}

void setAssetEntityDefinitions(mdl::Map& map)
{
  map.entityDefinitionManager().setDefinitions({
    {
      "path_corner",
      Color{},
      "path node",
      {},
      mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}},
    },
    {
      "cycler",
      Color{},
      "model entity",
      {
        {"model", mdl::PropertyValueTypes::String{}, "", "", false},
      },
      mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}},
    },
    {
      "cycler_sprite",
      Color{},
      "preferred model entity",
      {
        {"model", mdl::PropertyValueTypes::String{}, "", "", false},
      },
      mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}},
    },
    {
      "cycler_mdl",
      Color{},
      "mdl model entity",
      {
        {"mdl", mdl::PropertyValueTypes::String{}, "", "", false},
      },
      mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}},
    },
    {
      "env_sprite",
      Color{},
      "sprite entity",
      {
        {"model", mdl::PropertyValueTypes::String{}, "", "", false},
      },
      mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}},
    },
    {
      "ambient_generic",
      Color{},
      "sound entity",
      {
        {"message", mdl::PropertyValueTypes::String{}, "", "", false},
        {"noise", mdl::PropertyValueTypes::String{}, "", "", false},
        {"sound", mdl::PropertyValueTypes::String{}, "", "", false},
      },
      mdl::PointEntityDefinition{vm::bbox3d{16.0}, {}, {}},
    },
  });
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
} // namespace

TEST_CASE("MapViewToolBox")
{
  auto fixture = MapDocumentFixture{};
  auto& document = fixture.create();
  auto& map = document.map();
  setPathCornerDefinition(map);

  auto parent = QWidget{};
  auto bookCtrl = QStackedLayout{&parent};
  auto toolBox = MapViewToolBox{document, &bookCtrl};

  SECTION("path tool entry toggles and creates path entities")
  {
    CHECK_FALSE(toolBox.pathToolActive());

    toolBox.togglePathTool();

    CHECK(toolBox.pathToolActive());

    toolBox.pathTool().addPoint({64.0, 0.0, 0.0});
    toolBox.pathTool().addPoint({128.0, 0.0, 0.0});
    toolBox.pathTool().addPoint({128.0, 64.0, 0.0});

    toolBox.removeLastPathPoint();

    CHECK_THAT(
      toolBox.pathTool().points(),
      Equals(std::vector<vm::vec3d>{{64.0, 0.0, 0.0}, {128.0, 0.0, 0.0}}));

    toolBox.performPathCreation();

    CHECK(toolBox.pathTool().points().empty());
    CHECK(toolBox.pathToolActive());
    CHECK(map.worldNode().defaultLayer()->children().size() == 2u);
    CHECK(selectedEntityNodes(map).size() == 2u);

    toolBox.togglePathTool();

    CHECK_FALSE(toolBox.pathToolActive());
  }

  SECTION("path tool is exclusive with other modal tools")
  {
    auto* entityNode = new mdl::EntityNode{mdl::Entity{{{"classname", "path_corner"}}}};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {entityNode}}});
    mdl::selectNodes(map, {entityNode});

    toolBox.toggleRotateTool();
    REQUIRE(toolBox.rotateToolActive());

    toolBox.togglePathTool();

    CHECK(toolBox.pathToolActive());
    CHECK_FALSE(toolBox.rotateToolActive());
  }

  SECTION("create entity tool creates model entities from browser payloads")
  {
    setAssetEntityDefinitions(map);

    REQUIRE(toolBox.createEntityTool().canCreateModelEntity("models/player.mdl"));
    REQUIRE(toolBox.createEntityTool().createModelEntity("models/player.mdl"));
    toolBox.createEntityTool().commitEntity();

    const auto selectedEntities = selectedEntityNodes(map);
    REQUIRE(selectedEntities.size() == 1u);
    CHECK(selectedEntities.front()->entity().classname() == "cycler_sprite");
    REQUIRE(selectedEntities.front()->entity().property("model") != nullptr);
    CHECK(*selectedEntities.front()->entity().property("model") == "models/player.mdl");
  }

  SECTION("create entity tool creates sprite entities from browser payloads")
  {
    setAssetEntityDefinitions(map);

    REQUIRE(toolBox.createEntityTool().canCreateSpriteEntity("sprites/glow01.spr"));
    REQUIRE(toolBox.createEntityTool().createSpriteEntity("sprites/glow01.spr"));
    toolBox.createEntityTool().commitEntity();

    const auto selectedEntities = selectedEntityNodes(map);
    REQUIRE(selectedEntities.size() == 1u);
    CHECK(selectedEntities.front()->entity().classname() == "cycler_sprite");
    REQUIRE(selectedEntities.front()->entity().property("model") != nullptr);
    CHECK(*selectedEntities.front()->entity().property("model") == "sprites/glow01.spr");
  }

  SECTION("create entity tool creates sound entities from browser payloads")
  {
    setAssetEntityDefinitions(map);

    REQUIRE(toolBox.createEntityTool().canCreateSoundEntity("sound/ambience/hum.wav"));
    REQUIRE(toolBox.createEntityTool().createSoundEntity("sound/ambience/hum.wav"));
    toolBox.createEntityTool().commitEntity();

    const auto selectedEntities = selectedEntityNodes(map);
    REQUIRE(selectedEntities.size() == 1u);
    CHECK(selectedEntities.front()->entity().classname() == "ambient_generic");
    REQUIRE(selectedEntities.front()->entity().property("message") != nullptr);
    CHECK(
      *selectedEntities.front()->entity().property("message")
      == "sound/ambience/hum.wav");
  }
}

} // namespace tb::ui
