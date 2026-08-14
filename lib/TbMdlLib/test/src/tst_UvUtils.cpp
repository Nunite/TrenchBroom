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

#include "mdl/CatchConfig.h"
#include "mdl/UvUtils.h"

#include "vm/approx.h"

#include <tuple>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::mdl
{

TEST_CASE("solveFaceUVProjection")
{
  SECTION("solves an affine projection from non-axis-aligned points")
  {
    const auto points =
      std::vector<vm::vec3d>{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}};
    const auto uvs = std::vector<vm::vec2f>{{5, -2}, {9, 0}, {12, 9}, {8, 7}};

    const auto projection = solveFaceUVProjection(points, uvs);
    REQUIRE(projection);
    CHECK(projection->uAxis == vm::approx(vm::vec3d{2, 1, 0}));
    CHECK(projection->vAxis == vm::approx(vm::vec3d{1, 3, 0}));
    CHECK(projection->offset == vm::approx(vm::vec2f{5, -2}));
  }

  SECTION("finds a valid basis when the first three points are collinear")
  {
    const auto points =
      std::vector<vm::vec3d>{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {0, 1, 0}};
    const auto uvs = std::vector<vm::vec2f>{{0, 0}, {1, 0}, {2, 0}, {0, 1}};

    CHECK(solveFaceUVProjection(points, uvs).has_value());
  }

  SECTION("rejects non-affine and degenerate input")
  {
    CHECK_FALSE(
      solveFaceUVProjection(
        {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}, {{0, 0}, {1, 0}, {2, 1}, {0, 1}})
        .has_value());
    CHECK_FALSE(
      solveFaceUVProjection({{0, 0, 0}, {1, 0, 0}, {2, 0, 0}}, {{0, 0}, {1, 0}, {2, 0}})
        .has_value());
    CHECK_FALSE(solveFaceUVProjection({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {{0, 0}, {1, 0}})
                  .has_value());
  }
}

TEST_CASE("computeCameraAxesForFaceNormal")
{
  CHECK(
    computeCameraAxesForFaceNormal(vm::vec3d{0, 0, 1})
    == std::tuple{vm::vec3d{0, 1, 0}, vm::vec3d{1, 0, 0}});

  CHECK(
    computeCameraAxesForFaceNormal(vm::vec3d{0, 0, -1})
    == std::tuple{vm::vec3d{0, -1, 0}, vm::vec3d{1, 0, 0}});

  CHECK(
    computeCameraAxesForFaceNormal(vm::vec3d{1, 0, 0})
    == std::tuple{vm::vec3d{0, 0, 1}, vm::vec3d{0, 1, 0}});

  SECTION("returns normalized up axis orthogonal to normal")
  {
    const auto normal = vm::normalize(vm::vec3d{1, 2, 3});
    const auto [upAxis, rightAxis] = computeCameraAxesForFaceNormal(normal);

    CHECK(vm::length(upAxis) == vm::approx{1.0});
    CHECK(vm::length(rightAxis) == vm::approx{1.0});
    CHECK(vm::dot(normal, upAxis) == vm::approx{0.0});
    CHECK(vm::dot(normal, rightAxis) == vm::approx{0.0});
    CHECK(vm::dot(upAxis, rightAxis) == vm::approx{0.0});
    CHECK(vm::cross(rightAxis, upAxis) == vm::approx(normal));
  }
}

TEST_CASE("measureUvSkew")
{
  SECTION("returns zero for orthogonal axes regardless of their rotation")
  {
    CHECK(
      measureUvSkew(vm::vec3d{1, 1, 0}, vm::vec3d{-1, 1, 0}, vm::vec3d{0, 0, 1})
      == vm::approx{0.0f});
  }

  SECTION("measures affine skew")
  {
    const auto skew = measureUvSkew(
      vm::vec3d{0, -0.8439833689478853, 0},
      vm::vec3d{0, -0.4343567650594142, 0.9999997369174299},
      vm::vec3d{-1, 0, 0});
    REQUIRE(skew);
    CHECK(vm::is_equal(*skew, 23.47f, 0.01f));
  }

  SECTION("ignores components along the face normal")
  {
    CHECK(
      measureUvSkew(vm::vec3d{4, 1, 0}, vm::vec3d{-7, 0, 1}, vm::vec3d{1, 0, 0})
      == vm::approx{0.0f});
  }

  SECTION("rejects degenerate projected axes")
  {
    CHECK(
      measureUvSkew(vm::vec3d{1, 0, 0}, vm::vec3d{0, 1, 0}, vm::vec3d{1, 0, 0})
      == std::nullopt);
  }
}

} // namespace tb::mdl
