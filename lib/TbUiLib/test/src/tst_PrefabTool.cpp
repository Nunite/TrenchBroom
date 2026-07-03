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
#include "gl/Material.h"
#include "gl/MaterialCollection.h"
#include "gl/MaterialManager.h"
#include "gl/Texture.h"
#include "gl/TextureResource.h"
#include "mdl/EntityProperties.h"
#include "mdl/Map.h"
#include "mdl/Node.h"
#include "mdl/WadPropertyUtils.h"
#include "mdl/WorldNode.h"
#include "ui/InputState.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/PrefabAsset.h"
#include "ui/PrefabTool.h"
#include "ui/PrefabToolController.h"

#include "kd/vector_utils.h"

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
  REQUIRE(tool.previewNodes().size() == 1u);
  CHECK(
    tool.previewNodes().front()->logicalBounds()
    == vm::bbox3d{{16.0, 32.0, 32.0}, {80.0, 96.0, 48.0}});
}

gl::MaterialCollection makeMaterialCollection(
  std::filesystem::path path, std::vector<std::string> materialNames)
{
  auto materials = std::vector<gl::Material>{};
  for (auto& materialName : materialNames)
  {
    materials.emplace_back(
      std::move(materialName), gl::createTextureResource(gl::Texture{64, 64}));
  }
  return gl::MaterialCollection{std::move(path), std::move(materials)};
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
    CHECK(tool.previewNodes().empty());
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

  SECTION("increments preview version when preview changes")
  {
    auto env = fs::TestEnvironment{};
    const auto prefabPath = env.dir() / "crate.tbprefab";
    REQUIRE(writePrefabAsset(prefabPath, BrushPrefabText));

    const auto initialVersion = tool.previewVersion();
    REQUIRE(tool.updatePreview(
      prefabPath, InputState{}, [](auto&, const auto&, const auto&, const auto&) {
        return vm::vec3d{};
      }));
    CHECK(tool.previewVersion() == initialVersion + 1u);

    tool.clearPreview();

    CHECK(tool.previewVersion() == initialVersion + 2u);
  }

  SECTION("placement delta centers the prefab at the target point")
  {
    const auto bounds = vm::bbox3d{{0.0, 0.0, -16.0}, {64.0, 64.0, 0.0}};
    const auto targetPoint = vm::vec3d{128.0, 256.0, 16.0};
    const auto delta = prefabCenterPlacementDelta(bounds, targetPoint);

    CHECK(bounds.translate(delta).center() == targetPoint);
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
    CHECK(tool.previewNodes().empty());
  }

  SECTION("asks to import prefab material sources only when materials are missing")
  {
    auto env = fs::TestEnvironment{};
    const auto prefabPath = env.dir() / "crate.tbprefab";
    REQUIRE(writePrefabAsset(
      prefabPath,
      appendPrefabWadPaths(
        appendPrefabMaterialCollections(SavedPrefabText, {"textures/source.wad"}),
        {"source.wad"})));

    auto& map = document.map();
    map.materialManager().setMaterialCollections(
      kdl::vec_from(makeMaterialCollection("textures/current.wad", {"tex1"})));

    auto callbackCount = 0;
    auto missingMaterials = std::vector<std::string>{};
    auto materialCollections = std::vector<std::filesystem::path>{};
    auto wadPaths = std::vector<std::string>{};
    tool.setMaterialImportCallback(
      [&](const auto& missing, const auto& collections, const auto& wads) {
        ++callbackCount;
        missingMaterials = missing;
        materialCollections = collections;
        wadPaths = wads;
        return PrefabTool::PrefabMaterialImportAction::ContinueWithoutImport;
      });

    REQUIRE(tool.placePrefab(
      prefabPath, InputState{}, [](auto&, const auto&, const auto&, const auto&) {
        return vm::vec3d{};
      }));

    CHECK(callbackCount == 1);
    CHECK(
      missingMaterials
      == std::vector<std::string>{"tex2", "tex3", "tex4", "tex5", "tex6"});
    CHECK(
      materialCollections == std::vector<std::filesystem::path>{"textures/source.wad"});
    CHECK(wadPaths == std::vector<std::string>{"source.wad"});

    tool.setMaterialImportCallback([&](const auto&, const auto&, const auto&) {
      ++callbackCount;
      return PrefabTool::PrefabMaterialImportAction::Cancel;
    });
    map.materialManager().setMaterialCollections(kdl::vec_from(makeMaterialCollection(
      "textures/current.wad", {"tex1", "tex2", "tex3", "tex4", "tex5", "tex6"})));

    REQUIRE(tool.placePrefab(
      prefabPath, InputState{}, [](auto&, const auto&, const auto&, const auto&) {
        return vm::vec3d{};
      }));

    CHECK(callbackCount == 1);
  }

  SECTION("imports prefab material sources when requested")
  {
    auto quakeFixture = MapDocumentFixture{};
    auto& quakeDocument = quakeFixture.create(mdl::QuakeFixtureConfig);
    auto quakeTool = PrefabTool{quakeDocument};

    auto env = fs::TestEnvironment{};
    const auto prefabPath = env.dir() / "crate.tbprefab";
    REQUIRE(writePrefabAsset(
      prefabPath,
      appendPrefabWadPaths(
        appendPrefabMaterialCollections(SavedPrefabText, {"textures/source.wad"}),
        {"source.wad"})));

    auto& map = quakeDocument.map();
    quakeTool.setMaterialImportCallback([](const auto&, const auto&, const auto&) {
      return PrefabTool::PrefabMaterialImportAction::Import;
    });

    REQUIRE(quakeTool.placePrefab(
      prefabPath, InputState{}, [](auto&, const auto&, const auto&, const auto&) {
        return vm::vec3d{};
      }));

    const auto* enabledMaterialCollections = map.worldNode().entity().property(
      mdl::EntityPropertyKeys::TbEnabledMaterialCollections);
    REQUIRE(enabledMaterialCollections);
    CHECK(*enabledMaterialCollections == "textures/source.wad");

    const auto* wads = map.worldNode().entity().property(mdl::EntityPropertyKeys::Wad);
    REQUIRE(wads);
    CHECK(*wads == "source.wad");
  }
}

} // namespace tb::ui
