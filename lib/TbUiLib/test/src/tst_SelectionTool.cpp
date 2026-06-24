/*
 Copyright (C) 2022 Kristian Duske

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

#include "PreferenceManager.h"
#include "Preferences.h"
#include "gl/OrthographicCamera.h"
#include "mdl/Brush.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/GameInfo.h"
#include "mdl/Group.h"
#include "mdl/GroupNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Picking.h"
#include "mdl/Map_Selection.h"
#include "mdl/PickResult.h"
#include "mdl/TestUtils.h"
#include "mdl/WorldNode.h"
#include "ui/CatchConfig.h"
#include "ui/GestureTracker.h"
#include "ui/InputState.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/PickRequest.h"
#include "ui/SelectionTool.h"

#include "kd/result.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

namespace tb::ui
{
using namespace Catch::Matchers;

TEST_CASE("SelectionTool")
{
  auto fixture = MapDocumentFixture{};
  auto& document = fixture.create();
  auto& map = document.map();

  SECTION("clicking")
  {
    const auto& worldNode = map.worldNode();
    auto builder = mdl::BrushBuilder{
      worldNode.mapFormat(),
      map.worldBounds(),
      map.gameInfo().gameConfig.faceAttribsConfig.defaults};

    auto tool = SelectionTool{document};

    GIVEN("A group node")
    {
      auto* brushNode =
        new mdl::BrushNode{builder.createCube(32.0, "some_face") | kdl::value()};
      auto* entityNode = new mdl::EntityNode{mdl::Entity{{{"origin", "64 0 0"}}}};
      auto* groupNode = new mdl::GroupNode(mdl::Group{"some_group"});

      addNodes(map, {{parentForNodes(map), {groupNode}}});
      addNodes(map, {{groupNode, {brushNode, entityNode}}});

      auto camera = gl::OrthographicCamera{};

      AND_GIVEN("A pick ray that points at the top face of the brush")
      {
        camera.moveTo({0, 0, 32});
        camera.setDirection({0, 0, -1}, {0, 1, 0});

        const auto pickRay = vm::ray3d{camera.pickRay({0, 0, 0})};

        auto pickResult = mdl::PickResult{};
        pick(map, pickRay, pickResult);
        REQUIRE(pickResult.all().size() == 1);

        REQUIRE(map.selection().brushFaces.empty());

        auto inputState = InputState{};
        inputState.setPickRequest({pickRay, camera});
        inputState.setPickResult(std::move(pickResult));

        WHEN("I click once")
        {
          inputState.mouseDown(MouseButtons::Left);
          tool.mouseClick(inputState);
          inputState.mouseUp(MouseButtons::Left);

          THEN("The group gets selected")
          {
            CHECK(map.selection().brushFaces.empty());
            CHECK(map.selection() == mdl::makeSelection(map, {groupNode}));
          }
        }

        WHEN("I double click")
        {
          inputState.mouseDown(MouseButtons::Left);
          tool.mouseDoubleClick(inputState);
          inputState.mouseUp(MouseButtons::Left);

          THEN("The group is opened")
          {
            CHECK(map.selection().brushFaces.empty());
            CHECK_FALSE(map.selection().hasNodes());
            CHECK(map.editorContext().currentGroup() == groupNode);
          }
        }
      }
    }

    GIVEN("A brush node and an entity node")
    {
      auto brush = builder.createCube(
                     32.0,
                     "left_face",
                     "right_face",
                     "front_face",
                     "back_face",
                     "top_face",
                     "bottom_face")
                   | kdl::value();
      auto* brushNode = new mdl::BrushNode{std::move(brush)};

      const auto topFaceIndex = *brushNode->brush().findFace("top_face");
      const auto frontFaceIndex = *brushNode->brush().findFace("front_face");

      auto* entityNode = new mdl::EntityNode{mdl::Entity{{{"origin", "64 0 0"}}}};

      addNodes(map, {{parentForNodes(map), {brushNode, entityNode}}});

      auto camera = gl::OrthographicCamera{};

      AND_GIVEN("A pick ray that points at the top face of the brush")
      {
        camera.moveTo({0, 0, 32});
        camera.setDirection({0, 0, -1}, {0, 1, 0});

        const auto pickRay = vm::ray3d{camera.pickRay({0, 0, 0})};

        auto pickResult = mdl::PickResult{};
        pick(map, pickRay, pickResult);
        REQUIRE(pickResult.all().size() == 1);

        REQUIRE(map.selection().brushFaces.empty());

        auto inputState = InputState{};
        inputState.setPickRequest({pickRay, camera});
        inputState.setPickResult(std::move(pickResult));

        WHEN("I shift click once")
        {
          inputState.setModifierKeys(ModifierKeys::Shift);
          inputState.mouseDown(MouseButtons::Left);
          tool.mouseClick(inputState);
          inputState.mouseUp(MouseButtons::Left);

          THEN("The top face get selected")
          {
            CHECK(
              map.selection().brushFaces
              == std::vector<mdl::BrushFaceHandle>{{brushNode, topFaceIndex}});
            CHECK_FALSE(map.selection().hasNodes());
          }

          AND_WHEN("I shift click on the selected face again")
          {
            inputState.setModifierKeys(ModifierKeys::Shift);
            inputState.mouseDown(MouseButtons::Left);
            tool.mouseClick(inputState);
            inputState.mouseUp(MouseButtons::Left);

            THEN("The top face remains selected")
            {
              CHECK(
                map.selection().brushFaces
                == std::vector<mdl::BrushFaceHandle>{{brushNode, topFaceIndex}});
              CHECK_FALSE(map.selection().hasNodes());
            }
          }

          AND_WHEN("I shift+ctrl click on the selected face again")
          {
            inputState.setModifierKeys(ModifierKeys::Shift | ModifierKeys::CtrlCmd);
            inputState.mouseDown(MouseButtons::Left);
            tool.mouseClick(inputState);
            inputState.mouseUp(MouseButtons::Left);

            THEN("The top face gets deselected")
            {
              CHECK(map.selection().brushFaces.empty());
              CHECK_FALSE(map.selection().hasNodes());
            }
          }
        }

        WHEN("I click once")
        {
          inputState.mouseDown(MouseButtons::Left);
          tool.mouseClick(inputState);
          inputState.mouseUp(MouseButtons::Left);

          THEN("The brush gets selected")
          {
            CHECK(map.selection().brushFaces.empty());
            CHECK(map.selection() == mdl::makeSelection(map, {brushNode}));
          }

          AND_WHEN("I click on the selected brushagain")
          {
            inputState.mouseDown(MouseButtons::Left);
            tool.mouseClick(inputState);
            inputState.mouseUp(MouseButtons::Left);

            THEN("The brush remains selected")
            {
              CHECK(map.selection().brushFaces.empty());
              CHECK(map.selection() == mdl::makeSelection(map, {brushNode}));
            }
          }

          AND_WHEN("I ctrl click on the selected brush again")
          {
            inputState.setModifierKeys(ModifierKeys::CtrlCmd);
            inputState.mouseDown(MouseButtons::Left);
            tool.mouseClick(inputState);
            inputState.mouseUp(MouseButtons::Left);

            THEN("The brush gets deselected")
            {
              CHECK(map.selection().brushFaces.empty());
              CHECK_FALSE(map.selection().hasNodes());
            }
          }
        }

        WHEN("I shift double click")
        {
          inputState.setModifierKeys(ModifierKeys::Shift);
          inputState.mouseDown(MouseButtons::Left);
          tool.mouseDoubleClick(inputState);
          inputState.mouseUp(MouseButtons::Left);

          THEN("All brush faces are selected")
          {
            CHECK(map.selection().brushFaces.size() == 6);
            CHECK_FALSE(map.selection().hasNodes());
          }
        }

        WHEN("I double click")
        {
          inputState.mouseDown(MouseButtons::Left);
          tool.mouseDoubleClick(inputState);
          inputState.mouseUp(MouseButtons::Left);

          THEN("All nodes are selected")
          {
            CHECK(map.selection().brushFaces.empty());
            CHECK(map.selection() == mdl::makeSelection(map, {brushNode, entityNode}));
          }
        }

        AND_GIVEN("The front face of the brush is selected")
        {
          selectBrushFaces(map, {{brushNode, frontFaceIndex}});

          WHEN("I shift click once")
          {
            inputState.setModifierKeys(ModifierKeys::Shift);
            inputState.mouseDown(MouseButtons::Left);
            tool.mouseClick(inputState);
            inputState.mouseUp(MouseButtons::Left);

            THEN("The top face get selected")
            {
              CHECK(
                map.selection().brushFaces
                == std::vector<mdl::BrushFaceHandle>{{brushNode, topFaceIndex}});
              CHECK_FALSE(map.selection().hasNodes());
            }
          }

          WHEN("I shift+ctrl click once")
          {
            inputState.setModifierKeys(ModifierKeys::Shift | ModifierKeys::CtrlCmd);
            inputState.mouseDown(MouseButtons::Left);
            tool.mouseClick(inputState);
            inputState.mouseUp(MouseButtons::Left);

            THEN("Both the front and the top faces are selected")
            {
              CHECK_THAT(
                map.selection().brushFaces,
                UnorderedEquals(std::vector<mdl::BrushFaceHandle>{
                  {brushNode, topFaceIndex}, {brushNode, frontFaceIndex}}));
              CHECK_FALSE(map.selection().hasNodes());
            }
          }

          WHEN("I click once")
          {
            inputState.mouseDown(MouseButtons::Left);
            tool.mouseClick(inputState);
            inputState.mouseUp(MouseButtons::Left);

            THEN("The brush gets selected")
            {
              CHECK(map.selection().brushFaces.empty());
              CHECK(map.selection() == mdl::makeSelection(map, {brushNode}));
            }
          }

          WHEN("I ctrl click once")
          {
            inputState.setModifierKeys(ModifierKeys::CtrlCmd);
            inputState.mouseDown(MouseButtons::Left);
            tool.mouseClick(inputState);
            inputState.mouseUp(MouseButtons::Left);

            THEN("The brush gets selected")
            {
              CHECK(map.selection().brushFaces.empty());
              CHECK(map.selection() == mdl::makeSelection(map, {brushNode}));
            }
          }
        }

        AND_GIVEN("The entity is selected")
        {
          selectNodes(map, {entityNode});

          WHEN("I shift click once")
          {
            inputState.setModifierKeys(ModifierKeys::Shift);
            inputState.mouseDown(MouseButtons::Left);
            tool.mouseClick(inputState);
            inputState.mouseUp(MouseButtons::Left);

            THEN("The top face get selected")
            {
              CHECK(
                map.selection().brushFaces
                == std::vector<mdl::BrushFaceHandle>{{brushNode, topFaceIndex}});
              CHECK_FALSE(map.selection().hasNodes());
            }
          }

          WHEN("I shift+ctrl click once")
          {
            inputState.setModifierKeys(ModifierKeys::Shift | ModifierKeys::CtrlCmd);
            inputState.mouseDown(MouseButtons::Left);
            tool.mouseClick(inputState);
            inputState.mouseUp(MouseButtons::Left);

            THEN("The top face get selected")
            {
              CHECK(
                map.selection().brushFaces
                == std::vector<mdl::BrushFaceHandle>{{brushNode, topFaceIndex}});
              CHECK_FALSE(map.selection().hasNodes());
            }
          }

          WHEN("I click once")
          {
            inputState.mouseDown(MouseButtons::Left);
            tool.mouseClick(inputState);
            inputState.mouseUp(MouseButtons::Left);

            THEN("The brush gets selected")
            {
              CHECK(map.selection().brushFaces.empty());
              CHECK(map.selection() == mdl::makeSelection(map, {brushNode}));
            }
          }

          WHEN("I ctrl click once")
          {
            inputState.setModifierKeys(ModifierKeys::CtrlCmd);
            inputState.mouseDown(MouseButtons::Left);
            tool.mouseClick(inputState);
            inputState.mouseUp(MouseButtons::Left);

            THEN("The brush and entity both get selected")
            {
              CHECK(map.selection().brushFaces.empty());
              CHECK(map.selection() == mdl::makeSelection(map, {entityNode, brushNode}));
            }
          }
        }

        AND_GIVEN("The top face is hidden")
        {

          auto hiddenTag = mdl::Tag{"hidden", {}};

          auto newBrush = brushNode->brush();
          newBrush.face(topFaceIndex).addTag(hiddenTag);
          updateNodeContents(
            map, "Set Tag", {{brushNode, mdl::NodeContents{std::move(newBrush)}}});

          REQUIRE(brushNode->brush().face(topFaceIndex).hasTag(hiddenTag));

          map.editorContext().setHiddenTags(hiddenTag.type());
          REQUIRE_FALSE(map.editorContext().visible(
            *brushNode, brushNode->brush().face(topFaceIndex)));

          WHEN("I shift click once")
          {
            inputState.setModifierKeys(ModifierKeys::Shift);
            inputState.mouseDown(MouseButtons::Left);
            tool.mouseClick(inputState);
            inputState.mouseUp(MouseButtons::Left);

            THEN("Nothing happens")
            {
              CHECK(map.selection().brushFaces.empty());
              CHECK_FALSE(map.selection().hasNodes());
            }
          }

          WHEN("I click once")
          {
            inputState.mouseDown(MouseButtons::Left);
            tool.mouseClick(inputState);
            inputState.mouseUp(MouseButtons::Left);

            THEN("Nothing happens")
            {
              CHECK(map.selection().brushFaces.empty());
              CHECK_FALSE(map.selection().hasNodes());
            }
          }
        }
      }
    }
  }

  SECTION("2D box selection")
  {
    auto& prefs = PreferenceManager::instance();
    prefs.set(Preferences::Enable2DBoxSelection, false);

    const auto& worldNode = map.worldNode();
    auto builder = mdl::BrushBuilder{
      worldNode.mapFormat(),
      map.worldBounds(),
      map.gameInfo().gameConfig.faceAttribsConfig.defaults};

    auto* insideBrushNode = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{-16, -16, -16}, {16, 16, 16}}, "inside")
      | kdl::value()};
    auto* outsideBrushNode = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{96, 96, -16}, {128, 128, 16}}, "outside")
      | kdl::value()};
    addNodes(map, {{parentForNodes(map), {insideBrushNode, outsideBrushNode}}});

    auto camera = gl::OrthographicCamera{};
    camera.moveTo({0, 0, 128});
    camera.setDirection({0, 0, -1}, {0, 1, 0});

    const auto makeInputState = [&](const vm::vec3d& point) {
      auto inputState = InputState{};
      inputState.setModifierKeys(ModifierKeys::CtrlCmd);
      inputState.mouseDown(MouseButtons::Left);
      inputState.setPickRequest({vm::ray3d{point, {0, 0, -1}}, camera});
      inputState.setPickResult(mdl::PickResult{});
      return inputState;
    };

    auto tool = SelectionTool{document};

    SECTION("disabled")
    {
      auto inputState = makeInputState({-48, -48, 128});
      CHECK(tool.acceptMouseDrag(inputState) == nullptr);
    }

    SECTION("enabled")
    {
      prefs.set(Preferences::Enable2DBoxSelection, true);

      auto startInputState = makeInputState({-48, -48, 128});
      auto tracker = tool.acceptMouseDrag(startInputState);
      REQUIRE(tracker != nullptr);

      auto endInputState = makeInputState({48, 48, 128});
      REQUIRE(tracker->update(endInputState));
      endInputState.mouseUp(MouseButtons::Left);
      tracker->end(endInputState);

      CHECK(map.selection() == mdl::makeSelection(map, {insideBrushNode}));
    }

    SECTION("enabled with selectable containers")
    {
      prefs.set(Preferences::Enable2DBoxSelection, true);

      auto* groupBrushNode = new mdl::BrushNode{
        builder.createCuboid(vm::bbox3d{{-64, -64, -16}, {-48, -48, 16}}, "group")
        | kdl::value()};
      auto* groupNode = new mdl::GroupNode{mdl::Group{"group"}};
      addNodes(map, {{parentForNodes(map), {groupNode}}});
      addNodes(map, {{groupNode, {groupBrushNode}}});

      auto* entityBrushNode = new mdl::BrushNode{
        builder.createCuboid(vm::bbox3d{{48, 48, -16}, {64, 64, 16}}, "brush_entity")
        | kdl::value()};
      auto* brushEntityNode =
        new mdl::EntityNode{mdl::Entity{{{"classname", "func_wall"}}}};
      addNodes(map, {{parentForNodes(map), {brushEntityNode}}});
      addNodes(map, {{brushEntityNode, {entityBrushNode}}});

      auto startInputState = makeInputState({-80, -80, 128});
      auto tracker = tool.acceptMouseDrag(startInputState);
      REQUIRE(tracker != nullptr);

      auto endInputState = makeInputState({80, 80, 128});
      REQUIRE(tracker->update(endInputState));
      endInputState.mouseUp(MouseButtons::Left);
      tracker->end(endInputState);

      CHECK_THAT(
        map.selection().nodes,
        UnorderedEquals(
          std::vector<mdl::Node*>{insideBrushNode, groupNode, entityBrushNode}));
    }
  }

  SECTION("clickingThroughHidden")
  {
    const auto& worldNode = map.worldNode();
    auto builder = mdl::BrushBuilder{
      worldNode.mapFormat(),
      map.worldBounds(),
      map.gameInfo().gameConfig.faceAttribsConfig.defaults};

    auto tool = SelectionTool{document};

    GIVEN("A brush visible behind the hidden face of another brush")
    {
      auto visibleBrush = builder.createCube(
                            32.0,
                            "left_face",
                            "right_face",
                            "front_face",
                            "back_face",
                            "top_face",
                            "bottom_face")
                          | kdl::value();
      auto* visibleBrushNode = new mdl::BrushNode{std::move(visibleBrush)};
      const auto visibleTopFaceIndex = *visibleBrushNode->brush().findFace("top_face");

      auto hiddenBrush = builder.createCube(
                           64.0,
                           "left_face",
                           "right_face",
                           "front_face",
                           "back_face",
                           "top_face",
                           "bottom_face")
                         | kdl::value();
      auto* hiddenBrushNode = new mdl::BrushNode{std::move(hiddenBrush)};
      const auto hiddenTopFaceIndex = *hiddenBrushNode->brush().findFace("top_face");

      addNodes(map, {{parentForNodes(map), {visibleBrushNode, hiddenBrushNode}}});

      const auto hiddenTag = mdl::Tag{"hidden", {}};
      auto taggedBrush = hiddenBrushNode->brush();
      taggedBrush.face(hiddenTopFaceIndex).addTag(hiddenTag);
      updateNodeContents(
        map, "Set Tag", {{hiddenBrushNode, mdl::NodeContents{std::move(taggedBrush)}}});

      map.editorContext().setHiddenTags(hiddenTag.type());

      REQUIRE(hiddenBrushNode->brush().face(hiddenTopFaceIndex).hasTag(hiddenTag));
      CHECK_FALSE(map.editorContext().visible(
        *hiddenBrushNode, hiddenBrushNode->brush().face(hiddenTopFaceIndex)));

      auto camera = gl::OrthographicCamera{};
      AND_GIVEN("A pick ray that points at the top face of the brushes")
      {
        camera.moveTo({0, 0, 128});
        camera.setDirection({0, 0, -1}, {0, 1, 0});

        const auto pickRay = vm::ray3d{camera.pickRay({0, 0, 0})};

        auto pickResult = mdl::PickResult{};
        pick(map, pickRay, pickResult);
        CHECK(pickResult.all().size() == 2);
        REQUIRE(map.selection().brushFaces.empty());

        auto inputState = InputState{};
        inputState.setPickRequest({pickRay, camera});
        inputState.setPickResult(std::move(pickResult));

        WHEN("I shift click once")
        {
          inputState.setModifierKeys(ModifierKeys::Shift);
          inputState.mouseDown(MouseButtons::Left);
          tool.mouseClick(inputState);
          inputState.mouseUp(MouseButtons::Left);

          THEN("The top face of the visible brush get selected")
          {
            CHECK_FALSE(map.selection().hasNodes());
            CHECK(
              map.selection().brushFaces
              == std::vector<mdl::BrushFaceHandle>{
                {visibleBrushNode, visibleTopFaceIndex}});
          }
        }

        WHEN("I click once")
        {
          inputState.mouseDown(MouseButtons::Left);
          tool.mouseClick(inputState);
          inputState.mouseUp(MouseButtons::Left);

          THEN("The visible brush gets selected")
          {
            CHECK(map.selection().brushFaces.empty());
            CHECK(map.selection() == mdl::makeSelection(map, {visibleBrushNode}));
          }
        }
      }
    }
  }
}

} // namespace tb::ui
