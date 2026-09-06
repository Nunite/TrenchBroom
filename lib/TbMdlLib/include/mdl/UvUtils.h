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

#pragma once

#include "base/Result.h"

#include "vm/constants.h"
#include "vm/mat.h"
#include "vm/scalar.h"
#include "vm/vec.h"

#include <optional>
#include <tuple>
#include <vector>

namespace tb::mdl
{

struct FaceUVProjection
{
  vm::vec3d uAxis;
  vm::vec3d vAxis;
  vm::vec2f offset;
};

/**
 * Solves the affine world-to-UV projection defined by the given corresponding
 * points.
 * Returns nullopt if the input is degenerate or the UV coordinates are not
 * affine.
 */
std::optional<FaceUVProjection> solveFaceUVProjection(
  const std::vector<vm::vec3d>& points, const std::vector<vm::vec2f>& uvs);

struct UvAttributes;

/**
 * Return the up and right axes for a camera that looks at the given face.
 */
std::tuple<vm::vec3d, vm::vec3d> computeCameraAxesForFaceNormal(const vm::vec3d& normal);

/**
 * Returns the angular deviation from orthogonality of the UV axes after projecting
 *
 * them
 * onto the face plane. A value of 0 means that the mapping has no affine skew.
 *
 * Returns nullopt if either projected axis is degenerate.
 */
std::optional<float> measureUvSkew(
  const vm::vec3d& uAxis, const vm::vec3d& vAxis, const vm::vec3d& faceNormal);

/**
 * Checks that the given UV axes are finite and that they produce an invertible UV
 *
 * coordinate system matrix, both with a neutral offset/scale and with the given
 *
 * uvAttributes' actual offset/scale -- a finite but extreme scale can make an otherwise
 * fine pair of axes non-invertible even though the axes alone would be fine.
 */
Result<void> validateUvCoordSystem(
  const vm::vec3d& uAxis,
  const vm::vec3d& vAxis,
  const vm::vec3d& normal,
  const UvAttributes& uvAttributes);

/**
 * Returns the given scaling factor, or 1 if it is 0.
 */
template <typename T>
T safeScale(const T value)
{
  return vm::is_equal(value, T(0.0), vm::constants<T>::almost_zero())
           ? static_cast<T>(1.0)
           : value;
}

/**
 * Scales the given axis by the inverse of the given factor, treating a factor of 0 as 1.
 */
template <typename T1, typename T2>
vm::vec<T1, 3> safeScaleAxis(const vm::vec<T1, 3>& axis, const T2 factor)
{
  return axis / safeScale(T1(factor));
}

/**
 * Returns the UV coords of the given point in a UV coordinate system with the given axes
 * and scaling factors, disregarding any offset.
 */
vm::vec2f computeUvCoords(
  const vm::vec3d& point,
  const vm::vec3d& uAxis,
  const vm::vec3d& vAxis,
  const vm::vec2f& scale);

/**
 * Returns a matrix which transforms a point from world space into the UV coordinate
 * system with the given axes, offset and scaling factors.
 */
vm::mat4x4d computeWorldToUvMatrix(
  const vm::vec3d& uAxis,
  const vm::vec3d& vAxis,
  const vm::vec3d& normal,
  const vm::vec2f& offset,
  const vm::vec2f& scale);

/**
 * Returns the inverse of computeWorldToUvMatrix.
 */
vm::mat4x4d computeUvToWorldMatrix(
  const vm::vec3d& uAxis,
  const vm::vec3d& vAxis,
  const vm::vec3d& normal,
  const vm::vec2f& offset,
  const vm::vec2f& scale);

} // namespace tb::mdl
