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

#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFixture.h"
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

void setModelEntityDefinitions(mdl::Map& map)
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
      "cycler_mdl",
      Color{},
      "mdl model entity",
      {
        {"mdl", mdl::PropertyValueTypes::String{}, "", "", false},
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
    toolBox.toggleRotateTool();
    REQUIRE(toolBox.rotateToolActive());

    toolBox.togglePathTool();

    CHECK(toolBox.pathToolActive());
    CHECK_FALSE(toolBox.rotateToolActive());
  }

  SECTION("create entity tool creates model entities from browser payloads")
  {
    setModelEntityDefinitions(map);

    REQUIRE(toolBox.createEntityTool().canCreateModelEntity("models/player.mdl"));
    REQUIRE(toolBox.createEntityTool().createModelEntity("models/player.mdl"));
    toolBox.createEntityTool().commitEntity();

    const auto selectedEntities = selectedEntityNodes(map);
    REQUIRE(selectedEntities.size() == 1u);
    CHECK(selectedEntities.front()->entity().classname() == "cycler");
    REQUIRE(selectedEntities.front()->entity().property("model") != nullptr);
    CHECK(*selectedEntities.front()->entity().property("model") == "models/player.mdl");
  }
}

} // namespace tb::ui
