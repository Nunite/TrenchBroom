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

#include "mdl/UvUtils.h"

#include "mdl/UvAttributes.h"

#include "kd/contracts.h"
#include "kd/result.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace tb::mdl
{
namespace
{

std::optional<vm::vec3d> solveUvAxis(
  const std::array<vm::vec3d, 3>& points, const std::array<float, 3>& coordinates)
{
  const auto edge1 = points[1] - points[0];
  const auto edge2 = points[2] - points[0];
  const auto a = vm::dot(edge1, edge1);
  const auto b = vm::dot(edge1, edge2);
  const auto c = vm::dot(edge2, edge2);
  const auto determinant = a * c - b * b;
  if (vm::is_zero(determinant, vm::Cd::almost_zero()))
  {
    return std::nullopt;
  }

  const auto d1 = double(coordinates[1] - coordinates[0]);
  const auto d2 = double(coordinates[2] - coordinates[0]);
  const auto s = (d1 * c - d2 * b) / determinant;
  const auto t = (d2 * a - d1 * b) / determinant;
  return s * edge1 + t * edge2;
}

} // namespace

std::optional<FaceUVProjection> solveFaceUVProjection(
  const std::vector<vm::vec3d>& points, const std::vector<vm::vec2f>& uvs)
{
  if (points.size() != uvs.size() || points.size() < 3u)
  {
    return std::nullopt;
  }

  auto basis = std::optional<std::array<size_t, 3>>{};
  for (size_t i = 0u; i + 2u < points.size() && !basis; ++i)
  {
    for (size_t j = i + 1u; j + 1u < points.size() && !basis; ++j)
    {
      for (size_t k = j + 1u; k < points.size(); ++k)
      {
        if (!vm::is_zero(
              vm::squared_length(vm::cross(points[j] - points[i], points[k] - points[i])),
              vm::Cd::almost_zero()))
        {
          basis = std::array{i, j, k};
          break;
        }
      }
    }
  }
  if (!basis)
  {
    return std::nullopt;
  }

  const auto basisPoints =
    std::array{points[(*basis)[0]], points[(*basis)[1]], points[(*basis)[2]]};
  const auto basisUvs = std::array{uvs[(*basis)[0]], uvs[(*basis)[1]], uvs[(*basis)[2]]};
  const auto uAxis =
    solveUvAxis(basisPoints, {basisUvs[0].x(), basisUvs[1].x(), basisUvs[2].x()});
  const auto vAxis =
    solveUvAxis(basisPoints, {basisUvs[0].y(), basisUvs[1].y(), basisUvs[2].y()});
  if (!uAxis || !vAxis)
  {
    return std::nullopt;
  }

  const auto offset = vm::vec2f{
    uvs[0].x() - float(vm::dot(points[0], *uAxis)),
    uvs[0].y() - float(vm::dot(points[0], *vAxis))};
  for (size_t i = 0u; i < points.size(); ++i)
  {
    const auto projected = vm::vec2f{
      float(vm::dot(points[i], *uAxis)) + offset.x(),
      float(vm::dot(points[i], *vAxis)) + offset.y()};
    if (!vm::is_equal(projected, uvs[i], 0.002f))
    {
      return std::nullopt;
    }
  }

  return FaceUVProjection{*uAxis, *vAxis, offset};
}

std::tuple<vm::vec3d, vm::vec3d> computeCameraAxesForFaceNormal(const vm::vec3d& normal)
{
  const auto right = vm::abs(vm::dot(vm::vec3d{0, 0, 1}, normal)) < double(1)
                       ? vm::normalize(vm::cross(vm::vec3d{0, 0, 1}, normal))
                       : vm::vec3d{1, 0, 0};
  return {vm::normalize(vm::cross(normal, right)), right};
}

std::optional<float> measureUvSkew(
  const vm::vec3d& uAxis, const vm::vec3d& vAxis, const vm::vec3d& faceNormal)
{
  if (vm::is_zero(faceNormal, vm::Cd::almost_zero()))
  {
    return std::nullopt;
  }

  const auto normal = vm::normalize(faceNormal);
  const auto project = [&](const auto& axis) {
    return axis - vm::dot(axis, normal) * normal;
  };
  const auto projectedUAxis = project(uAxis);
  const auto projectedVAxis = project(vAxis);
  if (
    vm::is_zero(projectedUAxis, vm::Cd::almost_zero())
    || vm::is_zero(projectedVAxis, vm::Cd::almost_zero()))
  {
    return std::nullopt;
  }

  const auto axisDot = std::clamp(
    std::abs(vm::dot(vm::normalize(projectedUAxis), vm::normalize(projectedVAxis))),
    0.0,
    1.0);
  return float(vm::to_degrees(std::asin(axisDot)));
}

vm::vec2f computeUvCoords(
  const vm::vec3d& point,
  const vm::vec3d& uAxis,
  const vm::vec3d& vAxis,
  const vm::vec2f& scale)
{
  return vm::vec2f{
    float(vm::dot(point, safeScaleAxis(uAxis, scale.x()))),
    float(vm::dot(point, safeScaleAxis(vAxis, scale.y())))};
}

vm::mat4x4d computeWorldToUvMatrix(
  const vm::vec3d& uAxis,
  const vm::vec3d& vAxis,
  const vm::vec3d& normal,
  const vm::vec2f& o,
  const vm::vec2f& s)
{
  const vm::vec3d u = safeScaleAxis(uAxis, s.x());
  const vm::vec3d v = safeScaleAxis(vAxis, s.y());
  const vm::vec3d n = normal;

  return vm::mat4x4d{
    u[0],
    u[1],
    u[2],
    o[0],
    v[0],
    v[1],
    v[2],
    o[1],
    n[0],
    n[1],
    n[2],
    0.0,
    0.0,
    0.0,
    0.0,
    1.0};
}

vm::mat4x4d computeUvToWorldMatrix(
  const vm::vec3d& uAxis,
  const vm::vec3d& vAxis,
  const vm::vec3d& normal,
  const vm::vec2f& offset,
  const vm::vec2f& scale)
{
  const auto result = invert(computeWorldToUvMatrix(uAxis, vAxis, normal, offset, scale));
  contract_assert(result);

  return *result;
}

Result<void> validateUvCoordSystem(
  const vm::vec3d& uAxis,
  const vm::vec3d& vAxis,
  const vm::vec3d& normal,
  const UvAttributes& uvAttributes)
{
  if (!vm::is_finite(uAxis) || !vm::is_finite(vAxis))
  {
    return Error{"UV axes are invalid"};
  }

  const auto neutralMatrix =
    computeWorldToUvMatrix(uAxis, vAxis, normal, vm::vec2f{0, 0}, vm::vec2f{1, 1});
  if (!vm::invert(neutralMatrix))
  {
    return Error{"UV axes do not form an invertible coordinate system"};
  }

  const auto actualMatrix =
    computeWorldToUvMatrix(uAxis, vAxis, normal, uvAttributes.offset, uvAttributes.scale);
  if (!vm::invert(actualMatrix))
  {
    return Error{"UV coordinate system is not invertible"};
  }

  return kdl::void_success;
}

} // namespace tb::mdl
