/*
 Copyright (C) 2026

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

#include "Logger.h"
#include "TestLogger.h"
#include "fs/TestEnvironment.h"
#include "ui/InputState.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/PrefabAsset.h"
#include "ui/PrefabTool.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

const auto BrushPrefabText = std::string{R"(
{
( -0 -0 -16 ) ( -0 -0  -0 ) ( 64 -0 -16 ) tex1 1 2 3 4 5
( -0 -0 -16 ) ( -0 64 -16 ) ( -0 -0  -0 ) tex2 0 0 0 1 1
( -0 -0 -16 ) ( 64 -0 -16 ) ( -0 64 -16 ) tex3 0 0 0 1 1
( 64 64  -0 ) ( -0 64  -0 ) ( 64 64 -16 ) tex4 0 0 0 1 1
( 64 64  -0 ) ( 64 64 -16 ) ( 64 -0  -0 ) tex5 0 0 0 1 1
( 64 64  -0 ) ( 64 -0  -0 ) ( -0 64  -0 ) tex6 0 0 0 1 1
})"};

const auto SavedPrefabText = std::string{R"(// entity 0
{
"classname" "worldspawn"
// brush 0
{
( -0 -0 -16 ) ( -0 -0  -0 ) ( 64 -0 -16 ) tex1 1 2 3 4 5
( -0 -0 -16 ) ( -0 64 -16 ) ( -0 -0  -0 ) tex2 0 0 0 1 1
( -0 -0 -16 ) ( 64 -0 -16 ) ( -0 64 -16 ) tex3 0 0 0 1 1
( 64 64  -0 ) ( -0 64  -0 ) ( 64 64 -16 ) tex4 0 0 0 1 1
( 64 64  -0 ) ( 64 64 -16 ) ( 64 -0  -0 ) tex5 0 0 0 1 1
( 64 64  -0 ) ( 64 -0  -0 ) ( -0 64  -0 ) tex6 0 0 0 1 1
}
}
)"};

void checkPreviewBounds(PrefabTool& tool, const std::filesystem::path& prefabPath)
{
  const auto delta = vm::vec3d{16.0, 32.0, 48.0};

  CHECK(tool.updatePreview(
    prefabPath, InputState{}, [&](auto&, const auto&, const auto& bounds, const auto&) {
      CHECK(bounds == vm::bbox3d{{0.0, 0.0, -16.0}, {64.0, 64.0, 0.0}});
      return delta;
    }));

  REQUIRE(tool.previewBounds());
  CHECK(*tool.previewBounds() == vm::bbox3d{{16.0, 32.0, 32.0}, {80.0, 96.0, 48.0}});
}

} // namespace

TEST_CASE("PrefabTool")
{
  auto fixture = MapDocumentFixture{};
  auto& document = fixture.create();
  auto tool = PrefabTool{document};

  SECTION("updates preview bounds")
  {
    auto env = fs::TestEnvironment{};
    const auto prefabPath = env.dir() / "crate.tbprefab";
    REQUIRE(writePrefabAsset(prefabPath, BrushPrefabText));

    checkPreviewBounds(tool, prefabPath);

    tool.clearPreview();

    CHECK_FALSE(tool.previewBounds());
  }

  SECTION("updates preview bounds for saved prefab snippets")
  {
    auto env = fs::TestEnvironment{};
    const auto prefabPath = env.dir() / "crate.tbprefab";
    REQUIRE(writePrefabAsset(prefabPath, SavedPrefabText));

    checkPreviewBounds(tool, prefabPath);
  }

  SECTION("does not log while updating preview")
  {
    auto logger = TestLogger{};
    document.setTargetLogger(&logger);

    auto env = fs::TestEnvironment{};
    const auto prefabPath = env.dir() / "crate.tbprefab";
    REQUIRE(writePrefabAsset(prefabPath, SavedPrefabText));

    const auto messageCount = logger.countMessages();
    CHECK(tool.updatePreview(
      prefabPath, InputState{}, [](auto&, const auto&, const auto&, const auto&) {
        return vm::vec3d{};
      }));
    CHECK(logger.countMessages() == messageCount);

    document.setTargetLogger(nullptr);
  }

  SECTION("clears preview for invalid prefab")
  {
    CHECK_FALSE(tool.updatePreview(
      "missing.tbprefab", InputState{}, [](auto&, const auto&, const auto&, const auto&) {
        return vm::vec3d{};
      }));

    CHECK_FALSE(tool.updatePreview(
      "missing.txt", InputState{}, [](auto&, const auto&, const auto&, const auto&) {
        return vm::vec3d{};
      }));
    CHECK_FALSE(tool.previewBounds());
  }
}

} // namespace tb::ui
