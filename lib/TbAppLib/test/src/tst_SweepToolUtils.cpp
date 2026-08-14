/*
 Copyright (C) 2026 Jackson Palmer

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

#include "gl/Material.h"
#include "gl/MaterialCollection.h"
#include "gl/MaterialManager.h"
#include "gl/TextureResource.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/CatchConfig.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/Map.h" // IWYU pragma: keep
#include "mdl/Map_Nodes.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/SweepToolUtils.h"

#include "vm/approx.h"
#include "vm/bbox.h"
#include "vm/constants.h"
#include "vm/mat.h"
#include "vm/polygon.h"
#include "vm/quat.h"
#include "vm/vec.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <optional>
#include <ranges>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

namespace
{

std::optional<size_t> findFaceContaining(
  const mdl::Brush& brush, const std::initializer_list<vm::vec3d> points)
{
  for (size_t faceIndex = 0; faceIndex < brush.faceCount(); ++faceIndex)
  {
    const auto vertices = brush.face(faceIndex).vertexPositions();
    const auto containsAllPoints = std::ranges::all_of(points, [&](const auto& point) {
      return std::ranges::any_of(
        vertices, [&](const auto& vertex) { return vm::is_equal(vertex, point, 0.001); });
    });
    if (containsAllPoints)
    {
      return faceIndex;
    }
  }
  return std::nullopt;
}

bool sameUvCoordinates(const vm::vec2f& lhs, const vm::vec2f& rhs)
{
  return vm::is_equal(lhs, rhs, 0.001f);
}

const mdl::BrushFace* findFaceContainingPoint(
  const std::vector<const mdl::BrushFace*>& faces, const vm::vec3d& point)
{
  const auto it = std::ranges::find_if(faces, [&](const auto* face) {
    return std::ranges::any_of(face->vertexPositions(), [&](const auto& vertex) {
      return vm::is_equal(vertex, point, 0.001);
    });
  });
  return it != faces.end() ? *it : nullptr;
}

void checkAffineUvMapping(const mdl::BrushFace& face)
{
  CHECK(!vm::is_nan(face.uAxis()));
  CHECK(!vm::is_nan(face.vAxis()));
  CHECK(vm::length(face.uAxis()) > 0.000001);
  CHECK(vm::length(face.vAxis()) > 0.000001);
  CHECK(face.vertexPositions().size() == 4u);
}

void checkUsableUvMapping(const mdl::BrushFace& face)
{
  CHECK(!vm::is_nan(face.uAxis()));
  CHECK(!vm::is_nan(face.vAxis()));
  CHECK(vm::length(face.uAxis()) > 0.05);
  CHECK(vm::length(face.vAxis()) > 0.05);
  CHECK(std::abs(vm::dot(face.uAxis(), face.vAxis())) < 2.0);
}

double uvCondition(const mdl::BrushFace& face)
{
  const auto u = face.uAxis() / double(face.uvAttributes().scale.x());
  const auto v = face.vAxis() / double(face.uvAttributes().scale.y());
  const auto a = vm::squared_length(u);
  const auto b = vm::dot(u, v);
  const auto c = vm::squared_length(v);
  const auto trace = a + c;
  const auto discriminant = std::sqrt(std::max(0.0, (a - c) * (a - c) + 4.0 * b * b));
  const auto lambdaMin = 0.5 * (trace - discriminant);
  const auto lambdaMax = 0.5 * (trace + discriminant);
  return lambdaMin > 0.0 ? std::sqrt(lambdaMax / lambdaMin)
                         : std::numeric_limits<double>::infinity();
}

} // namespace

TEST_CASE("SweepTransform")
{
  SECTION("destinationCenter")
  {
    const auto source = SweepSource{{}, vm::vec3d{8, 8, 8}, {}, {}};
    const auto transform = SweepTransform{vm::vec3d{16, 0, -8}};
    CHECK(transform.destinationCenter(source) == vm::vec3d{24, 8, 0});
  }

  SECTION("effectiveRotation")
  {
    auto transform = SweepTransform{};

    SECTION("returns rotations up to a half turn unchanged")
    {
      transform.rotation = vm::quatd{vm::vec3d{0, 0, 1}, vm::Cd::half_pi()};

      const auto rotation = transform.effectiveRotation();
      CHECK(rotation.angle() == vm::approx{vm::Cd::half_pi()});
      CHECK(rotation.axis() == vm::approx{vm::vec3d{0, 0, 1}});
    }

    SECTION("replaces rotations beyond a half turn with the shorter turn")
    {
      transform.rotation = vm::quatd{vm::vec3d{0, 0, 1}, 3.0 * vm::Cd::half_pi()};

      const auto rotation = transform.effectiveRotation();
      CHECK(rotation.angle() == vm::approx{vm::Cd::half_pi()});
      CHECK(rotation.axis() == vm::approx{vm::vec3d{0, 0, -1}});
    }

    SECTION("treats a full turn as no rotation")
    {
      transform.rotation = vm::quatd{vm::vec3d{0, 0, 1}, vm::Cd::two_pi()};

      CHECK(transform.effectiveRotation().angle() == vm::approx{0.0});
    }
  }

  SECTION("isNoOp")
  {
    auto transform = SweepTransform{};
    CHECK(transform.isNoOp());

    SECTION("translation")
    {
      transform.translation = vm::vec3d{1, 0, 0};
      CHECK(!transform.isNoOp());
    }

    SECTION("rotation")
    {
      transform.rotation = vm::quatd{vm::vec3d{0, 0, 1}, vm::Cd::half_pi()};
      CHECK(!transform.isNoOp());
    }

    SECTION("scale")
    {
      transform.scale = vm::vec3d{2, 2, 2};
      CHECK(!transform.isNoOp());
    }

    SECTION("a full turn")
    {
      transform.rotation = vm::quatd{vm::vec3d{0, 0, 1}, vm::Cd::two_pi()};
      CHECK(transform.isNoOp());
    }
  }
}

TEST_CASE("SweepToolUtils functions")
{
  SECTION("stationTransform")
  {
    auto source = SweepSource{{}, vm::vec3d{0, 0, 0}, vm::vec3d{1, 0, 0}, {}};
    auto transform = SweepTransform{};
    auto parameters = SweepParameters{};

    const auto station = [&](const double t, const vm::vec3d& point) {
      return stationTransform(
               source, transform, parameters, t, transform.effectiveRotation())
             * point;
    };

    SECTION("straight path interpolates the translation")
    {
      transform.translation = vm::vec3d{64, 0, 0};
      parameters.pathMode = SweepPathMode::Straight;

      CHECK(station(0.0, vm::vec3d{0, 16, 0}) == vm::approx{vm::vec3d{0, 16, 0}});
      CHECK(station(0.5, vm::vec3d{0, 16, 0}) == vm::approx{vm::vec3d{32, 16, 0}});
      CHECK(station(1.0, vm::vec3d{0, 16, 0}) == vm::approx{vm::vec3d{64, 16, 0}});
    }

    SECTION("straight path scales the rotation angle with t")
    {
      transform.rotation = vm::quatd{vm::vec3d{0, 0, 1}, vm::Cd::half_pi()};
      parameters.pathMode = SweepPathMode::Straight;

      const auto d = 16.0 / std::sqrt(2.0);
      CHECK(station(0.5, vm::vec3d{16, 0, 0}) == vm::approx{vm::vec3d{d, d, 0}});
      CHECK(station(1.0, vm::vec3d{16, 0, 0}) == vm::approx{vm::vec3d{0, 16, 0}});
    }

    SECTION("straight path interpolates the scale about the source center")
    {
      transform.scale = vm::vec3d{2, 2, 2};
      parameters.pathMode = SweepPathMode::Straight;

      CHECK(station(0.5, vm::vec3d{16, 0, 0}) == vm::approx{vm::vec3d{24, 0, 0}});
      CHECK(station(1.0, vm::vec3d{16, 0, 0}) == vm::approx{vm::vec3d{32, 0, 0}});
    }

    SECTION("arc path revolves the source about the derived pivot")
    {
      // a quarter turn from {0,0,0} to {64,64,0} fits a circle about {0,64,0}
      transform.translation = vm::vec3d{64, 64, 0};
      transform.rotation = vm::quatd{vm::vec3d{0, 0, 1}, vm::Cd::half_pi()};
      parameters.pathMode = SweepPathMode::Arc;

      const auto d = 64.0 / std::sqrt(2.0);
      CHECK(station(0.0, source.center) == vm::approx{source.center});
      CHECK(station(0.5, source.center) == vm::approx{vm::vec3d{d, 64.0 - d, 0}});
      CHECK(station(1.0, source.center) == vm::approx{vm::vec3d{64, 64, 0}});
    }

    SECTION("arc path applies translation along the axis as a lift")
    {
      transform.translation = vm::vec3d{64, 64, 32};
      transform.rotation = vm::quatd{vm::vec3d{0, 0, 1}, vm::Cd::half_pi()};
      parameters.pathMode = SweepPathMode::Arc;

      const auto d = 64.0 / std::sqrt(2.0);
      CHECK(station(0.5, source.center) == vm::approx{vm::vec3d{d, 64.0 - d, 16}});
      CHECK(station(1.0, source.center) == vm::approx{vm::vec3d{64, 64, 32}});
    }

    SECTION("arc path without a usable pivot behaves like the straight path")
    {
      // no travel perpendicular to the rotation axis, so no circle fits
      transform.translation = vm::vec3d{0, 0, 64};
      transform.rotation = vm::quatd{vm::vec3d{0, 0, 1}, vm::Cd::half_pi()};
      parameters.pathMode = SweepPathMode::Arc;

      CHECK(station(1.0, vm::vec3d{16, 0, 0}) == vm::approx{vm::vec3d{0, 16, 64}});
    }

    SECTION("arc path without a rotation behaves like the straight path")
    {
      transform.translation = vm::vec3d{64, 0, 0};
      parameters.pathMode = SweepPathMode::Arc;

      CHECK(station(0.5, vm::vec3d{0, 16, 0}) == vm::approx{vm::vec3d{32, 16, 0}});
    }

    SECTION("s-bend path bulges toward the source normal")
    {
      transform.translation = vm::vec3d{0, 64, 0};
      parameters.pathMode = SweepPathMode::SBend;

      // the Hermite basis at t=1/4 weighs the tangents with 9/64 and -3/64 and the
      // translation with 5/32; both tangents are source.normal * 64
      CHECK(station(0.0, source.center) == vm::approx{source.center});
      CHECK(station(0.25, source.center) == vm::approx{vm::vec3d{6, 10, 0}});
      CHECK(station(1.0, source.center) == vm::approx{vm::vec3d{0, 64, 0}});
    }

    SECTION("s-bend path requires a non-zero source normal")
    {
      source.normal = vm::vec3d{0, 0, 0};
      transform.translation = vm::vec3d{0, 64, 0};
      parameters.pathMode = SweepPathMode::SBend;

      CHECK(station(0.25, source.center) == vm::approx{vm::vec3d{0, 16, 0}});
    }
  }

  SECTION("generateSweepBrushes")
  {
    auto fixture = MapDocumentFixture{};
    auto& document = fixture.create(mdl::QuakeFixtureConfig);
    auto& map = document.map();

    auto& defaultParent = parentForNodes(map);

    const auto squareAt = [](const double x) {
      return vm::polygon3d{
        {x, -16, -16},
        {x, -16, 16},
        {x, 16, 16},
        {x, 16, -16},
      };
    };

    auto source = SweepSource{
      {SweepFace{squareAt(0.0), &defaultParent}},
      vm::vec3d{0, 0, 0},
      vm::vec3d{1, 0, 0},
      vm::vec3d{0, 16, 16},
    };
    auto transform = SweepTransform{};
    auto parameters =
      SweepParameters{1, 1, SweepPathMode::Straight, SweepAlignment::Free};

    SECTION("creates one brush per segment")
    {
      transform.translation = vm::vec3d{64, 0, 0};
      parameters.segments = 4;

      const auto result = generateSweepBrushes(map, source, transform, parameters);
      REQUIRE(result.brushes.size() == 1);
      CHECK(result.issues.empty());

      const auto& brushes = result.brushes.at(&defaultParent);
      REQUIRE(brushes.size() == 4);
      CHECK(brushes[0]->logicalBounds() == vm::bbox3d{{0, -16, -16}, {16, 16, 16}});
      CHECK(brushes[3]->logicalBounds() == vm::bbox3d{{48, -16, -16}, {64, 16, 16}});
    }

    SECTION("bridge matches and reaches a differently sized target face exactly")
    {
      const auto targetVertices = std::vector<vm::vec3d>{
        {64, -24, -12},
        {64, -24, 12},
        {64, 24, 12},
        {64, 24, -12},
      };
      const auto target = SweepTarget{
        vm::polygon3d{
          {64, 24, 12},
          {64, -24, 12},
          {64, -24, -12},
          {64, 24, -12},
        },
        vm::vec3d{64, 0, 0},
        vm::vec3d{-1, 0, 0},
        std::nullopt,
      };
      parameters.segments = 3;

      const auto result = generateBridgeBrushes(map, source, target, parameters);
      CHECK(result.issues.empty());
      REQUIRE(result.brushes.contains(&defaultParent));
      const auto& brushes = result.brushes.at(&defaultParent);
      REQUIRE(brushes.size() == 3u);

      const auto containsPoints = [](const mdl::Brush& brush, const auto& points) {
        return std::ranges::any_of(brush.faces(), [&](const auto& face) {
          const auto faceVertices = face.vertexPositions();
          return std::ranges::all_of(points, [&](const auto& point) {
            return std::ranges::any_of(faceVertices, [&](const auto& vertex) {
              return vm::is_equal(vertex, point, vm::Cd::almost_zero());
            });
          });
        });
      };

      CHECK(containsPoints(
        brushes.front()->brush(), source.faces.front().polygon.vertices()));
      CHECK(containsPoints(brushes.back()->brush(), targetVertices));

      for (size_t i = 0u; i + 1u < brushes.size(); ++i)
      {
        const auto& lhs = brushes[i]->brush();
        const auto& rhs = brushes[i + 1u]->brush();
        const auto hasSharedCap =
          std::ranges::any_of(lhs.faces(), [&](const auto& lhsFace) {
            const auto lhsVertices = lhsFace.vertexPositions();
            return lhsVertices.size() == 4u
                   && std::ranges::any_of(rhs.faces(), [&](const auto& rhsFace) {
                        const auto rhsVertices = rhsFace.vertexPositions();
                        return rhsVertices.size() == lhsVertices.size()
                               && std::ranges::all_of(
                                 lhsVertices, [&](const auto& point) {
                                   return std::ranges::any_of(
                                     rhsVertices, [&](const auto& candidate) {
                                       return vm::is_equal(
                                         point, candidate, vm::Cd::almost_zero());
                                     });
                                 });
                      });
          });
        CHECK(hasSharedCap);
      }
    }

    SECTION("bridge rejects mismatched vertex counts")
    {
      const auto target = SweepTarget{
        vm::polygon3d{{64, -16, -16}, {64, 16, 0}, {64, -16, 16}},
        vm::vec3d{64, -16.0 / 3.0, 0},
        vm::vec3d{-1, 0, 0},
        std::nullopt,
      };

      const auto result = generateBridgeBrushes(map, source, target, parameters);
      CHECK(result.brushes.empty());
      REQUIRE(result.issues.size() == 1u);
      CHECK(result.issues.front().message.find("same vertex count") != std::string::npos);
    }

    SECTION("bridge follows an arc into a perpendicular target face")
    {
      const auto targetVertices = std::vector<vm::vec3d>{
        {-16, 64, -16},
        {-16, 64, 16},
        {16, 64, 16},
        {16, 64, -16},
      };
      const auto target = SweepTarget{
        vm::polygon3d{targetVertices},
        vm::vec3d{0, 64, 0},
        vm::vec3d{0, -1, 0},
        std::nullopt,
      };
      parameters.segments = 4;
      parameters.pathMode = SweepPathMode::Arc;

      const auto result = generateBridgeBrushes(map, source, target, parameters);
      CHECK(result.issues.empty());
      REQUIRE(result.brushes.contains(&defaultParent));
      const auto& brushes = result.brushes.at(&defaultParent);
      REQUIRE(brushes.size() == 4u);
      CHECK(std::ranges::all_of(
        brushes, [](const auto& brush) { return brush->brush().fullySpecified(); }));

      const auto& lastBrush = brushes.back()->brush();
      CHECK(std::ranges::any_of(lastBrush.faces(), [&](const auto& face) {
        const auto faceVertices = face.vertexPositions();
        return std::ranges::all_of(targetVertices, [&](const auto& targetVertex) {
          return std::ranges::any_of(faceVertices, [&](const auto& candidate) {
            return vm::is_equal(candidate, targetVertex, vm::Cd::almost_zero());
          });
        });
      }));
    }

    SECTION("iterations continue from the previous destination cap")
    {
      transform.translation = vm::vec3d{64, 0, 0};
      parameters.segments = 2;
      parameters.iterations = 2;

      const auto result = generateSweepBrushes(map, source, transform, parameters);

      const auto& brushes = result.brushes.at(&defaultParent);
      REQUIRE(brushes.size() == 4);
      CHECK(brushes[3]->logicalBounds() == vm::bbox3d{{96, -16, -16}, {128, 16, 16}});
    }

    SECTION("groups the brushes by the parent of their source face")
    {
      auto* entityNode = new mdl::EntityNode{mdl::Entity{}};
      addNodes(map, {{&defaultParent, {entityNode}}});

      source.faces = {
        SweepFace{squareAt(0.0), entityNode},
        SweepFace{squareAt(0.0), nullptr},
      };
      transform.translation = vm::vec3d{64, 0, 0};
      parameters.segments = 2;

      const auto result = generateSweepBrushes(map, source, transform, parameters);
      REQUIRE(result.brushes.size() == 2);
      CHECK(result.brushes.at(entityNode).size() == 2);
      // a face without a parent falls back to the map's default insertion parent
      CHECK(result.brushes.at(&defaultParent).size() == 2);
    }

    SECTION("integer alignment keeps the source station exact and rounds the rest")
    {
      source.faces = {SweepFace{squareAt(0.25), &defaultParent}};
      source.center = vm::vec3d{0.25, 0, 0};
      transform.translation = vm::vec3d{16, 0, 0};
      parameters.segments = 2;
      parameters.alignment = SweepAlignment::Integer;

      // the stations sit at x = 0.25 (exact), round(8.25) = 8 and round(16.25) = 16
      const auto result = generateSweepBrushes(map, source, transform, parameters);

      const auto& brushes = result.brushes.at(&defaultParent);
      REQUIRE(brushes.size() == 2);
      CHECK(brushes[0]->logicalBounds() == vm::bbox3d{{0.25, -16, -16}, {8, 16, 16}});
      CHECK(brushes[1]->logicalBounds() == vm::bbox3d{{8, -16, -16}, {16, 16, 16}});
    }

    SECTION("integer alignment rounds the cap shared between iterations")
    {
      // the destination cap of iteration r continues as the source station of iteration
      // r+1 (station(r=1,s=0) below); both must round the same way or the mesh gets a
      // seam
      transform.translation = vm::vec3d{15.6, 0, 0};
      parameters.segments = 1;
      parameters.iterations = 2;
      parameters.alignment = SweepAlignment::Integer;

      const auto result = generateSweepBrushes(map, source, transform, parameters);

      const auto& brushes = result.brushes.at(&defaultParent);
      REQUIRE(brushes.size() == 2);
      CHECK(brushes[0]->logicalBounds() == vm::bbox3d{{0, -16, -16}, {16, 16, 16}});
      CHECK(brushes[1]->logicalBounds() == vm::bbox3d{{16, -16, -16}, {31, 16, 16}});
    }

    SECTION("skips segments that do not form a valid brush")
    {
      transform.translation = vm::vec3d{1, 0, 0};
      parameters.segments = 2;
      parameters.alignment = SweepAlignment::Integer;

      // rounding collapses the second segment: the stations sit at x = 0, 1 and 1
      const auto result = generateSweepBrushes(map, source, transform, parameters);

      const auto& brushes = result.brushes.at(&defaultParent);
      REQUIRE(brushes.size() == 1);
      CHECK(brushes[0]->logicalBounds() == vm::bbox3d{{0, -16, -16}, {1, 16, 16}});
      REQUIRE(result.issues.size() == 1);
      CHECK(result.issues[0].sourceFaceIndex == 0);
      CHECK(result.issues[0].iteration == 0);
      CHECK(result.issues[0].segment == 1);
      CHECK(!result.issues[0].message.empty());
    }

    SECTION("returns nothing if every segment is degenerate")
    {
      // an identity transform collapses every station onto the source face
      parameters.segments = 2;

      const auto result = generateSweepBrushes(map, source, transform, parameters);
      CHECK(result.brushes.empty());
      CHECK(result.issues.size() == 2);
    }

    SECTION("continuous UVs globally unfold the boundary")
    {
      const auto attributes = SweepFaceAttributes{
        "continuous",
        mdl::UvAttributes{
          .offset = vm::vec2f{8.0f, 12.0f},
          .scale = vm::vec2f{2.0f, 4.0f},
          .rotation = 30.0f},
        mdl::SurfaceAttributes{},
        mdl::UvCoordSystemSnapshot{vm::vec3d{0.25, 0.0, 0.5}, vm::vec3d{-0.5, 0.0, 0.75}},
        vm::plane3d{16.0, vm::vec3d{0, -1, 0}}};
      source.faces[0].sideAttributes =
        std::vector<std::optional<SweepFaceAttributes>>(4u, attributes);
      transform.translation = vm::vec3d{64, 0, 0};
      parameters.segments = 2;
      parameters.uvMode = SweepUvMode::Continuous;

      const auto result = generateSweepBrushes(map, source, transform, parameters);
      CHECK(result.issues.empty());
      const auto& brushes = result.brushes.at(&defaultParent);
      REQUIRE(brushes.size() == 2u);

      const auto firstSideIndex = findFaceContaining(
        brushes[0]->brush(),
        {{0, -16, -16}, {0, -16, 16}, {32, -16, 16}, {32, -16, -16}});
      REQUIRE(firstSideIndex);
      const auto& firstSide = brushes[0]->brush().face(*firstSideIndex);
      checkAffineUvMapping(firstSide);
      CHECK(firstSide.uvAttributes().scale == attributes.uvAttributes.scale);
      CHECK(firstSide.uvAttributes().rotation == attributes.uvAttributes.rotation);
      CHECK(firstSide.uvCoords({0, -16, -16}) == vm::approx{vm::vec2f{4, 9}});
      CHECK(
        firstSide.uvCoords({0, -16, 16}) - firstSide.uvCoords({0, -16, -16})
        == vm::approx{vm::vec2f{8, 6}});

      const auto nextSegmentSideIndex = findFaceContaining(
        brushes[1]->brush(),
        {{32, -16, -16}, {32, -16, 16}, {64, -16, 16}, {64, -16, -16}});
      REQUIRE(nextSegmentSideIndex);
      const auto& nextSegmentSide = brushes[1]->brush().face(*nextSegmentSideIndex);
      checkAffineUvMapping(nextSegmentSide);
      CHECK(sameUvCoordinates(
        nextSegmentSide.uvCoords({32, -16, -16}), firstSide.uvCoords({32, -16, -16})));
      CHECK(sameUvCoordinates(
        nextSegmentSide.uvCoords({32, -16, 16}), firstSide.uvCoords({32, -16, 16})));

      const auto adjacentSideIndex = findFaceContaining(
        brushes[0]->brush(), {{0, -16, 16}, {0, 16, 16}, {32, 16, 16}, {32, -16, 16}});
      REQUIRE(adjacentSideIndex);
      const auto& adjacentSide = brushes[0]->brush().face(*adjacentSideIndex);
      checkAffineUvMapping(adjacentSide);
      CHECK(sameUvCoordinates(
        adjacentSide.uvCoords({0, -16, 16}), firstSide.uvCoords({0, -16, 16})));
      CHECK(sameUvCoordinates(
        adjacentSide.uvCoords({32, -16, 16}), firstSide.uvCoords({32, -16, 16})));
    }

    SECTION("continuous UVs preserve each source face at the entrance")
    {
      const auto makeAttributes = [](
                                    const vm::vec2f offset,
                                    const vm::vec2f scale,
                                    const float rotation,
                                    const vm::vec3d uAxis,
                                    const vm::vec3d vAxis,
                                    const vm::plane3d boundary) {
        return SweepFaceAttributes{
          "continuous",
          mdl::UvAttributes{.offset = offset, .scale = scale, .rotation = rotation},
          mdl::SurfaceAttributes{},
          mdl::UvCoordSystemSnapshot{uAxis, vAxis},
          boundary};
      };
      const auto sideAttributes = std::array{
        makeAttributes({11, 13}, {2, 4}, 10, {1, 0, 0}, {0, 0, 1}, {16, {0, -1, 0}}),
        makeAttributes({21, 23}, {3, 5}, 20, {1, 0, 0}, {0, 1, 0}, {16, {0, 0, 1}}),
        makeAttributes({31, 33}, {-2, 6}, 30, {1, 0, 0}, {0, 0, -1}, {16, {0, 1, 0}}),
        makeAttributes({41, 43}, {4, -5}, 40, {1, 0, 0}, {0, -1, 0}, {16, {0, 0, -1}})};
      source.faces[0].sideAttributes = {
        sideAttributes[0], sideAttributes[1], sideAttributes[2], sideAttributes[3]};
      transform.translation = vm::vec3d{64, 0, 0};
      parameters.segments = 2;
      parameters.uvMode = SweepUvMode::Continuous;

      const auto result = generateSweepBrushes(map, source, transform, parameters);
      CHECK(result.issues.empty());
      const auto& brushes = result.brushes.at(&defaultParent);
      REQUIRE(brushes.size() == 2u);

      const auto& vertices = source.faces[0].polygon.vertices();
      for (size_t edgeIndex = 0; edgeIndex < vertices.size(); ++edgeIndex)
      {
        const auto next = (edgeIndex + 1u) % vertices.size();
        const auto end0 = vertices[edgeIndex] + vm::vec3d{32, 0, 0};
        const auto end1 = vertices[next] + vm::vec3d{32, 0, 0};
        const auto faceIndex = findFaceContaining(
          brushes[0]->brush(), {vertices[edgeIndex], vertices[next], end1, end0});
        REQUIRE(faceIndex);
        const auto& face = brushes[0]->brush().face(*faceIndex);
        const auto& attributes = sideAttributes[edgeIndex];
        const auto expectedUv = [&](const vm::vec3d& point) {
          return vm::vec2f{
            float(
              vm::dot(point, attributes.uvCoordSystemSnapshot->uAxis)
                / double(attributes.uvAttributes.scale.x())
              + double(attributes.uvAttributes.offset.x())),
            float(
              vm::dot(point, attributes.uvCoordSystemSnapshot->vAxis)
                / double(attributes.uvAttributes.scale.y())
              + double(attributes.uvAttributes.offset.y()))};
        };

        CHECK(sameUvCoordinates(
          face.uvCoords(vertices[edgeIndex]), expectedUv(vertices[edgeIndex])));
        CHECK(
          sameUvCoordinates(face.uvCoords(vertices[next]), expectedUv(vertices[next])));
        CHECK(face.uvAttributes().scale == attributes.uvAttributes.scale);
        CHECK(face.uvAttributes().rotation == attributes.uvAttributes.rotation);
      }
    }

    SECTION("continuous UVs unwrap equivalent source texture phases")
    {
      auto materials = std::vector<gl::Material>{};
      materials.emplace_back(
        "continuous", gl::createTextureResource(gl::Texture{64, 32}));
      auto materialCollections = std::vector<gl::MaterialCollection>{};
      materialCollections.emplace_back(std::move(materials));
      map.materialManager().setMaterialCollections(std::move(materialCollections));

      const auto baseAttributes = SweepFaceAttributes{
        "continuous",
        mdl::UvAttributes{.offset = vm::vec2f{8, 12}},
        mdl::SurfaceAttributes{},
        mdl::UvCoordSystemSnapshot{vm::vec3d{0, 1, 0}, vm::vec3d{0, 0, 1}},
        vm::plane3d{0.0, vm::vec3d{1, 0, 0}}};
      auto sideAttributes =
        std::array{baseAttributes, baseAttributes, baseAttributes, baseAttributes};
      sideAttributes[1].uvAttributes.offset =
        sideAttributes[1].uvAttributes.offset + vm::vec2f{64, -32};
      sideAttributes[2].uvAttributes.offset =
        sideAttributes[2].uvAttributes.offset + vm::vec2f{-128, 64};
      sideAttributes[3].uvAttributes.offset =
        sideAttributes[3].uvAttributes.offset + vm::vec2f{192, 96};
      source.faces[0].sideAttributes = {
        sideAttributes[0], sideAttributes[1], sideAttributes[2], sideAttributes[3]};
      transform.translation = vm::vec3d{64, 64, 0};
      transform.rotation = vm::quatd{vm::vec3d{0, 0, 1}, vm::Cd::half_pi()};
      parameters.segments = 8;
      parameters.pathMode = SweepPathMode::Arc;
      parameters.uvMode = SweepUvMode::Continuous;

      const auto result = generateSweepBrushes(map, source, transform, parameters);
      CHECK(result.issues.empty());
      const auto& brushes = result.brushes.at(&defaultParent);
      REQUIRE(brushes.size() == parameters.segments);

      const auto rotation = transform.effectiveRotation();
      const auto& vertices = source.faces[0].polygon.vertices();
      for (size_t segmentIndex = 0; segmentIndex < parameters.segments; ++segmentIndex)
      {
        const auto startTransform = stationTransform(
          source,
          transform,
          parameters,
          double(segmentIndex) / double(parameters.segments),
          rotation);
        const auto endTransform = stationTransform(
          source,
          transform,
          parameters,
          double(segmentIndex + 1u) / double(parameters.segments),
          rotation);
        const auto& brush = brushes[segmentIndex]->brush();
        for (size_t edgeIndex = 0; edgeIndex < vertices.size(); ++edgeIndex)
        {
          const auto next = (edgeIndex + 1u) % vertices.size();
          const auto sharedVertex = endTransform * vertices[next];
          const auto currentFaceIndex = findFaceContaining(
            brush,
            {startTransform * vertices[edgeIndex],
             startTransform * vertices[next],
             endTransform * vertices[next],
             endTransform * vertices[edgeIndex]});
          const auto adjacentFaceIndex = findFaceContaining(
            brush,
            {startTransform * vertices[next],
             startTransform * vertices[(next + 1u) % vertices.size()],
             endTransform * vertices[(next + 1u) % vertices.size()],
             endTransform * vertices[next]});
          REQUIRE(currentFaceIndex);
          REQUIRE(adjacentFaceIndex);
          CHECK(sameUvCoordinates(
            brush.face(*currentFaceIndex).uvCoords(sharedVertex),
            brush.face(*adjacentFaceIndex).uvCoords(sharedVertex)));
        }
      }
    }

    SECTION("continuous UVs remain connected through an arc")
    {
      const auto attributes = SweepFaceAttributes{
        "continuous",
        mdl::UvAttributes{},
        mdl::SurfaceAttributes{},
        std::nullopt,
        vm::plane3d{0.0, vm::vec3d{1, 0, 0}}};
      source.faces[0].sideAttributes =
        std::vector<std::optional<SweepFaceAttributes>>(4u, attributes);
      transform.translation = vm::vec3d{64, 64, 0};
      transform.rotation = vm::quatd{vm::vec3d{0, 0, 1}, vm::Cd::half_pi()};
      parameters.segments = 8;
      parameters.pathMode = SweepPathMode::Arc;
      parameters.uvMode = SweepUvMode::Continuous;

      const auto result = generateSweepBrushes(map, source, transform, parameters);
      CHECK(result.issues.empty());
      const auto& brushes = result.brushes.at(&defaultParent);
      REQUIRE(brushes.size() == 8u);

      const auto rotation = transform.effectiveRotation();
      const auto transform0 =
        stationTransform(source, transform, parameters, 0.0, rotation);
      const auto transform1 =
        stationTransform(source, transform, parameters, 0.125, rotation);
      const auto transform2 =
        stationTransform(source, transform, parameters, 0.25, rotation);
      const auto& vertices = source.faces[0].polygon.vertices();
      const auto p00 = transform0 * vertices[0];
      const auto p01 = transform0 * vertices[1];
      const auto p10 = transform1 * vertices[0];
      const auto p11 = transform1 * vertices[1];
      const auto p12 = transform1 * vertices[2];
      const auto p20 = transform2 * vertices[0];
      const auto p21 = transform2 * vertices[1];

      const auto firstIndex =
        findFaceContaining(brushes[0]->brush(), {p00, p01, p11, p10});
      const auto nextIndex =
        findFaceContaining(brushes[1]->brush(), {p10, p11, p21, p20});
      const auto adjacentIndex = findFaceContaining(
        brushes[0]->brush(), {p01, transform0 * vertices[2], p12, p11});
      REQUIRE(firstIndex);
      REQUIRE(nextIndex);
      REQUIRE(adjacentIndex);
      const auto& first = brushes[0]->brush().face(*firstIndex);
      const auto& next = brushes[1]->brush().face(*nextIndex);
      const auto& adjacent = brushes[0]->brush().face(*adjacentIndex);
      checkAffineUvMapping(first);
      checkAffineUvMapping(next);
      checkAffineUvMapping(adjacent);
      CHECK(first.uvCoords(p00) == vm::approx{vm::vec2f{0, 0}});
      CHECK(first.uvCoords(p01) == vm::approx{vm::vec2f{0, 32}});
      CHECK(sameUvCoordinates(next.uvCoords(p10), first.uvCoords(p10)));
      CHECK(sameUvCoordinates(next.uvCoords(p11), first.uvCoords(p11)));
      CHECK(sameUvCoordinates(adjacent.uvCoords(p01), first.uvCoords(p01)));
      CHECK(sameUvCoordinates(adjacent.uvCoords(p11), first.uvCoords(p11)));

      const auto transform7 =
        stationTransform(source, transform, parameters, 0.875, rotation);
      const auto transform8 =
        stationTransform(source, transform, parameters, 1.0, rotation);
      const auto p70 = transform7 * vertices[0];
      const auto p71 = transform7 * vertices[1];
      const auto p80 = transform8 * vertices[0];
      const auto p81 = transform8 * vertices[1];
      const auto lastIndex =
        findFaceContaining(brushes[7]->brush(), {p70, p71, p81, p80});
      REQUIRE(lastIndex);
      const auto& last = brushes[7]->brush().face(*lastIndex);
      checkAffineUvMapping(last);

      const auto transform6 =
        stationTransform(source, transform, parameters, 0.75, rotation);
      const auto p60 = transform6 * vertices[0];
      const auto p61 = transform6 * vertices[1];
      const auto penultimateIndex =
        findFaceContaining(brushes[6]->brush(), {p60, p61, p71, p70});
      REQUIRE(penultimateIndex);
      const auto& penultimate = brushes[6]->brush().face(*penultimateIndex);
      CHECK(sameUvCoordinates(last.uvCoords(p70), penultimate.uvCoords(p70)));
      CHECK(sameUvCoordinates(last.uvCoords(p71), penultimate.uvCoords(p71)));

      auto pathUvLengths = std::vector<double>{};
      for (size_t segmentIndex = 0; segmentIndex < parameters.segments; ++segmentIndex)
      {
        const auto startTransform = stationTransform(
          source,
          transform,
          parameters,
          double(segmentIndex) / double(parameters.segments),
          rotation);
        const auto endTransform = stationTransform(
          source,
          transform,
          parameters,
          double(segmentIndex + 1u) / double(parameters.segments),
          rotation);
        const auto start0 = startTransform * vertices[0];
        const auto start1 = startTransform * vertices[1];
        const auto end0 = endTransform * vertices[0];
        const auto end1 = endTransform * vertices[1];
        const auto faceIndex = findFaceContaining(
          brushes[segmentIndex]->brush(), {start0, start1, end1, end0});
        REQUIRE(faceIndex);
        const auto& face = brushes[segmentIndex]->brush().face(*faceIndex);
        pathUvLengths.push_back(vm::length(face.uvCoords(end0) - face.uvCoords(start0)));
      }
      CHECK(std::ranges::max(pathUvLengths) / std::ranges::min(pathUvLengths) < 1.25);

      parameters.alignment = SweepAlignment::Integer;
      const auto snappedResult = generateSweepBrushes(map, source, transform, parameters);
      CHECK(snappedResult.issues.empty());
      REQUIRE(snappedResult.brushes.contains(&defaultParent));
      CHECK(snappedResult.brushes.at(&defaultParent).size() == parameters.segments);
    }

    SECTION("continuous UVs fit twisted sides to their actual brush faces")
    {
      const auto attributes = SweepFaceAttributes{
        "continuous",
        mdl::UvAttributes{
          .offset = vm::vec2f{13, -9}, .scale = vm::vec2f{-2, 4}, .rotation = 37},
        mdl::SurfaceAttributes{},
        mdl::UvCoordSystemSnapshot{vm::vec3d{0, 1, 0}, vm::vec3d{0, 0, 1}},
        vm::plane3d{0.0, vm::vec3d{1, 0, 0}}};
      source.faces[0].sideAttributes =
        std::vector<std::optional<SweepFaceAttributes>>(4u, attributes);
      transform.translation = vm::vec3d{64, 64, 24};
      transform.rotation = vm::quatd{vm::normalize(vm::vec3d{0.4, 0.2, 1.0}), 1.2};
      parameters.segments = 6;
      parameters.pathMode = SweepPathMode::Arc;
      parameters.uvMode = SweepUvMode::Continuous;

      const auto result = generateSweepBrushes(map, source, transform, parameters);
      CHECK(result.issues.empty());
      const auto& brushes = result.brushes.at(&defaultParent);
      REQUIRE(brushes.size() == parameters.segments);

      const auto rotation = transform.effectiveRotation();
      const auto& vertices = source.faces[0].polygon.vertices();
      auto previousFaces = std::vector<const mdl::BrushFace*>{};
      for (size_t segmentIndex = 0; segmentIndex < parameters.segments; ++segmentIndex)
      {
        const auto startTransform = stationTransform(
          source,
          transform,
          parameters,
          double(segmentIndex) / double(parameters.segments),
          rotation);
        const auto endTransform = stationTransform(
          source,
          transform,
          parameters,
          double(segmentIndex + 1u) / double(parameters.segments),
          rotation);
        const auto start0 = startTransform * vertices[0];
        const auto start1 = startTransform * vertices[1];
        const auto end0 = endTransform * vertices[0];
        const auto end1 = endTransform * vertices[1];

        auto segmentFaces = std::vector<const mdl::BrushFace*>{};
        for (const auto& face : brushes[segmentIndex]->brush().faces())
        {
          const auto faceVertices = face.vertexPositions();
          const auto belongsToTwistedSide =
            std::ranges::all_of(
              faceVertices,
              [&](const auto& point) {
                return vm::is_equal(point, start0, 0.001)
                       || vm::is_equal(point, start1, 0.001)
                       || vm::is_equal(point, end0, 0.001)
                       || vm::is_equal(point, end1, 0.001);
              })
            && faceVertices.size() >= 3u;
          if (belongsToTwistedSide)
          {
            checkUsableUvMapping(face);
            CHECK(face.uvAttributes().scale == attributes.uvAttributes.scale);
            CHECK(face.uvAttributes().rotation == attributes.uvAttributes.rotation);
            segmentFaces.push_back(&face);
          }
        }
        REQUIRE(!segmentFaces.empty());

        if (!previousFaces.empty())
        {
          for (const auto point : {start0, start1})
          {
            const auto* previous = findFaceContainingPoint(previousFaces, point);
            const auto* current = findFaceContainingPoint(segmentFaces, point);
            REQUIRE(previous != nullptr);
            REQUIRE(current != nullptr);
            CHECK(sameUvCoordinates(previous->uvCoords(point), current->uvCoords(point)));
          }
        }
        previousFaces = std::move(segmentFaces);
      }
    }

    SECTION("continuous UVs limit distortion on the Lws_newtool arch")
    {
      const auto attributes = SweepFaceAttributes{
        "continuous",
        mdl::UvAttributes{},
        mdl::SurfaceAttributes{},
        std::nullopt,
        vm::plane3d{704.0, vm::vec3d{1, 0, 0}}};
      const auto point = [](const double y, const double z) {
        return vm::vec3d{704, y, z};
      };
      const auto polygon = [&](const std::initializer_list<vm::vec3d> vertices) {
        return SweepFace{
          vm::polygon3d{vertices},
          &defaultParent,
          std::nullopt,
          std::vector<std::optional<SweepFaceAttributes>>(4u, attributes)};
      };
      source.faces = {
        polygon({point(-240, 216), point(-200, 296), point(-188, 284), point(-224, 212)}),
        polygon({point(-200, 296), point(-120, 336), point(-116, 320), point(-188, 284)}),
        polygon({point(-240, 96), point(-240, 216), point(-224, 212), point(-224, 96)}),
        polygon({point(-120, 336), point(-40, 336), point(-44, 320), point(-116, 320)}),
        polygon({point(-40, 336), point(40, 296), point(28, 284), point(-44, 320)}),
        polygon({point(40, 296), point(80, 216), point(64, 212), point(28, 284)}),
        polygon({point(80, 216), point(80, 96), point(64, 96), point(64, 212)}),
      };
      source.center = vm::vec3d{704, -80, 216};
      source.normal = vm::vec3d{1, 0, 0};
      transform.translation = vm::vec3d{403, -506, 0};
      transform.rotation = vm::quatd{vm::vec3d{0, 0, -1}, vm::Cd::pi()};
      parameters.segments = 6;
      parameters.pathMode = SweepPathMode::Arc;
      parameters.alignment = SweepAlignment::Integer;
      parameters.uvMode = SweepUvMode::Continuous;

      const auto result = generateSweepBrushes(map, source, transform, parameters);
      CHECK(result.issues.empty());
      const auto& brushes = result.brushes.at(&defaultParent);
      REQUIRE(brushes.size() == source.faces.size() * parameters.segments);

      auto retainedAnExactStation = false;
      for (size_t sourceFaceIndex = 0; sourceFaceIndex < source.faces.size();
           ++sourceFaceIndex)
      {
        const auto vertexCount = source.faces[sourceFaceIndex].polygon.vertexCount();
        for (size_t segmentIndex = 1; segmentIndex < parameters.segments; ++segmentIndex)
        {
          const auto& previous =
            brushes[sourceFaceIndex * parameters.segments + segmentIndex - 1u]->brush();
          const auto& current =
            brushes[sourceFaceIndex * parameters.segments + segmentIndex]->brush();
          auto sharedPoints = std::vector<vm::vec3d>{};
          for (const auto& vertexPosition : previous.vertexPositions())
          {
            if (current.hasVertex(vertexPosition, 0.001))
            {
              sharedPoints.push_back(vertexPosition);
            }
          }

          REQUIRE(sharedPoints.size() == vertexCount);
          const auto sharedPlane =
            vm::from_points(std::begin(sharedPoints), std::end(sharedPoints));
          REQUIRE(sharedPlane);
          CHECK(std::ranges::all_of(sharedPoints, [&](const auto& point) {
            return std::abs(sharedPlane->point_distance(point)) <= 0.001;
          }));
          retainedAnExactStation =
            retainedAnExactStation
            || std::ranges::any_of(
              sharedPoints, [](const auto& point) { return point != vm::round(point); });
        }
      }
      CHECK(retainedAnExactStation);

      using Edge = std::pair<vm::vec3d, vm::vec3d>;
      auto boundaryEdges = std::vector<Edge>{};
      for (const auto& face : source.faces)
      {
        const auto& vertices = face.polygon.vertices();
        for (size_t i = 0; i < vertices.size(); ++i)
        {
          const auto edge = Edge{vertices[i], vertices[(i + 1u) % vertices.size()]};
          const auto
            shared =
              std::ranges::count_if(
                source.faces, [&](const auto& candidate) {
                  const auto& candidateVertices = candidate.polygon.vertices();
                  return std::ranges::any_of(
                    std::views::iota(size_t{0}, candidateVertices.size()),
                    [&](const auto j) {
                      const auto& a = candidateVertices[j];
                      const auto& b =
                        candidateVertices[(j + 1u) % candidateVertices.size()];
                      return (vm::is_equal(a, edge.first, 0.001) && vm::is_equal(b, edge.second, 0.001))
                     || (vm::is_equal(a, edge.second, 0.001) && vm::is_equal(b, edge.first, 0.001));
                    });
                });
          if (shared == 1)
          {
            boundaryEdges.push_back(edge);
          }
        }
      }
      REQUIRE(boundaryEdges.size() == 16u);

      const auto rotation = transform.effectiveRotation();
      auto conditions = std::vector<double>{};
      for (size_t sourceFaceIndex = 0; sourceFaceIndex < source.faces.size();
           ++sourceFaceIndex)
      {
        const auto& vertices = source.faces[sourceFaceIndex].polygon.vertices();
        for (size_t edgeIndex = 0; edgeIndex < vertices.size(); ++edgeIndex)
        {
          const auto edge =
            Edge{vertices[edgeIndex], vertices[(edgeIndex + 1u) % vertices.size()]};
          const auto exposed =
            std::ranges::any_of(boundaryEdges, [&](const auto& candidate) {
              return (vm::is_equal(candidate.first, edge.first, 0.001)
                    && vm::is_equal(candidate.second, edge.second, 0.001))
                   || (vm::is_equal(candidate.first, edge.second, 0.001)
                       && vm::is_equal(candidate.second, edge.first, 0.001));
            });
          if (!exposed)
          {
            continue;
          }

          for (size_t segmentIndex = 0; segmentIndex < parameters.segments;
               ++segmentIndex)
          {
            const auto startTransform = stationTransform(
              source,
              transform,
              parameters,
              double(segmentIndex) / double(parameters.segments),
              rotation);
            const auto endTransform = stationTransform(
              source,
              transform,
              parameters,
              double(segmentIndex + 1u) / double(parameters.segments),
              rotation);
            const auto& brush =
              brushes[sourceFaceIndex * parameters.segments + segmentIndex]->brush();
            const auto actualPoint = [&](const vm::vec3d& expected) {
              return brush.findClosestVertexPosition(expected);
            };
            const auto quad = std::array{
              actualPoint(segmentIndex == 0u ? edge.first : startTransform * edge.first),
              actualPoint(
                segmentIndex == 0u ? edge.second : startTransform * edge.second),
              actualPoint(endTransform * edge.second),
              actualPoint(endTransform * edge.first)};
            for (const auto& face : brush.faces())
            {
              const auto faceVertices = face.vertexPositions();
              if (
                faceVertices.size() >= 3u
                && std::ranges::all_of(faceVertices, [&](const auto& vertex) {
                     return std::ranges::any_of(quad, [&](const auto& point) {
                       return vm::is_equal(vertex, point, 0.001);
                     });
                   }))
              {
                conditions.push_back(uvCondition(face));
              }
            }
          }
        }
      }

      REQUIRE(conditions.size() >= 96u);
      std::ranges::sort(conditions);
      const auto p95 = conditions[size_t(0.95 * double(conditions.size() - 1u))];
      CHECK(p95 <= 5.0);
      CHECK(conditions.back() <= 30.0);
    }

    SECTION(
      "continuous UVs retain stored rotations independently of their affine UV axes")
    {
      const auto makeAttributes = [](
                                    const vm::vec3d& normal,
                                    const vm::vec3d& uAxis,
                                    const vm::vec3d& vAxis,
                                    const float rotation) {
        return SweepFaceAttributes{
          "continuous",
          mdl::UvAttributes{.scale = vm::vec2f{1, 1}, .rotation = rotation},
          mdl::SurfaceAttributes{},
          mdl::UvCoordSystemSnapshot{uAxis, vAxis},
          vm::plane3d{0.0, normal}};
      };
      const auto capAttributes =
        makeAttributes(vm::vec3d{1, 0, 0}, vm::vec3d{0, 1, 0}, vm::vec3d{0, 0, -1}, 270);
      const auto sideAttributes = std::array{
        makeAttributes(
          vm::normalize(vm::vec3d{0, -1, 0}),
          vm::vec3d{1, 0, 0},
          vm::vec3d{0, 0, -1},
          90),
        makeAttributes(
          vm::normalize(vm::vec3d{0, -0.242535625, -0.9701425}),
          vm::vec3d{-1, 0, 0},
          vm::vec3d{0, -0.9701425, 0.242535625},
          90),
        makeAttributes(
          vm::normalize(vm::vec3d{0, 1, 0}),
          vm::vec3d{-1, 0, 0},
          vm::vec3d{0, 0, -1},
          90),
        makeAttributes(
          vm::normalize(vm::vec3d{0, 0.242535625, 0.9701425}),
          vm::vec3d{1, 0, 0},
          vm::vec3d{0, 0.9701425, -0.242535625},
          90)};
      source.faces = {SweepFace{
        vm::polygon3d{
          {-336, 48, -272}, {-336, 48, -152}, {-336, 64, -156}, {-336, 64, -272}},
        &defaultParent,
        capAttributes,
        {sideAttributes[0], sideAttributes[1], sideAttributes[2], sideAttributes[3]}}};
      source.center = vm::vec3d{-336, 208, -152};
      source.normal = vm::vec3d{1, 0, 0};
      transform.translation = vm::vec3d{512, -512, 0};
      transform.rotation = vm::quatd{vm::vec3d{0, 0, -1}, vm::Cd::half_pi()};
      parameters.segments = 4;
      parameters.pathMode = SweepPathMode::Arc;
      parameters.alignment = SweepAlignment::Integer;
      parameters.uvMode = SweepUvMode::Continuous;

      const auto result = generateSweepBrushes(map, source, transform, parameters);
      CHECK(result.issues.empty());
      const auto& brushes = result.brushes.at(&defaultParent);
      REQUIRE(brushes.size() == parameters.segments);

      for (const auto& brush : brushes)
      {
        for (const auto& face : brush->brush().faces())
        {
          const auto rotation = face.uvAttributes().rotation;
          CHECK(
            (std::abs(rotation - 90.0f) <= 0.001f
             || std::abs(rotation - 270.0f) <= 0.001f));
        }
      }
    }

    SECTION("continuous UVs solve large twisted strips without a dense matrix")
    {
      const auto attributes = SweepFaceAttributes{
        "continuous",
        mdl::UvAttributes{},
        mdl::SurfaceAttributes{},
        std::nullopt,
        vm::plane3d{0.0, vm::vec3d{1, 0, 0}}};
      source.faces[0].sideAttributes =
        std::vector<std::optional<SweepFaceAttributes>>(4u, attributes);
      transform.translation = vm::vec3d{320, 192, 96};
      transform.rotation = vm::quatd{vm::normalize(vm::vec3d{0.35, 0.2, 1.0}), 2.2};
      parameters.segments = 80;
      parameters.pathMode = SweepPathMode::Arc;
      parameters.uvMode = SweepUvMode::Continuous;

      const auto result = generateSweepBrushes(map, source, transform, parameters);
      CHECK(result.issues.empty());
      REQUIRE(result.brushes.contains(&defaultParent));
      CHECK(result.brushes.at(&defaultParent).size() == parameters.segments);
    }

    SECTION("continuous UVs join the outer boundary of multiple source faces")
    {
      const auto attributes = SweepFaceAttributes{
        "continuous",
        mdl::UvAttributes{},
        mdl::SurfaceAttributes{},
        std::nullopt,
        vm::plane3d{0.0, vm::vec3d{1, 0, 0}}};
      const auto left =
        vm::polygon3d{{0, -16, -16}, {0, -16, 16}, {0, 0, 16}, {0, 0, -16}};
      const auto right =
        vm::polygon3d{{0, 0, -16}, {0, 0, 16}, {0, 16, 16}, {0, 16, -16}};
      source.faces = {
        SweepFace{
          left,
          &defaultParent,
          std::nullopt,
          std::vector<std::optional<SweepFaceAttributes>>(4u, attributes)},
        SweepFace{
          right,
          &defaultParent,
          std::nullopt,
          std::vector<std::optional<SweepFaceAttributes>>(4u, attributes)}};
      transform.translation = vm::vec3d{32, 0, 0};
      parameters.segments = 1;
      parameters.pathMode = SweepPathMode::Straight;
      parameters.uvMode = SweepUvMode::Continuous;

      const auto result = generateSweepBrushes(map, source, transform, parameters);
      CHECK(result.issues.empty());
      const auto& brushes = result.brushes.at(&defaultParent);
      REQUIRE(brushes.size() == 2u);

      const auto leftTopIndex = findFaceContaining(
        brushes[0]->brush(), {{0, -16, 16}, {0, 0, 16}, {32, 0, 16}, {32, -16, 16}});
      const auto rightTopIndex = findFaceContaining(
        brushes[1]->brush(), {{0, 0, 16}, {0, 16, 16}, {32, 16, 16}, {32, 0, 16}});
      REQUIRE(leftTopIndex);
      REQUIRE(rightTopIndex);
      const auto& leftTop = brushes[0]->brush().face(*leftTopIndex);
      const auto& rightTop = brushes[1]->brush().face(*rightTopIndex);
      CHECK(
        sameUvCoordinates(rightTop.uvCoords({0, 0, 16}), leftTop.uvCoords({0, 0, 16})));
      CHECK(
        sameUvCoordinates(rightTop.uvCoords({32, 0, 16}), leftTop.uvCoords({32, 0, 16})));

      const auto internalLeftIndex = findFaceContaining(
        brushes[0]->brush(), {{0, 0, -16}, {0, 0, 16}, {32, 0, 16}, {32, 0, -16}});
      const auto internalRightIndex = findFaceContaining(
        brushes[1]->brush(), {{0, 0, -16}, {0, 0, 16}, {32, 0, 16}, {32, 0, -16}});
      REQUIRE(internalLeftIndex);
      REQUIRE(internalRightIndex);
      CHECK(
        brushes[0]->brush().face(*internalLeftIndex).uvAttributes()
        == attributes.uvAttributes);
      CHECK(
        brushes[1]->brush().face(*internalRightIndex).uvAttributes()
        == attributes.uvAttributes);
    }

    SECTION("continuous UVs keep source seams joined through a bend")
    {
      const auto makeAttributes =
        [](
          const std::string& materialName,
          const std::optional<mdl::UvCoordSystemSnapshot>& snapshot) {
          return SweepFaceAttributes{
            materialName,
            mdl::UvAttributes{},
            mdl::SurfaceAttributes{},
            snapshot,
            vm::plane3d{0.0, vm::vec3d{1, 0, 0}}};
        };
      const auto leftTop = makeAttributes(
        "continuous", mdl::UvCoordSystemSnapshot{vm::vec3d{1, 0, 0}, vm::vec3d{0, 1, 0}});
      const auto rightTop = makeAttributes(
        "continuous",
        mdl::UvCoordSystemSnapshot{vm::vec3d{-1, 0, 0}, vm::vec3d{0, 1, 0}});
      const auto other = [&](const std::string& name) {
        return makeAttributes(name, std::nullopt);
      };
      const auto left =
        vm::polygon3d{{0, -16, -16}, {0, -16, 16}, {0, 0, 16}, {0, 0, -16}};
      const auto right =
        vm::polygon3d{{0, 0, -16}, {0, 0, 16}, {0, 16, 16}, {0, 16, -16}};
      source.faces = {
        SweepFace{
          left,
          &defaultParent,
          std::nullopt,
          {other("left-side"), leftTop, other("internal-left"), other("left-bottom")}},
        SweepFace{
          right,
          &defaultParent,
          std::nullopt,
          {other("internal-right"),
           rightTop,
           other("right-side"),
           other("right-bottom")}}};
      transform.translation = vm::vec3d{32, 32, 0};
      transform.rotation = vm::quatd{vm::vec3d{0, 0, 1}, vm::Cd::half_pi()};
      parameters.segments = 4;
      parameters.pathMode = SweepPathMode::Arc;
      parameters.uvMode = SweepUvMode::Continuous;

      const auto result = generateSweepBrushes(map, source, transform, parameters);
      CHECK(result.issues.empty());
      const auto& brushes = result.brushes.at(&defaultParent);
      REQUIRE(brushes.size() == source.faces.size() * parameters.segments);

      const auto rotation = transform.effectiveRotation();
      for (size_t segmentIndex = 0; segmentIndex < parameters.segments; ++segmentIndex)
      {
        const auto startTransform = stationTransform(
          source,
          transform,
          parameters,
          double(segmentIndex) / double(parameters.segments),
          rotation);
        const auto endTransform = stationTransform(
          source,
          transform,
          parameters,
          double(segmentIndex + 1u) / double(parameters.segments),
          rotation);
        const auto startLeft = startTransform * vm::vec3d{0, -16, 16};
        const auto startJoint = startTransform * vm::vec3d{0, 0, 16};
        const auto startRight = startTransform * vm::vec3d{0, 16, 16};
        const auto endLeft = endTransform * vm::vec3d{0, -16, 16};
        const auto endJoint = endTransform * vm::vec3d{0, 0, 16};
        const auto endRight = endTransform * vm::vec3d{0, 16, 16};
        const auto& leftBrush = brushes[segmentIndex]->brush();
        const auto& rightBrush = brushes[parameters.segments + segmentIndex]->brush();
        const auto leftFaceIndex =
          findFaceContaining(leftBrush, {startLeft, startJoint, endJoint, endLeft});
        const auto rightFaceIndex =
          findFaceContaining(rightBrush, {startJoint, startRight, endRight, endJoint});
        REQUIRE(leftFaceIndex);
        REQUIRE(rightFaceIndex);
        const auto& leftFace = leftBrush.face(*leftFaceIndex);
        const auto& rightFace = rightBrush.face(*rightFaceIndex);
        CHECK(sameUvCoordinates(
          leftFace.uvCoords(startJoint), rightFace.uvCoords(startJoint)));
        CHECK(
          sameUvCoordinates(leftFace.uvCoords(endJoint), rightFace.uvCoords(endJoint)));
      }
    }

    SECTION("continuous UVs reject non Valve map formats")
    {
      auto standardFixture = MapDocumentFixture{};
      auto& standardDocument = standardFixture.create();
      auto& standardMap = standardDocument.map();
      auto standardSource = source;
      standardSource.faces[0].parent = &parentForNodes(standardMap);
      transform.translation = vm::vec3d{64, 0, 0};
      parameters.uvMode = SweepUvMode::Continuous;

      const auto result =
        generateSweepBrushes(standardMap, standardSource, transform, parameters);
      CHECK(result.brushes.empty());
      REQUIRE(result.issues.size() == 1u);
      CHECK(result.issues.front().message.find("Valve") != std::string::npos);
    }

    SECTION("continuous UVs reject boundary edges without source attributes")
    {
      transform.translation = vm::vec3d{64, 0, 0};
      parameters.uvMode = SweepUvMode::Continuous;

      const auto result = generateSweepBrushes(map, source, transform, parameters);
      CHECK(result.brushes.empty());
      REQUIRE(result.issues.size() == 1u);
      CHECK(result.issues.front().message.find("attributes") != std::string::npos);
    }
  }
}

} // namespace tb::ui
