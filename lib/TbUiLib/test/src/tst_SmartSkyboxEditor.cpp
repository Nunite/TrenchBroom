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

#include "mdl/EntityProperties.h"
#include "mdl/Map.h"
#include "mdl/WorldNode.h"
#include "ui/CatchConfig.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/SmartPropertyEditorManager.h"
#include "ui/SmartSkyboxEditor.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("SmartSkyboxEditor")
{
  SECTION("extracts skybox name and GoldSrc suffix")
  {
    CHECK(
      skyboxBaseAndSuffix("gfx/env/2namekrt.tga")
      == std::optional{std::pair<std::string, std::string>{"2namek", "rt"}});
    CHECK(
      skyboxBaseAndSuffix("gfx/env/desertUP.TGA")
      == std::optional{std::pair<std::string, std::string>{"desert", "up"}});
  }

  SECTION("ignores unsupported files")
  {
    CHECK(skyboxBaseAndSuffix("gfx/env/desertxx.tga") == std::nullopt);
    CHECK(skyboxBaseAndSuffix("gfx/env/desertrt.txt") == std::nullopt);
    CHECK(skyboxBaseAndSuffix("gfx/env/rt.tga") == std::nullopt);
  }

  SECTION("property editor manager leaves worldspawn skyname to default editor")
  {
    auto fixture = MapDocumentFixture{};
    auto& document = fixture.create();
    auto& map = document.map();
    auto& worldNode = map.worldNode();

    auto manager = SmartPropertyEditorManager{document};
    auto* stackedLayout = qobject_cast<QStackedLayout*>(manager.layout());
    REQUIRE(stackedLayout != nullptr);

    manager.switchEditor(
      mdl::EntityPropertyKeys::Skyname, std::vector<mdl::EntityNodeBase*>{&worldNode});
    CHECK(qobject_cast<SmartSkyboxEditor*>(stackedLayout->currentWidget()) == nullptr);
    CHECK(manager.isDefaultEditorActive());

    manager.switchEditor("targetname", std::vector<mdl::EntityNodeBase*>{&worldNode});
    CHECK(manager.isDefaultEditorActive());
  }
}

} // namespace tb::ui
