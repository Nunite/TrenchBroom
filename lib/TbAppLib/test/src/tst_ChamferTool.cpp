/*
 Copyright (C) 2026 XiangXtreme

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

#include "mdl/BrushBuilder.h"
#include "mdl/BrushNode.h"
#include "mdl/CatchConfig.h"
#include "mdl/EditorContext.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/NodeHandleManager.h"
#include "mdl/NodeHandles.h"
#include "ui/ChamferTool.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"

#include "kd/result.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("ChamferTool")
{
  auto fixture = MapDocumentFixture{};
  auto& document = fixture.create();
  auto& map = document.map();

  const auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
  auto* brushNode = new mdl::BrushNode{
    builder.createCuboid(vm::bbox3d{{-16, -16, -16}, {16, 16, 16}}, "material")
    | kdl::value()};
  mdl::addNodes(map, {{map.editorContext().currentLayer(), {brushNode}}});
  mdl::selectNodes(map, {brushNode});

  auto tool = ChamferTool{document};

  SECTION("activation defaults to edge handles")
  {
    REQUIRE(tool.activate());

    CHECK(tool.target() == ChamferTarget::Edges);
    CHECK(tool.edgeTool().active());
    CHECK_FALSE(tool.vertexTool().active());
    CHECK(map.nodeHandles().allHandles<mdl::EdgeHandle>().size() == 12u);
    CHECK(map.nodeHandles().allHandles<mdl::VertexHandle>().empty());

    REQUIRE(tool.deactivate());
    CHECK(map.nodeHandles().allHandles<mdl::EdgeHandle>().empty());
  }

  SECTION("switching target replaces edge handles with vertex handles")
  {
    REQUIRE(tool.activate());

    tool.setTarget(ChamferTarget::Vertices);

    CHECK_FALSE(tool.edgeTool().active());
    CHECK(tool.vertexTool().active());
    CHECK(map.nodeHandles().allHandles<mdl::EdgeHandle>().size() == 12u);
    CHECK(map.nodeHandles().allHandles<mdl::VertexHandle>().size() == 8u);
  }

  SECTION("applies a segmented edge chamfer and supports undo")
  {
    REQUIRE(tool.activate());
    const auto edge = mdl::EdgeHandle{vm::segment3d{{-16, -16, 16}, {16, -16, 16}}};
    const auto edgeHit =
      mdl::Hit{mdl::EdgeHandle::HandleHitType, 0.0, edge.position.center(), edge};
    tool.edgeTool().select({edgeHit}, false);

    const auto oldFaceCount = brushNode->brush().faceCount();
    CHECK(tool.hasPreview());
    CHECK(brushNode->brush().faceCount() == oldFaceCount);

    tool.setParameters(ChamferParameters{4.0, 2});
    REQUIRE(tool.hasPreview());
    REQUIRE(tool.canApply());
    CHECK(brushNode->brush().faceCount() == oldFaceCount);
    REQUIRE(tool.apply());

    CHECK(brushNode->brush().faceCount() == oldFaceCount + 2u);
    CHECK_FALSE(brushNode->brush().hasEdge(edge.position));

    map.undoCommand();
    CHECK(brushNode->brush().faceCount() == oldFaceCount);
    CHECK(brushNode->brush().hasEdge(edge.position));
  }

  SECTION("applies a vertex chamfer")
  {
    REQUIRE(tool.activate());
    tool.setTarget(ChamferTarget::Vertices);

    const auto vertex = mdl::VertexHandle{vm::vec3d{16, 16, 16}};
    const auto vertexHit =
      mdl::Hit{mdl::VertexHandle::HandleHitType, 0.0, vertex.position, vertex};
    tool.vertexTool().select({vertexHit}, false);

    const auto oldFaceCount = brushNode->brush().faceCount();
    tool.setParameters(ChamferParameters{4.0, 1});
    REQUIRE(tool.hasPreview());
    CHECK(brushNode->brush().faceCount() == oldFaceCount);
    REQUIRE(tool.apply());

    CHECK(brushNode->brush().faceCount() == oldFaceCount + 1u);
    CHECK_FALSE(brushNode->brush().hasVertex(vertex.position));
  }

  SECTION("requires selected handles and valid parameters")
  {
    REQUIRE(tool.activate());

    CHECK_FALSE(tool.hasPreview());
    CHECK_FALSE(tool.canApply());
    CHECK_FALSE(tool.apply());

    const auto edge = mdl::EdgeHandle{vm::segment3d{{-16, -16, 16}, {16, -16, 16}}};
    const auto edgeHit =
      mdl::Hit{mdl::EdgeHandle::HandleHitType, 0.0, edge.position.center(), edge};
    tool.edgeTool().select({edgeHit}, false);
    REQUIRE(tool.hasPreview());

    tool.setParameters(ChamferParameters{0.0, 1});
    CHECK_FALSE(tool.hasPreview());
    CHECK_FALSE(tool.canApply());

    tool.setParameters(ChamferParameters{4.0, 0});
    CHECK_FALSE(tool.hasPreview());
    CHECK_FALSE(tool.canApply());
  }

  SECTION("invalid geometry clears the preview")
  {
    REQUIRE(tool.activate());

    const auto edge = mdl::EdgeHandle{vm::segment3d{{-16, -16, 16}, {16, -16, 16}}};
    const auto edgeHit =
      mdl::Hit{mdl::EdgeHandle::HandleHitType, 0.0, edge.position.center(), edge};
    tool.edgeTool().select({edgeHit}, false);
    REQUIRE(tool.hasPreview());

    tool.setParameters(ChamferParameters{64.0, 1});
    CHECK_FALSE(tool.hasPreview());
    CHECK(tool.previewFailed());
    CHECK_FALSE(tool.canApply());
    CHECK(brushNode->brush().faceCount() == 6u);
  }

  SECTION("deactivation discards the preview")
  {
    REQUIRE(tool.activate());

    const auto edge = mdl::EdgeHandle{vm::segment3d{{-16, -16, 16}, {16, -16, 16}}};
    const auto edgeHit =
      mdl::Hit{mdl::EdgeHandle::HandleHitType, 0.0, edge.position.center(), edge};
    tool.edgeTool().select({edgeHit}, false);
    REQUIRE(tool.hasPreview());

    REQUIRE(tool.deactivate());
    CHECK_FALSE(tool.hasPreview());
  }
}

} // namespace tb::ui
