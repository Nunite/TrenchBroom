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

#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "ui/CatchConfig.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/SmartModelEditor.h"
#include "ui/SmartPropertyEditorManager.h"

#include <filesystem>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("SmartModelEditor")
{
  SECTION("uses path from models directory when model is below game path")
  {
    CHECK(
      modelPathForSmartModelEditor(
        std::filesystem::path{R"(C:\Games\HalfLife\valve\models\props\crate.mdl)"},
        std::filesystem::path{R"(C:\Games\HalfLife\valve)"})
      == std::optional<std::string>{"models/props/crate.mdl"});
  }

  SECTION("strips leading directories before nested models directory")
  {
    CHECK(
      modelPathForSmartModelEditor(
        std::filesystem::path{R"(C:\Games\HalfLife\valve\custom\pack\models\barney.mdl)"},
        std::filesystem::path{R"(C:\Games\HalfLife\valve)"})
      == std::optional<std::string>{"models/barney.mdl"});
  }

  SECTION("falls back to game relative path outside models directory")
  {
    CHECK(
      modelPathForSmartModelEditor(
        std::filesystem::path{R"(C:\Games\HalfLife\valve\sprites\hud.spr)"},
        std::filesystem::path{R"(C:\Games\HalfLife\valve)"})
      == std::optional<std::string>{"sprites/hud.spr"});
  }

  SECTION("returns nothing when relative path cannot be computed")
  {
    CHECK(
      modelPathForSmartModelEditor(
        std::filesystem::path{R"(C:\Games\HalfLife\valve\models\props\crate.mdl)"},
        std::filesystem::path{})
      == std::nullopt);
  }

  SECTION("property editor manager selects model editor for model keys")
  {
    auto fixture = MapDocumentFixture{};
    auto& document = fixture.create();
    auto& map = document.map();

    auto* entityNode = new mdl::EntityNode{mdl::Entity{{
      {"classname", "monster_scientist"},
      {"model", "models/scientist.mdl"},
      {"mdl", "scientist.mdl"},
    }}};
    mdl::addNodes(map, {{&mdl::parentForNodes(map), {entityNode}}});
    mdl::selectNodes(map, {entityNode});

    auto manager = SmartPropertyEditorManager{document};
    auto* stackedLayout = qobject_cast<QStackedLayout*>(manager.layout());
    REQUIRE(stackedLayout != nullptr);

    manager.switchEditor("model", map.selection().allEntities());
    CHECK(qobject_cast<SmartModelEditor*>(stackedLayout->currentWidget()) != nullptr);
    CHECK_FALSE(manager.isDefaultEditorActive());

    manager.switchEditor("mdl", map.selection().allEntities());
    CHECK(qobject_cast<SmartModelEditor*>(stackedLayout->currentWidget()) != nullptr);
    CHECK_FALSE(manager.isDefaultEditorActive());

    manager.switchEditor("targetname", map.selection().allEntities());
    CHECK(manager.isDefaultEditorActive());
  }
}

} // namespace tb::ui
