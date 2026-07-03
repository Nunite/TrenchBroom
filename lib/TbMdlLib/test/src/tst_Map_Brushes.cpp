/*
 Copyright (C) 2025 Kristian Duske

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

#include "fs/TestEnvironment.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/CatchConfig.h"
#include "mdl/EditorContext.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFixture.h"
#include "mdl/Map_Brushes.h"
#include "mdl/Map_Groups.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Matchers.h"
#include "mdl/NodeQueries.h"
#include "mdl/TestFactory.h"
#include "mdl/TestUtils.h"
#include "mdl/UVCoordSystem.h"
#include "mdl/UpdateBrushFaceAttributes.h"

#include "vm/approx.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::mdl
{
namespace
{

auto& getFace(const BrushNode& brushNode, const size_t faceIndex)
{
  return brushNode.brush().face(faceIndex);
}

BrushFaceHandle addTriangleFaceBrush(
  Map& map, const std::array<vm::vec3d, 3>& facePoints, const vm::vec3d& innerPoint)
{
  auto builder = BrushBuilder{
    map.worldNode().mapFormat(),
    map.worldBounds(),
    map.gameInfo().gameConfig.faceAttribsConfig.defaults};

  auto brush =
    builder.createBrush(
      std::vector<vm::vec3d>{facePoints[0], facePoints[1], facePoints[2], innerPoint},
      map.currentMaterialName())
    | kdl::value();
  auto* brushNode = new BrushNode{std::move(brush)};
  addNodes(map, {{parentForNodes(map), {brushNode}}});

  const auto hasPoint = [](const std::vector<vm::vec3d>& points, const vm::vec3d& point) {
    return std::ranges::any_of(points, [&](const auto& candidate) {
      return vm::is_equal(candidate, point, vm::Cd::almost_zero());
    });
  };
  auto faceIndex = std::optional<size_t>{};
  for (size_t i = 0; i < brushNode->brush().faceCount(); ++i)
  {
    const auto vertices = brushNode->brush().face(i).vertexPositions();
    if (
      vertices.size() == facePoints.size()
      && std::ranges::all_of(
        facePoints, [&](const auto& point) { return hasPoint(vertices, point); }))
    {
      faceIndex = i;
      break;
    }
  }
  REQUIRE(faceIndex);
  return BrushFaceHandle{brushNode, *faceIndex};
}

vm::vec2f textureCoords(const BrushFace& face, const vm::vec3d& point)
{
  return vm::vec2f{
    face.toUVCoordSystemMatrix(face.attributes().offset(), face.attributes().scale())
    * point};
}

std::vector<vm::vec3d> sharedVertices(const BrushFace& lhs, const BrushFace& rhs)
{
  auto result = std::vector<vm::vec3d>{};
  for (const auto& lhsVertex : lhs.vertexPositions())
  {
    for (const auto& rhsVertex : rhs.vertexPositions())
    {
      if (vm::is_equal(lhsVertex, rhsVertex, vm::Cd::almost_zero()))
      {
        result.push_back(lhsVertex);
      }
    }
  }
  return result;
}

} // namespace

TEST_CASE("Map_Brushes")
{
  auto fixture = MapFixture{};

  SECTION("createBrush")
  {
    auto& map = fixture.create();

    SECTION("valid brush")
    {
      const auto points = std::vector<vm::vec3d>{
        {-64, -64, -64},
        {-64, -64, +64},
        {-64, +64, -64},
        {-64, +64, +64},
        {+64, -64, -64},
        {+64, -64, +64},
        {+64, +64, -64},
        {+64, +64, +64},
      };

      CHECK(createBrush(map, points));

      REQUIRE(map.selection().brushes.size() == 1);

      const auto* brushNode = map.selection().brushes.front();
      CHECK(std::ranges::all_of(
        points, [&](const auto& point) { return brushNode->brush().hasVertex(point); }));
    }

    SECTION("invalid brush")
    {
      const auto points = std::vector<vm::vec3d>{
        {-64, -64, -64},
        {-64, -64, +64},
        {-64, +64, -64},
        {-64, +64, +64},
      };

      CHECK(!createBrush(map, points));
      CHECK(map.selection().brushes.empty());
    }
  }

  SECTION("setBrushFaceAttributes")
  {
    SECTION("Setting all attributes")
    {
      auto& map = fixture.create();

      auto* brushNode = createBrushNode(map);
      addNodes(map, {{parentForNodes(map), {brushNode}}});

      const size_t firstFaceIndex = 0u;
      const size_t secondFaceIndex = 1u;
      const size_t thirdFaceIndex = 2u;

      deselectAll(map);
      selectBrushFaces(map, {{brushNode, firstFaceIndex}});

      setBrushFaceAttributes(
        map,
        {
          .materialName = "first",
          .xOffset = SetValue{32.0f},
          .yOffset = SetValue{64.0f},
          .rotation = SetValue{90.0f},
          .xScale = SetValue{2.0f},
          .yScale = SetValue{4.0f},
          .surfaceFlags = SetFlags{63u},
          .surfaceContents = SetFlags{12u},
          .surfaceValue = SetValue{3.14f},
          .color = RgbaF{1.0f, 1.0f, 1.0f, 1.0f},
        });

      {
        const auto& firstAttrs = getFace(*brushNode, firstFaceIndex).attributes();
        CHECK(firstAttrs.materialName() == "first");
        CHECK(firstAttrs.xOffset() == 32.0f);
        CHECK(firstAttrs.yOffset() == 64.0f);
        CHECK(firstAttrs.rotation() == 90.0f);
        CHECK(firstAttrs.xScale() == 2.0f);
        CHECK(firstAttrs.yScale() == 4.0f);
        CHECK(firstAttrs.surfaceFlags() == 63u);
        CHECK(firstAttrs.surfaceContents() == 12u);
        CHECK(firstAttrs.surfaceValue() == 3.14f);
        CHECK(firstAttrs.color() == Color{RgbaF{1.0f, 1.0f, 1.0f, 1.0f}});
      }

      deselectAll(map);
      selectBrushFaces(map, {{brushNode, secondFaceIndex}});

      setBrushFaceAttributes(
        map,
        {
          .materialName = "second",
          .xOffset = SetValue{16.0f},
          .yOffset = SetValue{48.0f},
          .rotation = SetValue{45.0f},
          .xScale = SetValue{1.0f},
          .yScale = SetValue{1.0f},
          .surfaceFlags = SetFlags{18u},
          .surfaceContents = SetFlags{2048u},
          .surfaceValue = SetValue{1.0f},
          .color = RgbaF{0.5f, 0.5f, 0.5f, 0.5f},
        });

      {
        const auto& secondAttrs = getFace(*brushNode, secondFaceIndex).attributes();
        CHECK(secondAttrs.materialName() == "second");
        CHECK(secondAttrs.xOffset() == 16.0f);
        CHECK(secondAttrs.yOffset() == 48.0f);
        CHECK(secondAttrs.rotation() == 45.0f);
        CHECK(secondAttrs.xScale() == 1.0f);
        CHECK(secondAttrs.yScale() == 1.0f);
        CHECK(secondAttrs.surfaceFlags() == 18u);
        CHECK(secondAttrs.surfaceContents() == 2048u);
        CHECK(secondAttrs.surfaceValue() == 1.0f);
        CHECK(secondAttrs.color() == Color{RgbaF{0.5f, 0.5f, 0.5f, 0.5f}});
      }

      deselectAll(map);
      selectBrushFaces(map, {{brushNode, thirdFaceIndex}});

      setBrushFaceAttributes(
        map, copyAll(getFace(*brushNode, secondFaceIndex).attributes()));

      CHECK(
        getFace(*brushNode, thirdFaceIndex).attributes()
        == getFace(*brushNode, secondFaceIndex).attributes());

      auto thirdFaceContentsFlags =
        getFace(*brushNode, thirdFaceIndex).attributes().surfaceContents();

      deselectAll(map);
      selectBrushFaces(map, {{brushNode, secondFaceIndex}});

      setBrushFaceAttributes(
        map, copyAll(getFace(*brushNode, firstFaceIndex).attributes()));

      CHECK(
        getFace(*brushNode, secondFaceIndex).attributes()
        == getFace(*brushNode, firstFaceIndex).attributes());

      deselectAll(map);
      selectBrushFaces(map, {{brushNode, thirdFaceIndex}});
      setBrushFaceAttributes(
        map, copyAllExceptContentFlags(getFace(*brushNode, firstFaceIndex).attributes()));

      {
        const auto& firstAttrs = getFace(*brushNode, firstFaceIndex).attributes();
        const auto& newThirdAttrs = getFace(*brushNode, thirdFaceIndex).attributes();
        CHECK(newThirdAttrs.materialName() == firstAttrs.materialName());
        CHECK(newThirdAttrs.xOffset() == firstAttrs.xOffset());
        CHECK(newThirdAttrs.yOffset() == firstAttrs.yOffset());
        CHECK(newThirdAttrs.rotation() == firstAttrs.rotation());
        CHECK(newThirdAttrs.xScale() == firstAttrs.xScale());
        CHECK(newThirdAttrs.yScale() == firstAttrs.yScale());
        CHECK(newThirdAttrs.surfaceFlags() == firstAttrs.surfaceFlags());
        CHECK(newThirdAttrs.surfaceContents() == thirdFaceContentsFlags);
        CHECK(newThirdAttrs.surfaceValue() == firstAttrs.surfaceValue());
        CHECK(newThirdAttrs.color() == firstAttrs.color());
      }
    }

    SECTION("Undo and redo")
    {
      auto& map = fixture.create();

      auto* brushNode = createBrushNode(map, "original");
      addNodes(map, {{parentForNodes(map), {brushNode}}});

      for (const auto& face : brushNode->brush().faces())
      {
        REQUIRE(face.attributes().materialName() == "original");
      }

      selectNodes(map, {brushNode});

      setBrushFaceAttributes(map, {.materialName = "material"});
      for (const auto& face : brushNode->brush().faces())
      {
        REQUIRE(face.attributes().materialName() == "material");
      }

      map.undoCommand();
      for (const auto& face : brushNode->brush().faces())
      {
        CHECK(face.attributes().materialName() == "original");
      }

      map.redoCommand();
      for (const auto& face : brushNode->brush().faces())
      {
        CHECK(face.attributes().materialName() == "material");
      }
    }

    SECTION("Quake 2 format")
    {
      const int WaterFlag = 32;
      const int LavaFlag = 8;

      auto& map = fixture.load("test/mdl/Map/lavaAndWater.map", Quake2FixtureConfig);

      REQUIRE(map.editorContext().currentLayer() != nullptr);

      auto* lavabrush =
        dynamic_cast<BrushNode*>(map.editorContext().currentLayer()->children().at(0));
      REQUIRE(lavabrush);
      CHECK(!getFace(*lavabrush, 0).attributes().hasSurfaceAttributes());
      CHECK(
        getFace(*lavabrush, 0).resolvedSurfaceContents()
        == LavaFlag); // comes from the .wal texture

      auto* waterbrush =
        dynamic_cast<BrushNode*>(map.editorContext().currentLayer()->children().at(1));
      REQUIRE(waterbrush);
      CHECK(!getFace(*waterbrush, 0).attributes().hasSurfaceAttributes());
      CHECK(
        getFace(*waterbrush, 0).resolvedSurfaceContents()
        == WaterFlag); // comes from the .wal texture

      SECTION(
        "Transfer face attributes except content flags from waterbrush to lavabrush")
      {
        selectNodes(map, {lavabrush});

        CHECK(setBrushFaceAttributes(
          map, copyAllExceptContentFlags(getFace(*waterbrush, 0).attributes())));

        SECTION("Check lavabrush is now inheriting the water content flags")
        {
          // Note: the contents flag wasn't transferred, but because lavabrushes's
          // content flag was "Inherit", it stays "Inherit" and now inherits the water
          // contents
          CHECK(!getFace(*lavabrush, 0).attributes().hasSurfaceAttributes());
          CHECK(getFace(*lavabrush, 0).resolvedSurfaceContents() == WaterFlag);
          CHECK(getFace(*lavabrush, 0).attributes().materialName() == "watertest");
        }
      }

      SECTION(
        "Setting a content flag when the existing one is inherited keeps the existing "
        "one")
      {
        selectNodes(map, {lavabrush});

        CHECK(setBrushFaceAttributes(map, {.surfaceContents = SetFlagBits{WaterFlag}}));

        CHECK(getFace(*lavabrush, 0).attributes().hasSurfaceAttributes());
        CHECK(getFace(*lavabrush, 0).resolvedSurfaceContents() == (WaterFlag | LavaFlag));
      }
    }

    SECTION("Setting a material keeps the surface flags unset")
    {
      auto& map = fixture.create(QuakeFixtureConfig);

      auto* brushNode = createBrushNode(map);
      addNodes(map, {{parentForNodes(map), {brushNode}}});

      selectNodes(map, {brushNode});
      CHECK(!getFace(*brushNode, 0).attributes().hasSurfaceAttributes());

      setBrushFaceAttributes(map, {.materialName = "something_else"});

      CHECK(getFace(*brushNode, 0).attributes().materialName() == "something_else");
      CHECK(!getFace(*brushNode, 0).attributes().hasSurfaceAttributes());
    }

    SECTION("Reset attributes to defaults")
    {
      auto defaultFaceAttrs = BrushFaceAttributes{BrushFaceAttributes::NoMaterialName};
      defaultFaceAttrs.setXScale(0.5f);
      defaultFaceAttrs.setYScale(2.0f);

      auto fixtureConfig = MapFixtureConfig{};
      fixtureConfig.gameInfo.gameConfig.faceAttribsConfig.defaults = defaultFaceAttrs;

      auto& map = fixture.create(fixtureConfig);

      auto* brushNode = createBrushNode(map);
      addNodes(map, {{parentForNodes(map), {brushNode}}});

      const size_t faceIndex = 0u;
      const auto initialX = getFace(*brushNode, faceIndex).uAxis();
      const auto initialY = getFace(*brushNode, faceIndex).vAxis();

      selectBrushFaces(map, {{brushNode, faceIndex}});

      // NOLINTNEXTLINE(misc-const-correctness)
      for (size_t i = 0; i < 5; ++i)
      {
        setBrushFaceAttributes(map, {.rotation = AddValue{2.0f}});
      }

      REQUIRE(getFace(*brushNode, faceIndex).attributes().rotation() == 10.0f);

      setBrushFaceAttributes(map, resetAll(defaultFaceAttrs));

      CHECK(getFace(*brushNode, faceIndex).attributes().xOffset() == 0.0f);
      CHECK(getFace(*brushNode, faceIndex).attributes().yOffset() == 0.0f);
      CHECK(getFace(*brushNode, faceIndex).attributes().rotation() == 0.0f);
      CHECK(
        getFace(*brushNode, faceIndex).attributes().xScale()
        == defaultFaceAttrs.xScale());
      CHECK(
        getFace(*brushNode, faceIndex).attributes().yScale()
        == defaultFaceAttrs.yScale());

      CHECK(getFace(*brushNode, faceIndex).uAxis() == initialX);
      CHECK(getFace(*brushNode, faceIndex).vAxis() == initialY);
    }

    SECTION("Linked groups")
    {
      // https://github.com/TrenchBroom/TrenchBroom/issues/3768

      auto& map = fixture.create();

      auto* brushNode = createBrushNode(map);
      addNodes(map, {{parentForNodes(map), {brushNode}}});
      selectNodes(map, {brushNode});

      auto* groupNode = groupSelectedNodes(map, "test");
      REQUIRE(groupNode != nullptr);

      auto* linkedGroupNode = createLinkedDuplicate(map);
      REQUIRE(linkedGroupNode != nullptr);

      deselectAll(map);

      SECTION("Can select two linked groups and apply a material")
      {
        selectNodes(map, {groupNode, linkedGroupNode});

        REQUIRE(setBrushFaceAttributes(map, {.materialName = "abc"}));

        // check that the brushes in both linked groups got a material
        for (auto* g : std::vector<GroupNode*>{groupNode, linkedGroupNode})
        {
          auto* brush = dynamic_cast<BrushNode*>(g->children().at(0));
          REQUIRE(brush != nullptr);

          auto attrs = getFace(*brush, 0).attributes();
          CHECK(attrs.materialName() == "abc");
        }
      }
    }
  }

  SECTION("copyUV")
  {
    auto& map = fixture.create(QuakeFixtureConfig);

    auto* brushNode = createBrushNode(map);
    addNodes(map, {{parentForNodes(map), {brushNode}}});

    const auto sourceFaceIndex = brushNode->brush().findFace(vm::vec3d{0, -1, 0});
    REQUIRE(sourceFaceIndex);

    const auto targetFaceIndex = brushNode->brush().findFace(vm::vec3d{1, 0, 0});
    REQUIRE(targetFaceIndex);

    deselectAll(map);
    selectBrushFaces(map, {{brushNode, *sourceFaceIndex}});
    REQUIRE(setBrushFaceAttributes(
      map,
      {
        .xOffset = SetValue{13.0f},
        .yOffset = SetValue{17.0f},
        .rotation = SetValue{22.0f},
        .xScale = SetValue{1.2f},
        .yScale = SetValue{0.8f},
      }));

    deselectAll(map);
    selectBrushFaces(map, {{brushNode, *targetFaceIndex}});
    REQUIRE(setBrushFaceAttributes(
      map,
      {
        .xOffset = SetValue{2.0f},
        .yOffset = SetValue{3.0f},
        .rotation = SetValue{90.0f},
        .xScale = SetValue{1.0f},
        .yScale = SetValue{1.0f},
      }));

    const auto originalTargetFaceAttributes =
      getFace(*brushNode, *targetFaceIndex).attributes();
    const auto originalTargetUAxis = getFace(*brushNode, *targetFaceIndex).uAxis();
    const auto originalTargetVAxis = getFace(*brushNode, *targetFaceIndex).vAxis();

    const auto& sourceFace = getFace(*brushNode, *sourceFaceIndex);
    const auto sourceSnapshot = sourceFace.takeUVCoordSystemSnapshot();
    const auto sourceAttributes = sourceFace.attributes();
    const auto sourcePlane = sourceFace.boundary();

    CHECK(
      copyUV(map, *sourceSnapshot, sourceAttributes, sourcePlane, WrapStyle::Projection));

    auto expectedAttributes = originalTargetFaceAttributes;
    expectedAttributes.setXOffset(0.36245f);
    expectedAttributes.setYOffset(0.501574f);

    const auto& targetFace = getFace(*brushNode, *targetFaceIndex);
    CHECK_THAT(targetFace.attributes(), MatchesBrushFaceAttributes(expectedAttributes));
    CHECK(targetFace.uAxis() == vm::approx{vm::vec3d{0, -1, 0}});
    CHECK(targetFace.vAxis() == vm::approx{vm::vec3d{-0.374607, 0, -0.927184}});

    SECTION("Undo and redo")
    {
      map.undoCommand();

      const auto& undoneTargetFace = getFace(*brushNode, *targetFaceIndex);
      CHECK_THAT(
        undoneTargetFace.attributes(),
        MatchesBrushFaceAttributes(originalTargetFaceAttributes));
      CHECK(undoneTargetFace.uAxis() == vm::approx{originalTargetUAxis});
      CHECK(undoneTargetFace.vAxis() == vm::approx{originalTargetVAxis});

      map.redoCommand();

      const auto& redoneTargetFace = getFace(*brushNode, *targetFaceIndex);
      CHECK_THAT(
        redoneTargetFace.attributes(), MatchesBrushFaceAttributes(expectedAttributes));
      CHECK(redoneTargetFace.uAxis() == vm::approx{vm::vec3d{0, -1, 0}});
      CHECK(redoneTargetFace.vAxis() == vm::approx{vm::vec3d{-0.374607, 0, -0.927184}});
    }
  }

  SECTION("translateUV")
  {
    auto& map = fixture.create(QuakeFixtureConfig);

    auto* brushNode = createBrushNode(map);
    addNodes(map, {{parentForNodes(map), {brushNode}}});

    const auto faceIndex = brushNode->brush().findFace(vm::vec3d{0, 0, 1});
    REQUIRE(faceIndex);

    const auto otherFaceIndex = brushNode->brush().findFace(vm::vec3d{1, 0, 0});
    REQUIRE(otherFaceIndex);

    deselectAll(map);
    selectBrushFaces(map, {{brushNode, *faceIndex}});

    REQUIRE(setBrushFaceAttributes(
      map,
      {
        .xOffset = SetValue{10.0f},
        .yOffset = SetValue{20.0f},
      }));

    const auto cameraUp = vm::vec3f{0, 1, 0};
    const auto cameraRight = vm::vec3f{1, 0, 0};
    const auto delta = vm::vec2f{4.0f, 8.0f};

    const auto originalFaceAttributes = getFace(*brushNode, *faceIndex).attributes();
    const auto originalUAxis = getFace(*brushNode, *faceIndex).uAxis();
    const auto originalVAxis = getFace(*brushNode, *faceIndex).vAxis();

    const auto originalOtherFaceAttributes =
      getFace(*brushNode, *otherFaceIndex).attributes();

    auto expectedBrush = brushNode->brush();
    expectedBrush.face(*faceIndex)
      .translateUV(vm::vec3d{cameraUp}, vm::vec3d{cameraRight}, delta);
    const auto expectedAttributes = expectedBrush.face(*faceIndex).attributes();
    const auto expectedUAxis = expectedBrush.face(*faceIndex).uAxis();
    const auto expectedVAxis = expectedBrush.face(*faceIndex).vAxis();

    REQUIRE(translateUV(map, cameraUp, cameraRight, delta));

    const auto& movedFace = getFace(*brushNode, *faceIndex);
    CHECK_THAT(movedFace.attributes(), MatchesBrushFaceAttributes(expectedAttributes));
    CHECK(movedFace.uAxis() == vm::approx{expectedUAxis});
    CHECK(movedFace.vAxis() == vm::approx{expectedVAxis});

    CHECK_THAT(
      getFace(*brushNode, *otherFaceIndex).attributes(),
      MatchesBrushFaceAttributes(originalOtherFaceAttributes));

    SECTION("Undo and redo")
    {
      map.undoCommand();

      const auto& undoneFace = getFace(*brushNode, *faceIndex);
      CHECK_THAT(
        undoneFace.attributes(), MatchesBrushFaceAttributes(originalFaceAttributes));
      CHECK(undoneFace.uAxis() == vm::approx{originalUAxis});
      CHECK(undoneFace.vAxis() == vm::approx{originalVAxis});
      CHECK_THAT(
        getFace(*brushNode, *otherFaceIndex).attributes(),
        MatchesBrushFaceAttributes(originalOtherFaceAttributes));

      map.redoCommand();

      const auto& redoneFace = getFace(*brushNode, *faceIndex);
      CHECK_THAT(redoneFace.attributes(), MatchesBrushFaceAttributes(expectedAttributes));
      CHECK(redoneFace.uAxis() == vm::approx{expectedUAxis});
      CHECK(redoneFace.vAxis() == vm::approx{expectedVAxis});
      CHECK_THAT(
        getFace(*brushNode, *otherFaceIndex).attributes(),
        MatchesBrushFaceAttributes(originalOtherFaceAttributes));
    }
  }

  SECTION("rotateUV")
  {
    auto& map = fixture.create(QuakeFixtureConfig);

    auto* brushNode = createBrushNode(map);
    addNodes(map, {{parentForNodes(map), {brushNode}}});

    const auto faceIndex = brushNode->brush().findFace(vm::vec3d{0, 0, 1});
    REQUIRE(faceIndex);

    const auto otherFaceIndex = brushNode->brush().findFace(vm::vec3d{1, 0, 0});
    REQUIRE(otherFaceIndex);

    deselectAll(map);
    selectBrushFaces(map, {{brushNode, *faceIndex}});

    REQUIRE(setBrushFaceAttributes(map, {.rotation = SetValue{10.0f}}));

    const auto originalFaceAttributes = getFace(*brushNode, *faceIndex).attributes();
    const auto originalUAxis = getFace(*brushNode, *faceIndex).uAxis();
    const auto originalVAxis = getFace(*brushNode, *faceIndex).vAxis();

    const auto originalOtherFaceAttributes =
      getFace(*brushNode, *otherFaceIndex).attributes();

    auto expectedBrush = brushNode->brush();
    expectedBrush.face(*faceIndex).rotateUV(15.0f);
    const auto expectedAttributes = expectedBrush.face(*faceIndex).attributes();
    const auto expectedUAxis = expectedBrush.face(*faceIndex).uAxis();
    const auto expectedVAxis = expectedBrush.face(*faceIndex).vAxis();

    REQUIRE(rotateUV(map, 15.0f));

    const auto& rotatedFace = getFace(*brushNode, *faceIndex);
    CHECK_THAT(rotatedFace.attributes(), MatchesBrushFaceAttributes(expectedAttributes));
    CHECK(rotatedFace.uAxis() == vm::approx{expectedUAxis});
    CHECK(rotatedFace.vAxis() == vm::approx{expectedVAxis});

    CHECK_THAT(
      getFace(*brushNode, *otherFaceIndex).attributes(),
      MatchesBrushFaceAttributes(originalOtherFaceAttributes));

    SECTION("Undo and redo")
    {
      map.undoCommand();

      const auto& undoneFace = getFace(*brushNode, *faceIndex);
      CHECK_THAT(
        undoneFace.attributes(), MatchesBrushFaceAttributes(originalFaceAttributes));
      CHECK(undoneFace.uAxis() == vm::approx{originalUAxis});
      CHECK(undoneFace.vAxis() == vm::approx{originalVAxis});
      CHECK_THAT(
        getFace(*brushNode, *otherFaceIndex).attributes(),
        MatchesBrushFaceAttributes(originalOtherFaceAttributes));

      map.redoCommand();

      const auto& redoneFace = getFace(*brushNode, *faceIndex);
      CHECK_THAT(redoneFace.attributes(), MatchesBrushFaceAttributes(expectedAttributes));
      CHECK(redoneFace.uAxis() == vm::approx{expectedUAxis});
      CHECK(redoneFace.vAxis() == vm::approx{expectedVAxis});
      CHECK_THAT(
        getFace(*brushNode, *otherFaceIndex).attributes(),
        MatchesBrushFaceAttributes(originalOtherFaceAttributes));
    }
  }

  SECTION("shearUV")
  {
    auto& map = fixture.create(QuakeFixtureConfig);

    auto* brushNode = createBrushNode(map);
    addNodes(map, {{parentForNodes(map), {brushNode}}});

    const auto faceIndex = brushNode->brush().findFace(vm::vec3d{0, 0, 1});
    REQUIRE(faceIndex);

    const auto otherFaceIndex = brushNode->brush().findFace(vm::vec3d{1, 0, 0});
    REQUIRE(otherFaceIndex);

    deselectAll(map);
    selectBrushFaces(map, {{brushNode, *faceIndex}});

    const auto factors = vm::vec2f{0.25f, -0.5f};

    const auto originalFaceAttributes = getFace(*brushNode, *faceIndex).attributes();
    const auto originalUAxis = getFace(*brushNode, *faceIndex).uAxis();
    const auto originalVAxis = getFace(*brushNode, *faceIndex).vAxis();

    const auto originalOtherFaceAttributes =
      getFace(*brushNode, *otherFaceIndex).attributes();

    auto expectedBrush = brushNode->brush();
    expectedBrush.face(*faceIndex).shearUV(factors);
    const auto expectedAttributes = expectedBrush.face(*faceIndex).attributes();
    const auto expectedUAxis = expectedBrush.face(*faceIndex).uAxis();
    const auto expectedVAxis = expectedBrush.face(*faceIndex).vAxis();

    REQUIRE(shearUV(map, factors));

    const auto& shearedFace = getFace(*brushNode, *faceIndex);
    CHECK_THAT(shearedFace.attributes(), MatchesBrushFaceAttributes(expectedAttributes));
    CHECK(shearedFace.uAxis() == vm::approx{expectedUAxis});
    CHECK(shearedFace.vAxis() == vm::approx{expectedVAxis});

    CHECK_THAT(
      getFace(*brushNode, *otherFaceIndex).attributes(),
      MatchesBrushFaceAttributes(originalOtherFaceAttributes));

    SECTION("Undo and redo")
    {
      map.undoCommand();

      const auto& undoneFace = getFace(*brushNode, *faceIndex);
      CHECK_THAT(
        undoneFace.attributes(), MatchesBrushFaceAttributes(originalFaceAttributes));
      CHECK(undoneFace.uAxis() == vm::approx{originalUAxis});
      CHECK(undoneFace.vAxis() == vm::approx{originalVAxis});
      CHECK_THAT(
        getFace(*brushNode, *otherFaceIndex).attributes(),
        MatchesBrushFaceAttributes(originalOtherFaceAttributes));

      map.redoCommand();

      const auto& redoneFace = getFace(*brushNode, *faceIndex);
      CHECK_THAT(redoneFace.attributes(), MatchesBrushFaceAttributes(expectedAttributes));
      CHECK(redoneFace.uAxis() == vm::approx{expectedUAxis});
      CHECK(redoneFace.vAxis() == vm::approx{expectedVAxis});
      CHECK_THAT(
        getFace(*brushNode, *otherFaceIndex).attributes(),
        MatchesBrushFaceAttributes(originalOtherFaceAttributes));
    }
  }

  SECTION("flipUV")
  {
    auto& map = fixture.create(QuakeFixtureConfig);

    auto* brushNode = createBrushNode(map);
    addNodes(map, {{parentForNodes(map), {brushNode}}});

    const auto faceIndex = brushNode->brush().findFace(vm::vec3d{0, 0, 1});
    REQUIRE(faceIndex);

    const auto otherFaceIndex = brushNode->brush().findFace(vm::vec3d{1, 0, 0});
    REQUIRE(otherFaceIndex);

    deselectAll(map);
    selectBrushFaces(map, {{brushNode, *faceIndex}});

    REQUIRE(setBrushFaceAttributes(
      map,
      {
        .xScale = SetValue{2.0f},
        .yScale = SetValue{3.0f},
      }));

    const auto cameraUp = vm::vec3f{0, 1, 0};
    const auto cameraRight = vm::vec3f{1, 0, 0};
    const auto flipDirection = vm::direction::left;

    const auto originalFaceAttributes = getFace(*brushNode, *faceIndex).attributes();
    const auto originalUAxis = getFace(*brushNode, *faceIndex).uAxis();
    const auto originalVAxis = getFace(*brushNode, *faceIndex).vAxis();

    const auto originalOtherFaceAttributes =
      getFace(*brushNode, *otherFaceIndex).attributes();

    auto expectedBrush = brushNode->brush();
    expectedBrush.face(*faceIndex)
      .flipUV(vm::vec3d{cameraUp}, vm::vec3d{cameraRight}, flipDirection);
    const auto expectedAttributes = expectedBrush.face(*faceIndex).attributes();
    const auto expectedUAxis = expectedBrush.face(*faceIndex).uAxis();
    const auto expectedVAxis = expectedBrush.face(*faceIndex).vAxis();

    REQUIRE(flipUV(map, cameraUp, cameraRight, flipDirection));

    const auto& flippedFace = getFace(*brushNode, *faceIndex);
    CHECK_THAT(flippedFace.attributes(), MatchesBrushFaceAttributes(expectedAttributes));
    CHECK(flippedFace.uAxis() == vm::approx{expectedUAxis});
    CHECK(flippedFace.vAxis() == vm::approx{expectedVAxis});

    CHECK_THAT(
      getFace(*brushNode, *otherFaceIndex).attributes(),
      MatchesBrushFaceAttributes(originalOtherFaceAttributes));

    SECTION("Undo and redo")
    {
      map.undoCommand();

      const auto& undoneFace = getFace(*brushNode, *faceIndex);
      CHECK_THAT(
        undoneFace.attributes(), MatchesBrushFaceAttributes(originalFaceAttributes));
      CHECK(undoneFace.uAxis() == vm::approx{originalUAxis});
      CHECK(undoneFace.vAxis() == vm::approx{originalVAxis});
      CHECK_THAT(
        getFace(*brushNode, *otherFaceIndex).attributes(),
        MatchesBrushFaceAttributes(originalOtherFaceAttributes));

      map.redoCommand();

      const auto& redoneFace = getFace(*brushNode, *faceIndex);
      CHECK_THAT(redoneFace.attributes(), MatchesBrushFaceAttributes(expectedAttributes));
      CHECK(redoneFace.uAxis() == vm::approx{expectedUAxis});
      CHECK(redoneFace.vAxis() == vm::approx{expectedVAxis});
      CHECK_THAT(
        getFace(*brushNode, *otherFaceIndex).attributes(),
        MatchesBrushFaceAttributes(originalOtherFaceAttributes));
    }
  }

  SECTION("alignUV")
  {
    auto& map = fixture.create(QuakeFixtureConfig);

    auto* brushNode = createBrushNode(map);
    addNodes(map, {{parentForNodes(map), {brushNode}}});

    const auto faceIndex = brushNode->brush().findFace(vm::vec3d{0, -1, 0});
    REQUIRE(faceIndex);

    const auto otherFaceIndex = brushNode->brush().findFace(vm::vec3d{1, 0, 0});
    REQUIRE(otherFaceIndex);

    deselectAll(map);
    selectBrushFaces(map, {{brushNode, *faceIndex}});

    REQUIRE(setBrushFaceAttributes(map, {.rotation = SetValue{0.0f}}));

    const auto originalFaceAttributes = getFace(*brushNode, *faceIndex).attributes();
    const auto originalUAxis = getFace(*brushNode, *faceIndex).uAxis();
    const auto originalVAxis = getFace(*brushNode, *faceIndex).vAxis();

    const auto originalOtherFaceAttributes =
      getFace(*brushNode, *otherFaceIndex).attributes();

    auto expectedBrush = brushNode->brush();
    evaluate(
      align(expectedBrush.face(*faceIndex), UvPolicy::next),
      expectedBrush.face(*faceIndex));
    const auto expectedAttributes = expectedBrush.face(*faceIndex).attributes();
    const auto expectedUAxis = expectedBrush.face(*faceIndex).uAxis();
    const auto expectedVAxis = expectedBrush.face(*faceIndex).vAxis();

    alignUV(map, UvPolicy::next);

    const auto& alignedFace = getFace(*brushNode, *faceIndex);
    CHECK_THAT(
      alignedFace.attributes(), !MatchesBrushFaceAttributes(originalFaceAttributes));
    CHECK_THAT(alignedFace.attributes(), MatchesBrushFaceAttributes(expectedAttributes));
    CHECK(alignedFace.uAxis() == vm::approx{expectedUAxis});
    CHECK(alignedFace.vAxis() == vm::approx{expectedVAxis});

    CHECK_THAT(
      getFace(*brushNode, *otherFaceIndex).attributes(),
      MatchesBrushFaceAttributes(originalOtherFaceAttributes));

    SECTION("Undo and redo")
    {
      map.undoCommand();

      const auto& undoneFace = getFace(*brushNode, *faceIndex);
      CHECK_THAT(
        undoneFace.attributes(), MatchesBrushFaceAttributes(originalFaceAttributes));
      CHECK(undoneFace.uAxis() == vm::approx{originalUAxis});
      CHECK(undoneFace.vAxis() == vm::approx{originalVAxis});
      CHECK_THAT(
        getFace(*brushNode, *otherFaceIndex).attributes(),
        MatchesBrushFaceAttributes(originalOtherFaceAttributes));

      map.redoCommand();

      const auto& redoneFace = getFace(*brushNode, *faceIndex);
      CHECK_THAT(redoneFace.attributes(), MatchesBrushFaceAttributes(expectedAttributes));
      CHECK(redoneFace.uAxis() == vm::approx{expectedUAxis});
      CHECK(redoneFace.vAxis() == vm::approx{expectedVAxis});
      CHECK_THAT(
        getFace(*brushNode, *otherFaceIndex).attributes(),
        MatchesBrushFaceAttributes(originalOtherFaceAttributes));
    }
  }

  SECTION("unwrapUVAsQuads")
  {
    const auto p0 = vm::vec3d{0, 0, 0};
    const auto p1 = vm::vec3d{0, 0, 64};
    const auto p2 = vm::vec3d{64, 0, 64};
    const auto p3 = vm::vec3d{64, 0, 0};
    const auto p4 = vm::vec3d{128, 0, 64};
    const auto p5 = vm::vec3d{128, 0, 0};
    const auto innerPoint = vm::vec3d{32, 32, 32};

    SECTION("Unwraps paired triangle faces into rectangular quads")
    {
      auto& map = fixture.create(QuakeFixtureConfig);
      const auto firstLower = addTriangleFaceBrush(map, {p0, p1, p3}, innerPoint);
      const auto firstUpper = addTriangleFaceBrush(map, {p1, p2, p3}, innerPoint);
      const auto secondLower = addTriangleFaceBrush(map, {p3, p2, p5}, innerPoint);
      const auto secondUpper = addTriangleFaceBrush(map, {p2, p4, p5}, innerPoint);
      const auto unselectedFace = addTriangleFaceBrush(map, {p0, p1, p5}, innerPoint);

      const auto originalUnselectedAttributes = unselectedFace.face().attributes();

      deselectAll(map);
      selectBrushFaces(map, {firstLower, firstUpper, secondLower, secondUpper});

      CHECK(unwrapUVAsQuads(map));

      CHECK(textureCoords(firstLower.face(), p0) == vm::approx{vm::vec2f{0, 0}});
      CHECK(textureCoords(firstLower.face(), p1) == vm::approx{vm::vec2f{0, 64}});
      CHECK(textureCoords(firstLower.face(), p3) == vm::approx{vm::vec2f{64, 0}});

      CHECK(textureCoords(firstUpper.face(), p1) == vm::approx{vm::vec2f{0, 64}});
      CHECK(textureCoords(firstUpper.face(), p2) == vm::approx{vm::vec2f{64, 64}});
      CHECK(textureCoords(firstUpper.face(), p3) == vm::approx{vm::vec2f{64, 0}});

      CHECK(textureCoords(secondLower.face(), p3) == vm::approx{vm::vec2f{64, 0}});
      CHECK(textureCoords(secondLower.face(), p2) == vm::approx{vm::vec2f{64, 64}});
      CHECK(textureCoords(secondLower.face(), p5) == vm::approx{vm::vec2f{128, 0}});

      CHECK(textureCoords(secondUpper.face(), p2) == vm::approx{vm::vec2f{64, 64}});
      CHECK(textureCoords(secondUpper.face(), p4) == vm::approx{vm::vec2f{128, 64}});
      CHECK(textureCoords(secondUpper.face(), p5) == vm::approx{vm::vec2f{128, 0}});

      CHECK_THAT(
        unselectedFace.face().attributes(),
        MatchesBrushFaceAttributes(originalUnselectedAttributes));
    }

    SECTION("Returns false for Standard UV maps")
    {
      auto& standardMap = fixture.create({.mapFormat = MapFormat::Standard});
      const auto lower = addTriangleFaceBrush(standardMap, {p0, p1, p3}, innerPoint);
      const auto upper = addTriangleFaceBrush(standardMap, {p1, p2, p3}, innerPoint);
      const auto originalAttributes = lower.face().attributes();

      deselectAll(standardMap);
      selectBrushFaces(standardMap, {lower, upper});

      CHECK(!unwrapUVAsQuads(standardMap));
      CHECK_THAT(
        lower.face().attributes(), MatchesBrushFaceAttributes(originalAttributes));
    }

    SECTION("Returns false for unpaired triangles")
    {
      auto& map = fixture.create(QuakeFixtureConfig);
      const auto firstLower = addTriangleFaceBrush(map, {p0, p1, p3}, innerPoint);

      deselectAll(map);
      selectBrushFaces(map, {firstLower});
      const auto originalAttributes = firstLower.face().attributes();

      CHECK(!unwrapUVAsQuads(map));
      CHECK_THAT(
        firstLower.face().attributes(), MatchesBrushFaceAttributes(originalAttributes));
    }

    SECTION("Unwraps curved slide brushes selected as nodes")
    {
      auto env = fs::TestEnvironment{};
      env.createFile("curved-slide-sample.map", R"(// Game: Half-Life
// Format: Valve
// entity 0
{
"mapversion" "220"
"classname" "worldspawn"
// brush 0
{
( -384 -640 420 ) ( -377 -565 404 ) ( -377 -565 420 ) TRIM_SHIT1_ALBE [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -256 -640 240 ) ( -377 -565 404 ) ( -384 -640 404 ) TRIM_SHIT1_ALBE [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -251 -590 240 ) ( -377 -565 404 ) ( -256 -640 240 ) TRIM_SHIT1_ALBE [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -256 -640 256 ) ( -384 -640 404 ) ( -384 -640 420 ) TRIM_SHIT1_ALBE [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -251 -590 256 ) ( -377 -565 404 ) ( -251 -590 240 ) TRIM_SHIT1_ALBE [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -251 -590 256 ) ( -384 -640 420 ) ( -377 -565 420 ) TRIM_SHIT1_ALBE [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -251 -590 256 ) ( -256 -640 256 ) ( -384 -640 420 ) TRIM_SHIT1_ALBE [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -251 -590 256 ) ( -256 -640 240 ) ( -256 -640 256 ) TRIM_SHIT1_ALBE [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
}
// brush 1
{
( -377 -565 420 ) ( -355 -493 404 ) ( -355 -493 420 ) TRIM_SHIT1_ALBE [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -237 -542 240 ) ( -377 -565 404 ) ( -251 -590 240 ) TRIM_SHIT1_ALBE [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -237 -542 240 ) ( -355 -493 404 ) ( -377 -565 404 ) TRIM_SHIT1_ALBE [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -251 -590 256 ) ( -377 -565 404 ) ( -377 -565 420 ) TRIM_SHIT1_ALBE [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -237 -542 256 ) ( -355 -493 404 ) ( -237 -542 240 ) TRIM_SHIT1_ALBE [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -251 -590 256 ) ( -377 -565 420 ) ( -355 -493 420 ) TRIM_SHIT1_ALBE [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -237 -542 256 ) ( -251 -590 256 ) ( -355 -493 420 ) TRIM_SHIT1_ALBE [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -237 -542 256 ) ( -251 -590 240 ) ( -251 -590 256 ) TRIM_SHIT1_ALBE [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
}
// brush 2
{
( -355 -493 420 ) ( -319 -427 404 ) ( -319 -427 420 ) TRIM_SHIT1_ALBE [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -213 -498 240 ) ( -355 -493 404 ) ( -237 -542 240 ) TRIM_SHIT1_ALBE [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -237 -542 256 ) ( -355 -493 404 ) ( -355 -493 420 ) TRIM_SHIT1_ALBE [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -213 -498 256 ) ( -319 -427 404 ) ( -213 -498 240 ) TRIM_SHIT1_ALBE [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -213 -498 256 ) ( -355 -493 420 ) ( -319 -427 420 ) TRIM_SHIT1_ALBE [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -213 -498 256 ) ( -237 -542 240 ) ( -237 -542 256 ) TRIM_SHIT1_ALBE [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
}
// brush 3
{
( -319 -427 420 ) ( -272 -368 404 ) ( -272 -368 420 ) TRIM_SHIT1_ALBE [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -213 -498 240 ) ( -272 -368 404 ) ( -319 -427 404 ) TRIM_SHIT1_ALBE [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -181 -459 240 ) ( -272 -368 404 ) ( -213 -498 240 ) TRIM_SHIT1_ALBE [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1
( -213 -498 256 ) ( -319 -427 404 ) ( -319 -427 420 ) TRIM_SHIT1_ALBE [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -181 -459 256 ) ( -213 -498 256 ) ( -319 -427 420 ) TRIM_SHIT1_ALBE [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1
( -181 -459 256 ) ( -319 -427 420 ) ( -272 -368 420 ) TRIM_SHIT1_ALBE [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -181 -459 256 ) ( -272 -368 404 ) ( -181 -459 240 ) TRIM_SHIT1_ALBE [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( -181 -459 256 ) ( -213 -498 240 ) ( -213 -498 256 ) TRIM_SHIT1_ALBE [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
}
}
)");

      auto& map = fixture.load(env.dir() / "curved-slide-sample.map", QuakeFixtureConfig);
      const auto brushNodes = collectDescendants(
        std::vector<Node*>{&map.worldNode()}, [](const BrushNode&) { return true; });
      REQUIRE(brushNodes.size() == 4u);

      deselectAll(map);
      selectNodes(map, brushNodes);

      const auto selectedFaces = map.selection().allBrushFaces();
      REQUIRE(!selectedFaces.empty());
      struct OriginalUV
      {
        BrushFaceAttributes attributes;
        vm::vec3d uAxis;
        vm::vec3d vAxis;
      };
      auto originalUVs = std::vector<OriginalUV>{};
      originalUVs.reserve(selectedFaces.size());
      for (const auto& handle : selectedFaces)
      {
        originalUVs.push_back(
          {handle.face().attributes(), handle.face().uAxis(), handle.face().vAxis()});
      }

      CHECK(unwrapUVAsQuads(map));

      const auto changedFaces = map.selection().allBrushFaces();
      REQUIRE(changedFaces.size() == originalUVs.size());
      auto changedFaceCount = 0u;
      auto changed = std::vector<bool>(changedFaces.size(), false);
      for (size_t i = 0u; i < changedFaces.size(); ++i)
      {
        const auto& face = changedFaces[i].face();
        const auto& original = originalUVs[i];
        if (
          !MatchesBrushFaceAttributes(original.attributes).match(face.attributes())
          || !(face.uAxis() == vm::approx{original.uAxis})
          || !(face.vAxis() == vm::approx{original.vAxis}))
        {
          changed[i] = true;
          ++changedFaceCount;
        }
      }
      CHECK(changedFaceCount > 0u);

      auto sharedEdgeCount = 0u;
      auto continuousSharedEdgeCount = 0u;
      for (size_t i = 0u; i < changedFaces.size(); ++i)
      {
        for (size_t j = i + 1u; j < changedFaces.size(); ++j)
        {
          if (!changed[i] || !changed[j])
          {
            continue;
          }

          const auto shared =
            sharedVertices(changedFaces[i].face(), changedFaces[j].face());
          if (shared.size() != 2u)
          {
            continue;
          }

          ++sharedEdgeCount;
          if (
            textureCoords(changedFaces[i].face(), shared[0])
              == vm::approx{textureCoords(changedFaces[j].face(), shared[0])}
            && textureCoords(changedFaces[i].face(), shared[1])
                 == vm::approx{textureCoords(changedFaces[j].face(), shared[1])})
          {
            ++continuousSharedEdgeCount;
          }
        }
      }
      CHECK(sharedEdgeCount > 0u);
      CHECK(continuousSharedEdgeCount > 0u);
    }
  }

  SECTION("justifyUV")
  {
    auto& map = fixture.create(QuakeFixtureConfig);

    auto* brushNode = createBrushNode(map);
    addNodes(map, {{parentForNodes(map), {brushNode}}});

    const auto faceIndex = brushNode->brush().findFace(vm::vec3d{0, -1, 0});
    REQUIRE(faceIndex);

    const auto otherFaceIndex = brushNode->brush().findFace(vm::vec3d{1, 0, 0});
    REQUIRE(otherFaceIndex);

    deselectAll(map);
    selectBrushFaces(map, {{brushNode, *faceIndex}});

    REQUIRE(setBrushFaceAttributes(
      map,
      {
        .xOffset = SetValue{7.0f},
        .yOffset = SetValue{11.0f},
      }));

    const auto originalFaceAttributes = getFace(*brushNode, *faceIndex).attributes();
    const auto originalUAxis = getFace(*brushNode, *faceIndex).uAxis();
    const auto originalVAxis = getFace(*brushNode, *faceIndex).vAxis();

    const auto originalOtherFaceAttributes =
      getFace(*brushNode, *otherFaceIndex).attributes();

    auto expectedBrush = brushNode->brush();
    evaluate(
      justify(expectedBrush.face(*faceIndex), UvAxis::u, UvSign::plus, UvPolicy::best),
      expectedBrush.face(*faceIndex));
    const auto expectedAttributes = expectedBrush.face(*faceIndex).attributes();
    const auto expectedUAxis = expectedBrush.face(*faceIndex).uAxis();
    const auto expectedVAxis = expectedBrush.face(*faceIndex).vAxis();

    justifyUV(map, UvJustifyDirection::Left, UvPolicy::best);

    const auto& justifiedFace = getFace(*brushNode, *faceIndex);
    CHECK_THAT(
      justifiedFace.attributes(), !MatchesBrushFaceAttributes(originalFaceAttributes));
    CHECK_THAT(
      justifiedFace.attributes(), MatchesBrushFaceAttributes(expectedAttributes));
    CHECK(justifiedFace.uAxis() == vm::approx{expectedUAxis});
    CHECK(justifiedFace.vAxis() == vm::approx{expectedVAxis});

    CHECK_THAT(
      getFace(*brushNode, *otherFaceIndex).attributes(),
      MatchesBrushFaceAttributes(originalOtherFaceAttributes));

    SECTION("Undo and redo")
    {
      map.undoCommand();

      const auto& undoneFace = getFace(*brushNode, *faceIndex);
      CHECK_THAT(
        undoneFace.attributes(), MatchesBrushFaceAttributes(originalFaceAttributes));
      CHECK(undoneFace.uAxis() == vm::approx{originalUAxis});
      CHECK(undoneFace.vAxis() == vm::approx{originalVAxis});
      CHECK_THAT(
        getFace(*brushNode, *otherFaceIndex).attributes(),
        MatchesBrushFaceAttributes(originalOtherFaceAttributes));

      map.redoCommand();

      const auto& redoneFace = getFace(*brushNode, *faceIndex);
      CHECK_THAT(redoneFace.attributes(), MatchesBrushFaceAttributes(expectedAttributes));
      CHECK(redoneFace.uAxis() == vm::approx{expectedUAxis});
      CHECK(redoneFace.vAxis() == vm::approx{expectedVAxis});
      CHECK_THAT(
        getFace(*brushNode, *otherFaceIndex).attributes(),
        MatchesBrushFaceAttributes(originalOtherFaceAttributes));
    }
  }

  SECTION("fitUV")
  {
    auto& map = fixture.create(QuakeFixtureConfig);

    auto* brushNode = createBrushNode(map);
    addNodes(map, {{parentForNodes(map), {brushNode}}});

    const auto faceIndex = brushNode->brush().findFace(vm::vec3d{0, -1, 0});
    REQUIRE(faceIndex);

    const auto otherFaceIndex = brushNode->brush().findFace(vm::vec3d{1, 0, 0});
    REQUIRE(otherFaceIndex);

    deselectAll(map);
    selectBrushFaces(map, {{brushNode, *faceIndex}});

    REQUIRE(setBrushFaceAttributes(
      map,
      {
        .xOffset = SetValue{5.0f},
        .yOffset = SetValue{9.0f},
        .xScale = SetValue{1.0f},
      }));

    const auto originalFaceAttributes = getFace(*brushNode, *faceIndex).attributes();
    const auto originalUAxis = getFace(*brushNode, *faceIndex).uAxis();
    const auto originalVAxis = getFace(*brushNode, *faceIndex).vAxis();

    const auto originalOtherFaceAttributes =
      getFace(*brushNode, *otherFaceIndex).attributes();

    auto expectedBrush = brushNode->brush();
    auto& expectedFace = expectedBrush.face(*faceIndex);

    const auto invariantVertex = anchorVertex(expectedFace, UvAxis::u, UvSign::minus);
    const auto previousUvCoords = vm::vec2f{
      expectedFace.toUVCoordSystemMatrix(
        expectedFace.attributes().offset(), expectedFace.attributes().scale())
      * invariantVertex};

    evaluate(fit(expectedFace, UvAxis::u, UvPolicy::next), expectedFace);

    const auto newUvCoords = vm::vec2f{
      expectedFace.toUVCoordSystemMatrix(
        expectedFace.attributes().offset(), expectedFace.attributes().scale())
      * invariantVertex};
    const auto delta = previousUvCoords - newUvCoords;

    evaluate(
      {
        .xOffset = AddValue{delta.x()},
        .yOffset = AddValue{delta.y()},
      },
      expectedFace);

    const auto expectedAttributes = expectedFace.attributes();
    const auto expectedUAxis = expectedFace.uAxis();
    const auto expectedVAxis = expectedFace.vAxis();

    fitUV(map, UvFitDirection::Horizontal, UvPolicy::next);

    const auto& fittedFace = getFace(*brushNode, *faceIndex);
    CHECK_THAT(
      fittedFace.attributes(), !MatchesBrushFaceAttributes(originalFaceAttributes));
    CHECK_THAT(fittedFace.attributes(), MatchesBrushFaceAttributes(expectedAttributes));
    CHECK(fittedFace.uAxis() == vm::approx{expectedUAxis});
    CHECK(fittedFace.vAxis() == vm::approx{expectedVAxis});

    CHECK_THAT(
      getFace(*brushNode, *otherFaceIndex).attributes(),
      MatchesBrushFaceAttributes(originalOtherFaceAttributes));

    SECTION("Undo and redo")
    {
      map.undoCommand();

      const auto& undoneFace = getFace(*brushNode, *faceIndex);
      CHECK_THAT(
        undoneFace.attributes(), MatchesBrushFaceAttributes(originalFaceAttributes));
      CHECK(undoneFace.uAxis() == vm::approx{originalUAxis});
      CHECK(undoneFace.vAxis() == vm::approx{originalVAxis});
      CHECK_THAT(
        getFace(*brushNode, *otherFaceIndex).attributes(),
        MatchesBrushFaceAttributes(originalOtherFaceAttributes));

      map.redoCommand();

      const auto& redoneFace = getFace(*brushNode, *faceIndex);
      CHECK_THAT(redoneFace.attributes(), MatchesBrushFaceAttributes(expectedAttributes));
      CHECK(redoneFace.uAxis() == vm::approx{expectedUAxis});
      CHECK(redoneFace.vAxis() == vm::approx{expectedVAxis});
      CHECK_THAT(
        getFace(*brushNode, *otherFaceIndex).attributes(),
        MatchesBrushFaceAttributes(originalOtherFaceAttributes));
    }
  }

  SECTION("autoFitUV")
  {
    auto& map = fixture.create(QuakeFixtureConfig);

    auto* brushNode = createBrushNode(map);
    addNodes(map, {{parentForNodes(map), {brushNode}}});

    const auto iFront = *brushNode->brush().findFace(vm::vec3d{0, -1, 0});
    REQUIRE(iFront);

    const auto iRight = *brushNode->brush().findFace(vm::vec3d{1, 0, 0});
    REQUIRE(iRight);

    const auto iTop = *brushNode->brush().findFace(vm::vec3d{0, 0, 1});
    REQUIRE(iTop);

    SECTION("Aligns when any selected face is not aligned")
    {
      // front face is not aligned (rotation == 15)
      deselectAll(map);
      selectBrushFaces(map, {{brushNode, iFront}});
      REQUIRE(setBrushFaceAttributes(
        map,
        {
          .xOffset = SetValue{5.0f},
          .yOffset = SetValue{9.0f},
          .rotation = SetValue{15.0f},
          .xScale = SetValue{1.3f},
          .yScale = SetValue{0.8f},
        }));

      // right face is aligned (rotation == 0)
      deselectAll(map);
      selectBrushFaces(map, {{brushNode, iRight}});
      REQUIRE(setBrushFaceAttributes(
        map,
        {
          .xOffset = SetValue{15.5f},
          .yOffset = SetValue{15.5f},
          .rotation = SetValue{0.0f},
          .xScale = SetValue{32.0f},
          .yScale = SetValue{32.0f},
        }));

      REQUIRE(!isAligned(getFace(*brushNode, iFront)));
      REQUIRE(getFace(*brushNode, iRight).uAxis() == vm::approx{vm::vec3d{0, 1, 0}});
      REQUIRE(getFace(*brushNode, iRight).vAxis() == vm::approx{vm::vec3d{0, 0, -1}});

      const auto originalTopAttributes = getFace(*brushNode, iTop).attributes();

      deselectAll(map);
      selectBrushFaces(map, {{brushNode, iFront}, {brushNode, iRight}});
      autoFitUV(map);

      // front face is now aligned
      CHECK(getFace(*brushNode, iFront).uAxis() == vm::approx{vm::vec3d{1, 0, 0}});
      CHECK(getFace(*brushNode, iFront).vAxis() == vm::approx{vm::vec3d{0, 0, -1}});

      // right face remains aligned
      CHECK(getFace(*brushNode, iRight).uAxis() == vm::approx{vm::vec3d{0, 1, 0}});
      CHECK(getFace(*brushNode, iRight).vAxis() == vm::approx{vm::vec3d{0, 0, -1}});

      // top face was not affected
      CHECK_THAT(
        getFace(*brushNode, iTop).attributes(),
        MatchesBrushFaceAttributes(originalTopAttributes));
    }

    SECTION(
      "Does not realign when selected faces are aligned but not all fitted and justified")
    {
      deselectAll(map);
      selectBrushFaces(map, {{brushNode, iFront}});
      REQUIRE(setBrushFaceAttributes(
        map,
        {
          .xOffset = SetValue{7.0f},
          .yOffset = SetValue{11.0f},
          .rotation = SetValue{0.0f},
          .xScale = SetValue{1.4f},
          .yScale = SetValue{0.9f},
        }));

      deselectAll(map);
      selectBrushFaces(map, {{brushNode, iRight}});
      REQUIRE(setBrushFaceAttributes(
        map,
        {
          .xOffset = SetValue{15.5f},
          .yOffset = SetValue{15.5f},
          .rotation = SetValue{0.0f},
          .xScale = SetValue{32.0f},
          .yScale = SetValue{32.0f},
        }));

      deselectAll(map);
      selectBrushFaces(map, {{brushNode, iFront}, {brushNode, iRight}});

      REQUIRE(isAligned(getFace(*brushNode, iFront)));
      REQUIRE(!isJustified(getFace(*brushNode, iFront), UvAxis::u, UvSign::plus));
      REQUIRE(!isJustified(getFace(*brushNode, iFront), UvAxis::v, UvSign::plus));
      REQUIRE(!isFitted(getFace(*brushNode, iFront), UvAxis::u));
      REQUIRE(!isFitted(getFace(*brushNode, iFront), UvAxis::v));

      REQUIRE(isAligned(getFace(*brushNode, iRight)));
      REQUIRE(isJustified(getFace(*brushNode, iRight), UvAxis::u, UvSign::plus));
      REQUIRE(isJustified(getFace(*brushNode, iRight), UvAxis::v, UvSign::plus));
      REQUIRE(isFitted(getFace(*brushNode, iRight), UvAxis::u));
      REQUIRE(isFitted(getFace(*brushNode, iRight), UvAxis::v));

      REQUIRE(getFace(*brushNode, iRight).uAxis() == vm::approx{vm::vec3d{0, 1, 0}});
      REQUIRE(getFace(*brushNode, iRight).vAxis() == vm::approx{vm::vec3d{0, 0, -1}});

      autoFitUV(map);

      CHECK(getFace(*brushNode, iFront).uAxis() == vm::approx{vm::vec3d{1, 0, 0}});
      CHECK(getFace(*brushNode, iFront).vAxis() == vm::approx{vm::vec3d{0, 0, -1}});
      CHECK(getFace(*brushNode, iRight).uAxis() == vm::approx{vm::vec3d{0, 1, 0}});
      CHECK(getFace(*brushNode, iRight).vAxis() == vm::approx{vm::vec3d{0, 0, -1}});

      CHECK(isAligned(getFace(*brushNode, iFront)));
      CHECK(isJustified(getFace(*brushNode, iFront), UvAxis::u, UvSign::plus));
      CHECK(isJustified(getFace(*brushNode, iFront), UvAxis::v, UvSign::plus));
      CHECK(isFitted(getFace(*brushNode, iFront), UvAxis::u));
      CHECK(isFitted(getFace(*brushNode, iFront), UvAxis::v));

      CHECK(isAligned(getFace(*brushNode, iRight)));
      CHECK(isJustified(getFace(*brushNode, iRight), UvAxis::u, UvSign::plus));
      CHECK(isJustified(getFace(*brushNode, iRight), UvAxis::v, UvSign::plus));
      CHECK(isFitted(getFace(*brushNode, iRight), UvAxis::u));
      CHECK(isFitted(getFace(*brushNode, iRight), UvAxis::v));
    }

    SECTION("Undo and Redo")
    {
      deselectAll(map);
      selectBrushFaces(map, {{brushNode, iFront}});
      REQUIRE(setBrushFaceAttributes(
        map,
        {
          .xOffset = SetValue{5.0f},
          .yOffset = SetValue{9.0f},
          .rotation = SetValue{15.0f},
          .xScale = SetValue{1.3f},
          .yScale = SetValue{0.8f},
        }));

      deselectAll(map);
      selectBrushFaces(map, {{brushNode, iRight}});
      REQUIRE(setBrushFaceAttributes(
        map,
        {
          .xOffset = SetValue{15.5f},
          .yOffset = SetValue{15.5f},
          .rotation = SetValue{0.0f},
          .xScale = SetValue{32.0f},
          .yScale = SetValue{32.0f},
        }));

      const auto originalFrontAttributes = getFace(*brushNode, iFront).attributes();
      const auto originalRightAttributes = getFace(*brushNode, iRight).attributes();

      deselectAll(map);
      selectBrushFaces(map, {{brushNode, iFront}, {brushNode, iRight}});
      autoFitUV(map);

      const auto modifiedFrontAttributes = getFace(*brushNode, iFront).attributes();
      const auto modifiedRightAttributes = getFace(*brushNode, iRight).attributes();

      REQUIRE(modifiedFrontAttributes != originalFrontAttributes);
      REQUIRE(modifiedRightAttributes != originalRightAttributes);

      map.undoCommand();

      REQUIRE(getFace(*brushNode, iFront).attributes() == originalFrontAttributes);
      REQUIRE(getFace(*brushNode, iRight).attributes() == originalRightAttributes);

      map.redoCommand();

      REQUIRE(getFace(*brushNode, iFront).attributes() == modifiedFrontAttributes);
      REQUIRE(getFace(*brushNode, iRight).attributes() == modifiedRightAttributes);
    }
  }
}

} // namespace tb::mdl
