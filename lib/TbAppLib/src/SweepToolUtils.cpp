/*
 Copyright (C) 2026 Jackson Palmer
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

#include "ui/SweepToolUtils.h"

#include "base/Logger.h"
#include "gl/Material.h"
#include "gl/MaterialManager.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h" // IWYU pragma: keep
#include "mdl/BrushNode.h"
#include "mdl/GameConfig.h"
#include "mdl/GameInfo.h"
#include "mdl/Grid.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Node.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"
#include "render/BrushRenderer.h"
#include "ui/MapDocument.h"

#include "kd/ranges/to.h"
#include "kd/reflection_impl.h"
#include "kd/result.h"

#include "vm/mat.h"
#include "vm/mat_ext.h"
#include "vm/plane.h"
#include "vm/plane_io.h"   // IWYU pragma: keep
#include "vm/polygon_io.h" // IWYU pragma: keep
#include "vm/quat.h"
#include "vm/quat_io.h" // IWYU pragma: keep
#include "vm/vec.h"
#include "vm/vec_io.h" // IWYU pragma: keep

#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <ranges>
#include <set>
#include <string>

namespace tb::ui
{

namespace
{

std::optional<vm::vec3d> arcPivot(
  const SweepSource& source, const SweepTransform& transform, const vm::quatd& rotation)
{
  const auto theta = rotation.angle();
  if (std::abs(theta) < vm::Cd::almost_zero())
  {
    return std::nullopt;
  }

  const auto axis = vm::normalize(rotation.axis());
  const auto c0 = source.center;
  const auto c1 = transform.destinationCenter(source);

  // work in the plane perpendicular to the axis; the rise becomes a helix lift
  const auto rise = vm::dot(c1 - c0, axis);
  const auto u1 = c1 - axis * rise;
  const auto chord = u1 - c0;
  const auto chordLength = vm::length(chord);
  if (chordLength < vm::Cd::almost_zero())
  {
    // no lateral travel, so there is no circle to fit
    return std::nullopt;
  }

  auto inPlanePerp = vm::cross(axis, chord);
  const auto perpLength = vm::length(inPlanePerp);
  if (perpLength < vm::Cd::almost_zero())
  {
    return std::nullopt;
  }
  inPlanePerp = inPlanePerp / perpLength;

  const auto mid = (c0 + u1) * 0.5;
  const auto distanceMidToCenter = (chordLength * 0.5) / std::tan(theta * 0.5);

  // pick the candidate center whose rotation by theta maps c0 onto u1
  const auto turn = vm::quatd{axis, theta};
  auto bestPivot = mid;
  auto bestError = std::numeric_limits<double>::max();
  for (const auto sign : {1.0, -1.0})
  {
    const auto pivot = mid + inPlanePerp * (distanceMidToCenter * sign);
    const auto mapped = pivot + turn * (c0 - pivot);
    if (const auto error = vm::squared_length(mapped - u1); error < bestError)
    {
      bestError = error;
      bestPivot = pivot;
    }
  }
  return bestPivot;
}

vm::vec3d sBendOffset(
  const SweepSource& source,
  const SweepTransform& transform,
  const double t,
  const vm::quatd& rotation)
{
  // cubic Hermite with end tangents along the source normal and the rotated cap normal
  const auto chordLength = vm::length(transform.translation);
  const auto startTangent = source.normal * chordLength;
  const auto endTangent = (rotation * source.normal) * chordLength;

  const auto t2 = t * t;
  const auto t3 = t2 * t;
  return startTangent * (t3 - 2.0 * t2 + t)
         + transform.translation * (-2.0 * t3 + 3.0 * t2) + endTangent * (t3 - t2);
}

vm::mat4x4d stationTransformWithPivot(
  const SweepSource& source,
  const SweepTransform& transform,
  const SweepParameters& parameters,
  const double t,
  const vm::quatd& rotationFull,
  const std::optional<vm::vec3d>& pivot)
{
  const auto c0 = source.center;
  const auto angle = rotationFull.angle();
  const auto hasRotation = std::abs(angle) > vm::Cd::almost_zero();
  // axis() is degenerate at ~0 rotation, so pick a safe default
  const auto axis = hasRotation ? vm::normalize(rotationFull.axis()) : vm::vec3d{0, 0, 1};
  const auto rotation = vm::quatd{axis, angle * t};
  const auto factors = vm::vec3d{1, 1, 1} + (transform.scale - vm::vec3d{1, 1, 1}) * t;

  if (parameters.pathMode == SweepPathMode::Arc && pivot)
  {
    const auto rise = vm::dot(transform.translation, axis);
    auto m = vm::translation_matrix(-c0);
    m = vm::scaling_matrix(factors) * m;
    m = vm::translation_matrix(c0) * m; // scale about c0
    m = vm::translation_matrix(-*pivot) * m;
    m = vm::rotation_matrix(rotation) * m;                      // revolve about the pivot
    m = vm::translation_matrix(*pivot + axis * (rise * t)) * m; // helix lift
    return m;
  }
  // no usable pivot, fall through to the straight path

  const auto sBend = parameters.pathMode == SweepPathMode::SBend
                     && !vm::is_zero(source.normal, vm::Cd::almost_zero());
  auto m = vm::translation_matrix(-c0);
  m = vm::scaling_matrix(factors) * m;
  m = vm::rotation_matrix(rotation) * m;
  m = vm::translation_matrix(c0) * m;
  m =
    vm::translation_matrix(
      sBend ? sBendOffset(source, transform, t, rotationFull) : transform.translation * t)
    * m;
  return m;
}

std::optional<vm::vec3d> stationArcPivot(
  const SweepSource& source,
  const SweepTransform& transform,
  const SweepParameters& parameters,
  const vm::quatd& rotationFull)
{
  return parameters.pathMode == SweepPathMode::Arc
           ? arcPivot(source, transform, rotationFull)
           : std::nullopt;
}

std::vector<vm::mat4x4d> computeSweepPowers(
  const SweepSource& source,
  const SweepTransform& transform,
  const SweepParameters& parameters,
  const vm::quatd& rotation,
  const std::optional<vm::vec3d>& pivot)
{
  // iteration r continues from the previous cap, i.e. applies the cap transform r times,
  // so powers[r] is the exclusive prefix product capTransform^r
  const auto capTransform =
    stationTransformWithPivot(source, transform, parameters, 1.0, rotation, pivot);
  const auto capTransforms = std::views::iota(size_t{0}, parameters.iterations)
                             | std::views::transform([&](auto) { return capTransform; });
  auto powers = std::vector<vm::mat4x4d>{};
  std::exclusive_scan(
    capTransforms.begin(),
    capTransforms.end(),
    std::back_inserter(powers),
    vm::mat4x4d::identity(),
    std::multiplies<>());
  return powers;
}

std::vector<std::vector<vm::mat4x4d>> computeTransformTable(
  const SweepSource& source,
  const SweepTransform& transform,
  const SweepParameters& parameters)
{
  const auto rotation = transform.effectiveRotation();
  const auto pivot = stationArcPivot(source, transform, parameters, rotation);
  const auto powers = computeSweepPowers(source, transform, parameters, rotation, pivot);
  contract_assert(powers.size() == parameters.iterations);

  return std::views::iota(0u, parameters.iterations)
         | std::views::transform([&](const auto r) {
             return std::views::iota(0u, parameters.segments + 1u)
                    | std::views::transform([&](const auto s) {
                        const auto t = double(s) / double(parameters.segments);
                        return powers[r]
                               * stationTransformWithPivot(
                                 source, transform, parameters, t, rotation, pivot);
                      })
                    | kdl::ranges::to<std::vector>();
           })
         | kdl::ranges::to<std::vector>();
}

bool pointsContain(
  const std::vector<vm::vec3d>& points,
  const vm::vec3d& candidate,
  const double epsilon = vm::Cd::almost_zero())
{
  return std::ranges::any_of(
    points, [&](const auto& point) { return vm::is_equal(point, candidate, epsilon); });
}

bool faceUsesOnlyPoints(const mdl::BrushFace& face, const std::vector<vm::vec3d>& points)
{
  const auto faceVertices = face.vertexPositions();
  return faceVertices.size() >= 3u
         && std::ranges::all_of(faceVertices, [&](const auto& vertex) {
              return pointsContain(points, vertex);
            });
}

struct ContinuousUvEdge
{
  size_t sourceFaceIndex;
  size_t sourceEdgeIndex;
  vm::vec3d start;
  vm::vec3d end;
  const SweepFaceAttributes* attributes;
  bool internal = false;
};

struct ContinuousUvEdgeLocation
{
  size_t componentIndex;
  size_t edgeIndex;
  bool forward;
};

struct ContinuousUvComponent
{
  std::vector<std::vector<vm::vec2f>> stationUvs;
};

struct ContinuousUvLayout
{
  std::vector<std::vector<std::optional<ContinuousUvEdgeLocation>>> edgeLocations;
  std::vector<ContinuousUvComponent> components;
  std::vector<SweepIssue> issues;
};

bool sameUndirectedEdge(const ContinuousUvEdge& lhs, const ContinuousUvEdge& rhs)
{
  return (
           vm::is_equal(lhs.start, rhs.start, vm::Cd::almost_zero())
           && vm::is_equal(lhs.end, rhs.end, vm::Cd::almost_zero()))
         || (
           vm::is_equal(lhs.start, rhs.end, vm::Cd::almost_zero())
           && vm::is_equal(lhs.end, rhs.start, vm::Cd::almost_zero()));
}

size_t findOrAddVertex(std::vector<vm::vec3d>& vertices, const vm::vec3d& vertex)
{
  const auto it = std::ranges::find_if(vertices, [&](const auto& candidate) {
    return vm::is_equal(candidate, vertex, vm::Cd::almost_zero());
  });
  if (it != vertices.end())
  {
    return size_t(std::distance(vertices.begin(), it));
  }

  vertices.push_back(vertex);
  return vertices.size() - 1u;
}

bool pointsAreCoplanar(const std::array<vm::vec3d, 4>& points)
{
  const auto plane = vm::from_points(points[0], points[1], points[3]);
  auto extent = 0.0;
  for (size_t axis = 0; axis < 3u; ++axis)
  {
    const auto [minIt, maxIt] = std::ranges::minmax_element(
      points, {}, [axis](const auto& point) { return point[axis]; });
    extent = std::max(extent, (*maxIt)[axis] - (*minIt)[axis]);
  }

  const auto defaultEpsilon = vm::constants<double>::point_status_epsilon();
  const auto planeEpsilon = std::max(defaultEpsilon, extent / 10.0 * defaultEpsilon);
  return plane && vm::is_zero(plane->point_distance(points[2]), planeEpsilon);
}

struct AffineDependency
{
  std::array<std::pair<size_t, double>, 3> terms;
};

struct AffineVariableElimination
{
  std::vector<std::optional<AffineDependency>> dependencies;
  std::vector<std::optional<size_t>> freeVariables;
  size_t freeVariableCount = 0u;

  std::vector<double> expand(
    const std::vector<double>& reduced, const std::vector<double>& pinned) const
  {
    auto result = pinned;
    for (size_t i = 0; i < result.size(); ++i)
    {
      if (freeVariables[i])
      {
        result[i] += reduced[*freeVariables[i]];
      }
      else if (dependencies[i])
      {
        result[i] = 0.0;
        for (const auto [index, coefficient] : dependencies[i]->terms)
        {
          result[i] += coefficient * result[index];
        }
      }
    }
    return result;
  }

  std::vector<double> reduce(const std::vector<double>& values) const
  {
    auto accumulated = values;
    for (size_t i = accumulated.size(); i-- > 0u;)
    {
      if (dependencies[i])
      {
        for (const auto [index, coefficient] : dependencies[i]->terms)
        {
          accumulated[index] += coefficient * accumulated[i];
        }
      }
    }

    auto result = std::vector<double>(freeVariableCount, 0.0);
    for (size_t i = 0; i < accumulated.size(); ++i)
    {
      if (freeVariables[i])
      {
        result[*freeVariables[i]] = accumulated[i];
      }
    }
    return result;
  }

  std::vector<double> reduceDiagonal(const std::vector<double>& diagonal) const
  {
    auto accumulated = diagonal;
    for (size_t i = accumulated.size(); i-- > 0u;)
    {
      if (dependencies[i])
      {
        for (const auto [index, coefficient] : dependencies[i]->terms)
        {
          accumulated[index] += coefficient * coefficient * accumulated[i];
        }
      }
    }

    auto result = std::vector<double>(freeVariableCount, 1.0);
    for (size_t i = 0; i < accumulated.size(); ++i)
    {
      if (freeVariables[i])
      {
        result[*freeVariables[i]] =
          std::max(accumulated[i], std::numeric_limits<double>::epsilon());
      }
    }
    return result;
  }
};

struct ArapTriangle
{
  std::array<size_t, 3> vertices;
  std::array<vm::vec2d, 3> gradients;
  double weight;
};

std::optional<ArapTriangle> makeArapTriangle(
  const std::array<size_t, 3>& vertices,
  const std::vector<vm::vec3d>& points,
  const double blendWeight)
{
  const auto edge1 = points[vertices[1]] - points[vertices[0]];
  const auto edge2 = points[vertices[2]] - points[vertices[0]];
  const auto x1 = vm::length(edge1);
  if (x1 <= vm::Cd::almost_zero())
  {
    return std::nullopt;
  }

  const auto x2 = vm::dot(edge1, edge2) / x1;
  const auto y2Squared = vm::squared_length(edge2) - x2 * x2;
  if (y2Squared <= vm::Cd::almost_zero() * vm::Cd::almost_zero())
  {
    return std::nullopt;
  }
  const auto y2 = std::sqrt(y2Squared);

  const auto gradient1 = vm::vec2d{1.0 / x1, -x2 / (x1 * y2)};
  const auto gradient2 = vm::vec2d{0.0, 1.0 / y2};
  return ArapTriangle{
    vertices,
    {-gradient1 - gradient2, gradient1, gradient2},
    blendWeight * x1 * y2 * 0.5};
}

std::vector<double> multiplyArapMatrix(
  const std::vector<ArapTriangle>& triangles, const std::vector<double>& values)
{
  auto result = std::vector<double>(values.size(), 0.0);
  for (const auto& triangle : triangles)
  {
    auto gradient = vm::vec2d{};
    for (size_t i = 0; i < 3u; ++i)
    {
      gradient = gradient + values[triangle.vertices[i]] * triangle.gradients[i];
    }
    for (size_t i = 0; i < 3u; ++i)
    {
      result[triangle.vertices[i]] +=
        triangle.weight * vm::dot(gradient, triangle.gradients[i]);
    }
  }
  return result;
}

std::optional<std::vector<double>> solveConjugateGradient(
  const std::vector<double>& rhs,
  std::vector<double> solution,
  const std::vector<double>& diagonal,
  const auto& multiply)
{
  if (rhs.empty())
  {
    return solution;
  }

  const auto matrixSolution = multiply(solution);
  auto residual = std::vector<double>(rhs.size());
  auto preconditionedResidual = std::vector<double>(rhs.size());
  for (size_t i = 0; i < rhs.size(); ++i)
  {
    residual[i] = rhs[i] - matrixSolution[i];
    preconditionedResidual[i] = residual[i] / diagonal[i];
  }
  auto direction = preconditionedResidual;
  auto residualDotPreconditioned = std::inner_product(
    residual.begin(), residual.end(), preconditionedResidual.begin(), 0.0);
  const auto rhsNormSquared =
    std::inner_product(rhs.begin(), rhs.end(), rhs.begin(), 0.0);
  const auto toleranceSquared = std::max(1.0, rhsNormSquared) * 1e-12;
  if (residualDotPreconditioned <= toleranceSquared)
  {
    return solution;
  }

  const auto maxIterations = std::min<size_t>(rhs.size() * 6u, 4096u);
  for (size_t iteration = 0; iteration < maxIterations; ++iteration)
  {
    const auto matrixDirection = multiply(direction);
    const auto denominator = std::inner_product(
      direction.begin(), direction.end(), matrixDirection.begin(), 0.0);
    if (denominator <= std::numeric_limits<double>::epsilon())
    {
      const auto residualNormSquared =
        std::inner_product(residual.begin(), residual.end(), residual.begin(), 0.0);
      return residualNormSquared <= toleranceSquared ? std::optional{solution}
                                                     : std::nullopt;
    }

    const auto step = residualDotPreconditioned / denominator;
    for (size_t i = 0; i < rhs.size(); ++i)
    {
      solution[i] += step * direction[i];
      residual[i] -= step * matrixDirection[i];
    }

    const auto residualNormSquared =
      std::inner_product(residual.begin(), residual.end(), residual.begin(), 0.0);
    if (residualNormSquared <= toleranceSquared)
    {
      return solution;
    }

    for (size_t i = 0; i < rhs.size(); ++i)
    {
      preconditionedResidual[i] = residual[i] / diagonal[i];
    }
    const auto nextResidualDotPreconditioned = std::inner_product(
      residual.begin(), residual.end(), preconditionedResidual.begin(), 0.0);
    const auto beta = nextResidualDotPreconditioned / residualDotPreconditioned;
    for (size_t i = 0; i < rhs.size(); ++i)
    {
      direction[i] = preconditionedResidual[i] + beta * direction[i];
    }
    residualDotPreconditioned = nextResidualDotPreconditioned;
  }
  return std::nullopt;
}

std::optional<std::vector<double>> factorPositiveDefiniteMatrix(
  const size_t size, const auto& multiply)
{
  auto matrix = std::vector<double>(size * size, 0.0);
  for (size_t column = 0; column < size; ++column)
  {
    auto basis = std::vector<double>(size, 0.0);
    basis[column] = 1.0;
    const auto result = multiply(basis);
    for (size_t row = 0; row < size; ++row)
    {
      matrix[row * size + column] = result[row];
    }
  }

  auto factor = std::vector<double>(size * size, 0.0);
  for (size_t row = 0; row < size; ++row)
  {
    for (size_t column = 0; column <= row; ++column)
    {
      auto value = 0.5 * (matrix[row * size + column] + matrix[column * size + row]);
      for (size_t k = 0; k < column; ++k)
      {
        value -= factor[row * size + k] * factor[column * size + k];
      }
      if (row == column)
      {
        if (value <= std::numeric_limits<double>::epsilon())
        {
          return std::nullopt;
        }
        factor[row * size + column] = std::sqrt(value);
      }
      else
      {
        factor[row * size + column] = value / factor[column * size + column];
      }
    }
  }
  return factor;
}

std::vector<double> solvePositiveDefiniteFactor(
  const std::vector<double>& factor, std::vector<double> values)
{
  const auto size = values.size();
  for (size_t row = 0; row < size; ++row)
  {
    for (size_t column = 0; column < row; ++column)
    {
      values[row] -= factor[row * size + column] * values[column];
    }
    values[row] /= factor[row * size + row];
  }
  for (size_t row = size; row-- > 0u;)
  {
    for (size_t column = row + 1u; column < size; ++column)
    {
      values[row] -= factor[column * size + row] * values[column];
    }
    values[row] /= factor[row * size + row];
  }
  return values;
}

std::optional<std::vector<vm::vec2d>> solveConstrainedArap(
  std::vector<vm::vec2d> coordinates,
  const std::vector<ArapTriangle>& triangles,
  const AffineVariableElimination& elimination,
  const size_t pinnedVariableCount)
{
  const auto variableCount = coordinates.size();
  auto pinnedX = std::vector<double>(variableCount, 0.0);
  auto pinnedY = std::vector<double>(variableCount, 0.0);
  for (size_t i = 0; i < pinnedVariableCount; ++i)
  {
    pinnedX[i] = coordinates[i][0];
    pinnedY[i] = coordinates[i][1];
  }

  auto matrixDiagonal = std::vector<double>(variableCount, 0.0);
  for (const auto& triangle : triangles)
  {
    for (size_t i = 0; i < 3u; ++i)
    {
      matrixDiagonal[triangle.vertices[i]] +=
        triangle.weight * vm::squared_length(triangle.gradients[i]);
    }
  }
  const auto reducedDiagonal = elimination.reduceDiagonal(matrixDiagonal);
  const auto zeroReduced = std::vector<double>(elimination.freeVariableCount, 0.0);
  const auto fixedX = elimination.expand(zeroReduced, pinnedX);
  const auto fixedY = elimination.expand(zeroReduced, pinnedY);

  const auto reducedMultiply = [&](const std::vector<double>& values) {
    const auto expanded =
      elimination.expand(values, std::vector<double>(variableCount, 0.0));
    return elimination.reduce(multiplyArapMatrix(triangles, expanded));
  };

  constexpr auto directSolveLimit = 256u;
  auto directFactor = std::optional<std::vector<double>>{};
  if (elimination.freeVariableCount <= directSolveLimit)
  {
    directFactor =
      factorPositiveDefiniteMatrix(elimination.freeVariableCount, reducedMultiply);
    if (!directFactor)
    {
      return std::nullopt;
    }
  }

  constexpr auto maxArapIterations = 32u;
  for (size_t arapIteration = 0; arapIteration < maxArapIterations; ++arapIteration)
  {
    auto rhsX = std::vector<double>(variableCount, 0.0);
    auto rhsY = std::vector<double>(variableCount, 0.0);
    for (const auto& triangle : triangles)
    {
      auto j00 = 0.0;
      auto j01 = 0.0;
      auto j10 = 0.0;
      auto j11 = 0.0;
      for (size_t i = 0; i < 3u; ++i)
      {
        const auto& uv = coordinates[triangle.vertices[i]];
        const auto& gradient = triangle.gradients[i];
        j00 += uv[0] * gradient[0];
        j01 += uv[0] * gradient[1];
        j10 += uv[1] * gradient[0];
        j11 += uv[1] * gradient[1];
      }

      const auto trace = j00 + j11;
      const auto skew = j10 - j01;
      const auto rotationLength = std::hypot(trace, skew);
      const auto cosine =
        rotationLength > vm::Cd::almost_zero() ? trace / rotationLength : 1.0;
      const auto sine =
        rotationLength > vm::Cd::almost_zero() ? skew / rotationLength : 0.0;
      for (size_t i = 0; i < 3u; ++i)
      {
        const auto& gradient = triangle.gradients[i];
        rhsX[triangle.vertices[i]] +=
          triangle.weight * (cosine * gradient[0] - sine * gradient[1]);
        rhsY[triangle.vertices[i]] +=
          triangle.weight * (sine * gradient[0] + cosine * gradient[1]);
      }
    }

    const auto fixedMatrixX = multiplyArapMatrix(triangles, fixedX);
    const auto fixedMatrixY = multiplyArapMatrix(triangles, fixedY);
    for (size_t i = 0; i < variableCount; ++i)
    {
      rhsX[i] -= fixedMatrixX[i];
      rhsY[i] -= fixedMatrixY[i];
    }
    const auto reducedRhsX = elimination.reduce(rhsX);
    const auto reducedRhsY = elimination.reduce(rhsY);

    auto initialX = std::vector<double>(elimination.freeVariableCount, 0.0);
    auto initialY = std::vector<double>(elimination.freeVariableCount, 0.0);
    for (size_t i = 0; i < variableCount; ++i)
    {
      if (elimination.freeVariables[i])
      {
        initialX[*elimination.freeVariables[i]] = coordinates[i][0];
        initialY[*elimination.freeVariables[i]] = coordinates[i][1];
      }
    }

    const auto solvedX =
      directFactor
        ? std::optional{solvePositiveDefiniteFactor(*directFactor, reducedRhsX)}
        : solveConjugateGradient(
            reducedRhsX, std::move(initialX), reducedDiagonal, reducedMultiply);
    const auto solvedY =
      directFactor
        ? std::optional{solvePositiveDefiniteFactor(*directFactor, reducedRhsY)}
        : solveConjugateGradient(
            reducedRhsY, std::move(initialY), reducedDiagonal, reducedMultiply);
    if (!solvedX || !solvedY)
    {
      return std::nullopt;
    }

    const auto expandedX = elimination.expand(*solvedX, pinnedX);
    const auto expandedY = elimination.expand(*solvedY, pinnedY);
    auto maxDelta = 0.0;
    for (size_t i = 0; i < variableCount; ++i)
    {
      const auto next = vm::vec2d{expandedX[i], expandedY[i]};
      maxDelta = std::max(maxDelta, vm::length(next - coordinates[i]));
      coordinates[i] = next;
    }
    if (maxDelta <= 1e-5)
    {
      break;
    }
  }
  return coordinates;
}

float usableUvScale(const float scale)
{
  return std::abs(scale) > 0.0001f ? scale : 1.0f;
}

std::optional<vm::vec2d> sourceFaceUv(
  const SweepFaceAttributes& attributes, const vm::vec3d& point)
{
  if (!attributes.uvCoordSystemSnapshot)
  {
    return std::nullopt;
  }

  const auto uScale = double(usableUvScale(attributes.uvAttributes.scale.x()));
  const auto vScale = double(usableUvScale(attributes.uvAttributes.scale.y()));
  return vm::vec2d{
    vm::dot(point, attributes.uvCoordSystemSnapshot->uAxis) / uScale
      + double(attributes.uvAttributes.offset.x()),
    vm::dot(point, attributes.uvCoordSystemSnapshot->vAxis) / vScale
      + double(attributes.uvAttributes.offset.y())};
}

bool sourceUvsMeetAtSharedVertex(
  const ContinuousUvEdge& lhs,
  const ContinuousUvEdge& rhs,
  const std::optional<vm::vec2d>& textureSize)
{
  for (const auto& lhsPoint : {lhs.start, lhs.end})
  {
    for (const auto& rhsPoint : {rhs.start, rhs.end})
    {
      if (!vm::is_equal(lhsPoint, rhsPoint, vm::Cd::almost_zero()))
      {
        continue;
      }

      const auto lhsUv = sourceFaceUv(*lhs.attributes, lhsPoint);
      const auto rhsUv = sourceFaceUv(*rhs.attributes, rhsPoint);
      if (!lhsUv || !rhsUv)
      {
        return true;
      }

      const auto phaseDelta = *lhsUv - *rhsUv;
      auto phaseResidual = phaseDelta;
      if (textureSize)
      {
        for (size_t axis = 0; axis < 2u; ++axis)
        {
          phaseResidual[axis] -=
            std::round(phaseResidual[axis] / (*textureSize)[axis]) * (*textureSize)[axis];
        }
      }
      // The source seam alone determines continuity. Projecting a transformed station
      // through the original face axes would split a connected strip as soon as it bends.
      return vm::is_zero(phaseResidual, 0.001);
    }
  }
  return false;
}

template <typename Station>
std::optional<std::vector<std::vector<vm::vec2f>>> buildAffineUvGrid(
  const std::vector<vm::vec3d>& profileVertices,
  const std::optional<std::vector<vm::vec2f>>& sourceProfileUvs,
  const SweepFaceAttributes& seedAttributes,
  const SweepParameters& parameters,
  const Station& station)
{
  const auto stationCount = parameters.segments * parameters.iterations + 1u;
  const auto profileVertexCount = profileVertices.size();
  if (stationCount < 2u || profileVertexCount < 2u)
  {
    return std::nullopt;
  }

  const auto gridIndex = [=](const size_t stationIndex, const size_t profileIndex) {
    return stationIndex * profileVertexCount + profileIndex;
  };
  const auto stationPoint = [&](const size_t globalStation, const size_t profileIndex) {
    const auto iteration =
      std::min(globalStation / parameters.segments, parameters.iterations - 1u);
    const auto segment = globalStation == stationCount - 1u
                           ? parameters.segments
                           : globalStation % parameters.segments;
    return station(profileVertices[profileIndex], iteration, segment);
  };

  const auto closedProfile =
    vm::is_equal(profileVertices.front(), profileVertices.back(), vm::Cd::almost_zero());
  const auto pathSampleCount = profileVertexCount - (closedProfile ? 1u : 0u);
  const auto uScale = double(usableUvScale(seedAttributes.uvAttributes.scale.x()));
  const auto vScale = double(usableUvScale(seedAttributes.uvAttributes.scale.y()));

  auto seedUv = vm::vec2d{
    double(seedAttributes.uvAttributes.offset.x()),
    double(seedAttributes.uvAttributes.offset.y())};
  auto pathDirection = vm::vec2d{uScale < 0.0 ? -1.0 : 1.0, 0.0};
  auto profileDirection = vm::vec2d{0.0, vScale < 0.0 ? -1.0 : 1.0};
  auto pathUvPerWorldUnit = 1.0 / std::abs(uScale);
  auto profileUvPerWorldUnit = 1.0 / std::abs(vScale);

  if (seedAttributes.uvCoordSystemSnapshot)
  {
    const auto rawSeedUv = [&](const vm::vec3d& point) {
      return *sourceFaceUv(seedAttributes, point);
    };

    seedUv = sourceProfileUvs && sourceProfileUvs->size() == profileVertexCount
               ? vm::vec2d{sourceProfileUvs->front()}
               : rawSeedUv(profileVertices[0]);
    const auto seedEdgeLength = vm::length(profileVertices[1] - profileVertices[0]);
    const auto seedUvEdge =
      sourceProfileUvs && sourceProfileUvs->size() == profileVertexCount
        ? vm::vec2d{(*sourceProfileUvs)[1] - sourceProfileUvs->front()}
        : rawSeedUv(profileVertices[1]) - seedUv;
    const auto seedUvEdgeLength = vm::length(seedUvEdge);
    if (
      seedEdgeLength > vm::Cd::almost_zero() && seedUvEdgeLength > vm::Cd::almost_zero())
    {
      profileDirection = seedUvEdge / seedUvEdgeLength;
      profileUvPerWorldUnit = seedUvEdgeLength / seedEdgeLength;
      pathDirection = vm::vec2d{profileDirection[1], -profileDirection[0]};

      auto averageWorldStep = 0.0;
      auto averageUvStep = vm::vec2d{0.0, 0.0};
      for (size_t profileIndex = 0; profileIndex < pathSampleCount; ++profileIndex)
      {
        const auto start = stationPoint(0u, profileIndex);
        const auto end = stationPoint(1u, profileIndex);
        averageWorldStep += vm::length(end - start);
        averageUvStep = averageUvStep + rawSeedUv(end) - rawSeedUv(start);
      }
      averageWorldStep /= double(pathSampleCount);
      averageUvStep = averageUvStep / double(pathSampleCount);
      if (vm::dot(averageUvStep, pathDirection) < 0.0)
      {
        pathDirection = -pathDirection;
      }
      if (averageWorldStep > vm::Cd::almost_zero())
      {
        const auto perpendicularUvStep = std::abs(vm::dot(averageUvStep, pathDirection));
        if (perpendicularUvStep > vm::Cd::almost_zero())
        {
          pathUvPerWorldUnit = perpendicularUvStep / averageWorldStep;
        }
      }
    }
  }

  auto profileCoordinates = std::vector<double>(profileVertexCount, 0.0);
  for (size_t i = 1u; i < profileVertexCount; ++i)
  {
    profileCoordinates[i] =
      profileCoordinates[i - 1u]
      + vm::length(profileVertices[i] - profileVertices[i - 1u]) * profileUvPerWorldUnit;
  }

  auto pathCoordinates = std::vector<double>(stationCount, 0.0);
  for (size_t stationIndex = 1u; stationIndex < stationCount; ++stationIndex)
  {
    auto averageLength = 0.0;
    for (size_t profileIndex = 0; profileIndex < pathSampleCount; ++profileIndex)
    {
      averageLength += vm::length(
        stationPoint(stationIndex, profileIndex)
        - stationPoint(stationIndex - 1u, profileIndex));
    }
    averageLength /= double(pathSampleCount);
    pathCoordinates[stationIndex] =
      pathCoordinates[stationIndex - 1u] + averageLength * pathUvPerWorldUnit;
  }

  auto sourceLocalCoordinates = std::vector<vm::vec2d>(profileVertexCount);
  if (sourceProfileUvs && sourceProfileUvs->size() == profileVertexCount)
  {
    for (size_t profileIndex = 0; profileIndex < profileVertexCount; ++profileIndex)
    {
      const auto delta = vm::vec2d{(*sourceProfileUvs)[profileIndex]} - seedUv;
      sourceLocalCoordinates[profileIndex] = vm::vec2d{
        vm::dot(delta, pathDirection) / pathUvPerWorldUnit,
        vm::dot(delta, profileDirection) / profileUvPerWorldUnit};
    }
  }
  else
  {
    for (size_t profileIndex = 0; profileIndex < profileVertexCount; ++profileIndex)
    {
      sourceLocalCoordinates[profileIndex] =
        vm::vec2d{0.0, profileCoordinates[profileIndex] / profileUvPerWorldUnit};
    }
  }

  const auto variableCount = stationCount * profileVertexCount;
  auto points = std::vector<vm::vec3d>(variableCount);
  auto localCoordinates = std::vector<vm::vec2d>(variableCount);
  for (size_t stationIndex = 0; stationIndex < stationCount; ++stationIndex)
  {
    for (size_t profileIndex = 0; profileIndex < profileVertexCount; ++profileIndex)
    {
      const auto index = gridIndex(stationIndex, profileIndex);
      points[index] = stationPoint(stationIndex, profileIndex);
      localCoordinates[index] =
        sourceLocalCoordinates[profileIndex]
        + vm::vec2d{pathCoordinates[stationIndex] / pathUvPerWorldUnit, 0.0};
    }
  }

  auto elimination = AffineVariableElimination{};
  elimination.dependencies.resize(variableCount);
  elimination.freeVariables.resize(variableCount);
  auto triangles = std::vector<ArapTriangle>{};
  triangles.reserve((stationCount - 1u) * (profileVertexCount - 1u) * 4u);
  for (size_t stationIndex = 0; stationIndex + 1u < stationCount; ++stationIndex)
  {
    for (size_t profileIndex = 0; profileIndex + 1u < profileVertexCount; ++profileIndex)
    {
      const auto i00 = gridIndex(stationIndex, profileIndex);
      const auto i01 = gridIndex(stationIndex, profileIndex + 1u);
      const auto i10 = gridIndex(stationIndex + 1u, profileIndex);
      const auto i11 = gridIndex(stationIndex + 1u, profileIndex + 1u);
      const auto& p00 = points[i00];
      const auto& p01 = points[i01];
      const auto& p10 = points[i10];
      const auto& p11 = points[i11];
      const auto coplanar = pointsAreCoplanar({p00, p01, p11, p10});

      for (const auto& [vertices, weight] :
           {std::pair{std::array{i00, i01, i11}, coplanar ? 1.0 : 0.5},
            std::pair{std::array{i00, i11, i10}, coplanar ? 1.0 : 0.5}})
      {
        if (const auto triangle = makeArapTriangle(vertices, points, weight))
        {
          triangles.push_back(*triangle);
        }
      }
      if (!coplanar)
      {
        for (const auto& vertices :
             {std::array{i00, i01, i10}, std::array{i01, i11, i10}})
        {
          if (const auto triangle = makeArapTriangle(vertices, points, 0.5))
          {
            triangles.push_back(*triangle);
          }
        }
        continue;
      }

      const auto edge1 = p01 - p00;
      const auto edge2 = p10 - p00;
      const auto delta = p11 - p00;
      const auto a = vm::dot(edge1, edge1);
      const auto b = vm::dot(edge1, edge2);
      const auto c = vm::dot(edge2, edge2);
      const auto d1 = vm::dot(delta, edge1);
      const auto d2 = vm::dot(delta, edge2);
      const auto determinant = a * c - b * b;
      if (std::abs(determinant) < vm::Cd::almost_zero())
      {
        return std::nullopt;
      }
      const auto profileCoefficient = (d1 * c - d2 * b) / determinant;
      const auto pathCoefficient = (d2 * a - d1 * b) / determinant;
      elimination.dependencies[i11] =
        AffineDependency{std::array<std::pair<size_t, double>, 3>{
          std::pair{i00, 1.0 - profileCoefficient - pathCoefficient},
          std::pair{i01, profileCoefficient},
          std::pair{i10, pathCoefficient}}};
    }
  }

  for (size_t i = profileVertexCount; i < variableCount; ++i)
  {
    if (!elimination.dependencies[i])
    {
      elimination.freeVariables[i] = elimination.freeVariableCount++;
    }
  }

  // ARAP distributes the unavoidable flattening error over the strip while the
  // elimination above keeps every coplanar Valve 220 face exactly affine. Solve in
  // world-distance units so nonuniform and negative texture scales remain intentional.
  const auto solved = solveConstrainedArap(
    std::move(localCoordinates), triangles, elimination, profileVertexCount);
  if (!solved)
  {
    return std::nullopt;
  }

  auto result = std::vector(stationCount, std::vector<vm::vec2f>(profileVertexCount));
  for (size_t stationIndex = 0; stationIndex < stationCount; ++stationIndex)
  {
    for (size_t profileIndex = 0; profileIndex < profileVertexCount; ++profileIndex)
    {
      const auto index = gridIndex(stationIndex, profileIndex);
      const auto uv = seedUv + (*solved)[index][0] * pathUvPerWorldUnit * pathDirection
                      + (*solved)[index][1] * profileUvPerWorldUnit * profileDirection;
      result[stationIndex][profileIndex] = vm::vec2f{float(uv[0]), float(uv[1])};
    }
  }
  return result;
}

template <typename Station>
ContinuousUvLayout buildContinuousUvLayout(
  const SweepSource& source,
  const SweepParameters& parameters,
  const gl::MaterialManager& materialManager,
  const Station& station)
{
  auto layout = ContinuousUvLayout{};
  layout.edgeLocations.reserve(source.faces.size());

  auto edges = std::vector<ContinuousUvEdge>{};
  for (size_t faceIndex = 0; faceIndex < source.faces.size(); ++faceIndex)
  {
    const auto& face = source.faces[faceIndex];
    const auto& vertices = face.polygon.vertices();
    layout.edgeLocations.emplace_back(vertices.size());
    for (size_t edgeIndex = 0; edgeIndex < vertices.size(); ++edgeIndex)
    {
      const auto* attributes =
        edgeIndex < face.sideAttributes.size() && face.sideAttributes[edgeIndex]
          ? &*face.sideAttributes[edgeIndex]
          : nullptr;
      edges.push_back(ContinuousUvEdge{
        faceIndex,
        edgeIndex,
        vertices[edgeIndex],
        vertices[(edgeIndex + 1u) % vertices.size()],
        attributes});
    }
  }

  for (size_t i = 0; i < edges.size(); ++i)
  {
    for (size_t j = i + 1u; j < edges.size(); ++j)
    {
      if (sameUndirectedEdge(edges[i], edges[j]))
      {
        edges[i].internal = true;
        edges[j].internal = true;
      }
    }
  }

  auto candidateIndices = std::vector<size_t>{};
  for (size_t i = 0; i < edges.size(); ++i)
  {
    if (!edges[i].internal && edges[i].attributes)
    {
      candidateIndices.push_back(i);
    }
    else if (!edges[i].internal)
    {
      layout.issues.push_back(SweepIssue{
        edges[i].sourceFaceIndex,
        0,
        0,
        "Continuous UV boundary has no adjacent face attributes"});
      return layout;
    }
  }

  auto assigned = std::set<size_t>{};
  for (const auto seedIndex : candidateIndices)
  {
    if (assigned.contains(seedIndex))
    {
      continue;
    }

    const auto& materialName = edges[seedIndex].attributes->materialName;
    auto textureSize = std::optional<vm::vec2d>{};
    if (const auto* texture = gl::getTexture(materialManager.material(materialName)))
    {
      textureSize = vm::vec2d{texture->sizef()};
    }
    auto componentEdgeIndices = std::vector<size_t>{seedIndex};
    auto componentSet = std::set<size_t>{seedIndex};
    for (size_t cursor = 0; cursor < componentEdgeIndices.size(); ++cursor)
    {
      const auto& current = edges[componentEdgeIndices[cursor]];
      for (const auto candidateIndex : candidateIndices)
      {
        const auto& candidate = edges[candidateIndex];
        if (
          componentSet.contains(candidateIndex)
          || candidate.attributes->materialName != materialName)
        {
          continue;
        }

        const auto connected =
          sourceUvsMeetAtSharedVertex(current, candidate, textureSize);
        if (connected)
        {
          componentSet.insert(candidateIndex);
          componentEdgeIndices.push_back(candidateIndex);
        }
      }
    }

    auto vertices = std::vector<vm::vec3d>{};
    auto edgeVertices = std::vector<std::array<size_t, 2>>{};
    auto adjacency = std::vector<std::vector<size_t>>{};
    for (const auto edgeIndex : componentEdgeIndices)
    {
      const auto startIndex = findOrAddVertex(vertices, edges[edgeIndex].start);
      const auto endIndex = findOrAddVertex(vertices, edges[edgeIndex].end);
      edgeVertices.push_back({startIndex, endIndex});
    }
    adjacency.resize(vertices.size());
    for (size_t i = 0; i < edgeVertices.size(); ++i)
    {
      adjacency[edgeVertices[i][0]].push_back(i);
      adjacency[edgeVertices[i][1]].push_back(i);
    }

    const auto hasBranch = std::ranges::any_of(
      adjacency, [](const auto& incident) { return incident.size() > 2u; });
    const auto endpointCount = std::ranges::count_if(
      adjacency, [](const auto& incident) { return incident.size() == 1u; });
    if (hasBranch || (endpointCount != 0u && endpointCount != 2u))
    {
      const auto& seed = edges[seedIndex];
      layout.issues.push_back(SweepIssue{
        seed.sourceFaceIndex,
        0,
        0,
        "Continuous UV boundary branches and cannot be unfolded as a strip"});
      assigned.insert(componentSet.begin(), componentSet.end());
      continue;
    }

    auto currentVertex = size_t{0};
    if (endpointCount == 2u)
    {
      currentVertex = size_t(std::distance(
        adjacency.begin(), std::ranges::find_if(adjacency, [](const auto& incident) {
          return incident.size() == 1u;
        })));
    }

    auto orderedEdgeIndices = std::vector<size_t>{};
    auto orderedVertices = std::vector<vm::vec3d>{vertices[currentVertex]};
    auto usedLocalEdges = std::set<size_t>{};
    while (orderedEdgeIndices.size() < componentEdgeIndices.size())
    {
      const auto nextIt = std::ranges::find_if(
        adjacency[currentVertex],
        [&](const auto i) { return !usedLocalEdges.contains(i); });
      if (nextIt == adjacency[currentVertex].end())
      {
        break;
      }

      const auto localEdgeIndex = *nextIt;
      usedLocalEdges.insert(localEdgeIndex);
      const auto globalEdgeIndex = componentEdgeIndices[localEdgeIndex];
      orderedEdgeIndices.push_back(globalEdgeIndex);
      const auto& endpoints = edgeVertices[localEdgeIndex];
      currentVertex = endpoints[0] == currentVertex ? endpoints[1] : endpoints[0];
      orderedVertices.push_back(vertices[currentVertex]);
    }

    if (orderedEdgeIndices.size() != componentEdgeIndices.size())
    {
      const auto& seed = edges[seedIndex];
      layout.issues.push_back(
        SweepIssue{seed.sourceFaceIndex, 0, 0, "Continuous UV boundary is disconnected"});
      assigned.insert(componentSet.begin(), componentSet.end());
      continue;
    }

    const auto& seed = edges[orderedEdgeIndices.front()];
    auto sourceProfileUvs =
      std::optional<std::vector<vm::vec2f>>{std::vector<vm::vec2f>{}};
    sourceProfileUvs->reserve(orderedVertices.size());
    auto previousUv = std::optional<vm::vec2d>{};
    for (size_t i = 0; i < orderedEdgeIndices.size(); ++i)
    {
      const auto& edge = edges[orderedEdgeIndices[i]];
      const auto startUv = sourceFaceUv(*edge.attributes, orderedVertices[i]);
      const auto endUv = sourceFaceUv(*edge.attributes, orderedVertices[i + 1u]);
      if (!startUv || !endUv)
      {
        sourceProfileUvs.reset();
        break;
      }

      auto phaseShift = vm::vec2d{};
      if (previousUv && textureSize)
      {
        for (size_t axis = 0; axis < 2u; ++axis)
        {
          phaseShift[axis] =
            std::round(((*previousUv)[axis] - (*startUv)[axis]) / (*textureSize)[axis])
            * (*textureSize)[axis];
        }
      }
      const auto unwrappedStartUv = *startUv + phaseShift;
      const auto unwrappedEndUv = *endUv + phaseShift;
      if (previousUv && !vm::is_equal(*previousUv, unwrappedStartUv, 0.001))
      {
        sourceProfileUvs.reset();
        break;
      }

      if (!previousUv)
      {
        sourceProfileUvs->emplace_back(
          float(unwrappedStartUv[0]), float(unwrappedStartUv[1]));
      }
      sourceProfileUvs->emplace_back(float(unwrappedEndUv[0]), float(unwrappedEndUv[1]));
      previousUv = unwrappedEndUv;
    }

    const auto stationUvs = buildAffineUvGrid(
      orderedVertices, sourceProfileUvs, *seed.attributes, parameters, station);
    if (!stationUvs)
    {
      layout.issues.push_back(SweepIssue{
        seed.sourceFaceIndex,
        0,
        0,
        "Continuous UV boundary could not be globally parameterized"});
      assigned.insert(componentSet.begin(), componentSet.end());
      continue;
    }

    const auto componentIndex = layout.components.size();
    for (size_t i = 0; i < orderedEdgeIndices.size(); ++i)
    {
      const auto edgeIndex = orderedEdgeIndices[i];
      const auto& edge = edges[edgeIndex];
      const auto forward =
        vm::is_equal(edge.start, orderedVertices[i], vm::Cd::almost_zero());
      layout.edgeLocations[edge.sourceFaceIndex][edge.sourceEdgeIndex] =
        ContinuousUvEdgeLocation{componentIndex, i, forward};
    }
    layout.components.push_back(ContinuousUvComponent{std::move(*stationUvs)});
    assigned.insert(componentSet.begin(), componentSet.end());
  }

  return layout;
}

void applyFaceAttributes(
  mdl::BrushFace& face,
  const SweepFaceAttributes& attributes,
  const mdl::WrapStyle wrapStyle,
  gl::MaterialManager& materialManager)
{
  face.setMaterialName(attributes.materialName);
  face.setUvAttributes(attributes.uvAttributes);
  face.setSurfaceAttributes(attributes.surfaceAttributes);

  if (attributes.uvCoordSystemSnapshot)
  {
    face.copyUvCoordSystemFromFace(
      *attributes.uvCoordSystemSnapshot,
      attributes.uvAttributes,
      attributes.sourceBoundary,
      wrapStyle);
  }

  face.setMaterial(materialManager.material(attributes.materialName));
}

std::optional<vm::vec3d> solveUvAxis(
  const std::array<vm::vec3d, 3>& points, const std::array<float, 3>& coordinates)
{
  const auto edge1 = points[1] - points[0];
  const auto edge2 = points[2] - points[0];
  const auto a = vm::dot(edge1, edge1);
  const auto b = vm::dot(edge1, edge2);
  const auto c = vm::dot(edge2, edge2);
  const auto determinant = a * c - b * b;
  if (std::abs(determinant) < vm::Cd::almost_zero())
  {
    return std::nullopt;
  }

  const auto d1 = double(coordinates[1] - coordinates[0]);
  const auto d2 = double(coordinates[2] - coordinates[0]);
  const auto s = (d1 * c - d2 * b) / determinant;
  const auto t = (d2 * a - d1 * b) / determinant;
  return s * edge1 + t * edge2;
}

bool applyContinuousFaceUvs(
  mdl::BrushFace& face,
  const std::array<vm::vec3d, 4>& points,
  const std::array<vm::vec2f, 4>& uvs,
  const mdl::UvAttributes& sourceUvAttributes)
{
  const auto faceVertices = face.vertexPositions();
  auto facePoints = std::vector<vm::vec3d>{};
  auto faceUvs = std::vector<vm::vec2f>{};
  facePoints.reserve(faceVertices.size());
  faceUvs.reserve(faceVertices.size());
  for (const auto& faceVertex : faceVertices)
  {
    const auto pointIt = std::ranges::find_if(points, [&](const auto& point) {
      return vm::is_equal(point, faceVertex, vm::Cd::almost_zero());
    });
    if (pointIt == points.end())
    {
      return false;
    }

    const auto pointIndex = size_t(std::distance(points.begin(), pointIt));
    facePoints.push_back(*pointIt);
    faceUvs.push_back(uvs[pointIndex]);
  }
  if (facePoints.size() < 3u)
  {
    return false;
  }

  auto basisIndices = std::optional<std::array<size_t, 3>>{};
  for (size_t i = 0; i + 2u < facePoints.size() && !basisIndices; ++i)
  {
    for (size_t j = i + 1u; j + 1u < facePoints.size() && !basisIndices; ++j)
    {
      for (size_t k = j + 1u; k < facePoints.size(); ++k)
      {
        const auto edge1 = facePoints[j] - facePoints[i];
        const auto edge2 = facePoints[k] - facePoints[i];
        if (vm::squared_length(vm::cross(edge1, edge2)) > 1e-12)
        {
          basisIndices = std::array{i, j, k};
          break;
        }
      }
    }
  }
  if (!basisIndices)
  {
    return false;
  }

  const auto basisPoints = std::array{
    facePoints[(*basisIndices)[0]],
    facePoints[(*basisIndices)[1]],
    facePoints[(*basisIndices)[2]]};
  const auto basisUvs = std::array{
    faceUvs[(*basisIndices)[0]],
    faceUvs[(*basisIndices)[1]],
    faceUvs[(*basisIndices)[2]]};
  const auto uAxis =
    solveUvAxis(basisPoints, {basisUvs[0].x(), basisUvs[1].x(), basisUvs[2].x()});
  const auto vAxis =
    solveUvAxis(basisPoints, {basisUvs[0].y(), basisUvs[1].y(), basisUvs[2].y()});
  if (!uAxis || !vAxis)
  {
    return false;
  }

  const auto offset = vm::vec2f{
    faceUvs[0].x() - float(vm::dot(facePoints[0], *uAxis)),
    faceUvs[0].y() - float(vm::dot(facePoints[0], *vAxis))};
  for (size_t i = 0; i < facePoints.size(); ++i)
  {
    const auto projected = vm::vec2f{
      float(vm::dot(facePoints[i], *uAxis)) + offset.x(),
      float(vm::dot(facePoints[i], *vAxis)) + offset.y()};
    if (!vm::is_equal(projected, faceUvs[i], 0.002f))
    {
      return false;
    }
  }

  auto uvAttributes = sourceUvAttributes;
  uvAttributes.offset = offset;
  uvAttributes.scale = vm::vec2f{
    usableUvScale(uvAttributes.scale.x()), usableUvScale(uvAttributes.scale.y())};
  face.setUvAttributes(uvAttributes);
  face.restoreUvCoordSystemSnapshot(mdl::UvCoordSystemSnapshot{
    *uAxis * double(uvAttributes.scale.x()), *vAxis * double(uvAttributes.scale.y())});
  return true;
}

void applySegmentAttributes(
  mdl::Brush& brush,
  const SweepFace& sourceFace,
  const size_t sourceFaceIndex,
  const std::vector<vm::vec3d>& startCap,
  const std::vector<vm::vec3d>& endCap,
  const size_t globalSegment,
  const ContinuousUvLayout* continuousUvLayout,
  gl::MaterialManager& materialManager,
  std::vector<SweepIssue>& issues,
  const size_t iteration,
  const size_t segment)
{
  for (auto& face : brush.faces())
  {
    face.setMaterial(materialManager.material(face.materialName()));
  }

  auto sidePoints = std::vector<std::vector<vm::vec3d>>{};
  sidePoints.reserve(startCap.size());
  for (size_t i = 0; i < startCap.size(); ++i)
  {
    const auto next = (i + 1u) % startCap.size();
    sidePoints.push_back({startCap[i], startCap[next], endCap[next], endCap[i]});
  }

  for (auto& face : brush.faces())
  {
    if (
      sourceFace.capAttributes
      && (faceUsesOnlyPoints(face, startCap) || faceUsesOnlyPoints(face, endCap)))
    {
      applyFaceAttributes(
        face, *sourceFace.capAttributes, mdl::WrapStyle::Rotation, materialManager);
      continue;
    }

    for (size_t i = 0; i < sidePoints.size() && i < sourceFace.sideAttributes.size(); ++i)
    {
      if (sourceFace.sideAttributes[i] && faceUsesOnlyPoints(face, sidePoints[i]))
      {
        applyFaceAttributes(
          face,
          *sourceFace.sideAttributes[i],
          mdl::WrapStyle::Projection,
          materialManager);

        if (
          continuousUvLayout && sourceFaceIndex < continuousUvLayout->edgeLocations.size()
          && i < continuousUvLayout->edgeLocations[sourceFaceIndex].size())
        {
          if (
            const auto& location = continuousUvLayout->edgeLocations[sourceFaceIndex][i])
          {
            const auto next = (i + 1u) % startCap.size();
            const auto& component =
              continuousUvLayout->components[location->componentIndex];
            const auto edgeIndex = location->edgeIndex;
            const auto& startUvs = component.stationUvs[globalSegment];
            const auto& endUvs = component.stationUvs[globalSegment + 1u];
            const auto uvs =
              location->forward
                ? std::array<
                    vm::vec2f,
                    4>{startUvs[edgeIndex], startUvs[edgeIndex + 1u], endUvs[edgeIndex + 1u], endUvs[edgeIndex]}
                : std::array<vm::vec2f, 4>{
                    startUvs[edgeIndex + 1u],
                    startUvs[edgeIndex],
                    endUvs[edgeIndex],
                    endUvs[edgeIndex + 1u]};
            if (!applyContinuousFaceUvs(
                  face,
                  {startCap[i], startCap[next], endCap[next], endCap[i]},
                  uvs,
                  sourceFace.sideAttributes[i]->uvAttributes))
            {
              issues.push_back(SweepIssue{
                sourceFaceIndex,
                iteration,
                segment,
                "Continuous UV coordinates are not affine on a generated face"});
            }
          }
        }
        break;
      }
    }
  }
}

} // namespace

std::ostream& operator<<(std::ostream& lhs, const SweepAlignment rhs)
{
  switch (rhs)
  {
  case SweepAlignment::Integer:
    lhs << "Integer";
    break;
  case SweepAlignment::Free:
    lhs << "Free";
    break;
  }

  return lhs;
}

std::ostream& operator<<(std::ostream& lhs, const SweepPathMode rhs)
{
  switch (rhs)
  {
  case SweepPathMode::Arc:
    lhs << "Arc";
    break;
  case SweepPathMode::Straight:
    lhs << "Straight";
    break;
  case SweepPathMode::SBend:
    lhs << "S-bend";
    break;
  }

  return lhs;
}

std::ostream& operator<<(std::ostream& lhs, const SweepUvMode rhs)
{
  switch (rhs)
  {
  case SweepUvMode::Preserve:
    lhs << "Preserve";
    break;
  case SweepUvMode::Continuous:
    lhs << "Continuous";
    break;
  }

  return lhs;
}

kdl_reflect_impl(SweepTransform);
kdl_reflect_impl(SweepParameters);
kdl_reflect_impl(SweepFaceAttributes);
kdl_reflect_impl(SweepFace);
kdl_reflect_impl(SweepSource);
kdl_reflect_impl(SweepIssue);

vm::vec3d SweepTransform::destinationCenter(const SweepSource& source) const
{
  return source.center + translation;
}

vm::quatd SweepTransform::effectiveRotation() const
{
  // quat::angle() runs 0..2pi, so a ring dragged 330 degrees would sweep all 330; above a
  // half-turn, use the smaller turn about the opposite axis instead
  const auto theta = rotation.angle();
  if (theta <= vm::Cd::pi() + vm::Cd::almost_zero())
  {
    return rotation;
  }

  // a full turn is the identity, and axis() is degenerate here
  const auto shortAngle = vm::Cd::two_pi() - theta;
  return shortAngle < vm::Cd::almost_zero()
           ? vm::quatd{vm::vec3d{0, 0, 1}, 0.0}
           : vm::quatd{-vm::normalize(rotation.axis()), shortAngle};
}

bool SweepTransform::isNoOp() const
{
  return vm::is_zero(translation, vm::Cd::almost_zero())
         && effectiveRotation().angle() < vm::Cd::almost_zero()
         && vm::is_equal(scale, vm::vec3d{1, 1, 1}, vm::Cd::almost_zero());
}

vm::mat4x4d stationTransform(
  const SweepSource& source,
  const SweepTransform& transform,
  const SweepParameters& parameters,
  const double t,
  const vm::quatd& rotationFull)
{
  return stationTransformWithPivot(
    source,
    transform,
    parameters,
    t,
    rotationFull,
    stationArcPivot(source, transform, parameters, rotationFull));
}

SweepResult generateSweepBrushes(
  mdl::Map& map,
  const SweepSource& source,
  const SweepTransform& transform,
  const SweepParameters& parameters)
{
  auto result = SweepResult{};
  if (parameters.segments == 0u || parameters.iterations == 0u)
  {
    return result;
  }

  if (
    parameters.uvMode == SweepUvMode::Continuous
    && !mdl::isParallelUvCoordSystem(map.worldNode().mapFormat()))
  {
    result.issues.push_back(
      SweepIssue{0, 0, 0, "Continuous UVs require a Valve-style map format"});
    return result;
  }

  const auto builder = mdl::BrushBuilder{
    map.worldNode().mapFormat(),
    map.worldBounds(),
    map.gameInfo().gameConfig.faceAttribsConfig.defaultUvAttributes,
    map.gameInfo().gameConfig.faceAttribsConfig.defaultSurfaceAttributes};

  const auto materialName = map.currentMaterialName();

  const auto snapToInteger = parameters.alignment == SweepAlignment::Integer;
  const auto N = parameters.segments;

  // precompute the transform table once since it never depends on the face or vertex
  const auto transforms = computeTransformTable(source, transform, parameters);

  // a station vertex depends only on (r, s), so adjacent segments compute their shared
  // cap vertices identically and the mesh stays watertight even when snapping
  const auto station = [&](const vm::vec3d& v, const size_t r, const size_t s) {
    const auto p = transforms[r][s] * v;
    // station 0 of the first iteration is the source face itself and must stay exact
    return snapToInteger && (s > 0 || r > 0) ? vm::round(p) : p;
  };

  auto continuousUvLayout = std::optional<ContinuousUvLayout>{};
  if (parameters.uvMode == SweepUvMode::Continuous)
  {
    continuousUvLayout =
      buildContinuousUvLayout(source, parameters, map.materialManager(), station);
    result.issues = continuousUvLayout->issues;
    if (!result.issues.empty())
    {
      return result;
    }
  }

  // each source face produces its own run of brushes, grouped under its original parent
  for (size_t sourceFaceIndex = 0; sourceFaceIndex < source.faces.size();
       ++sourceFaceIndex)
  {
    const auto& sourceFace = source.faces[sourceFaceIndex];
    // fall back to the default insertion parent if the captured parent has been deleted
    auto& parent = sourceFace.parent ? *sourceFace.parent : parentForNodes(map);

    const auto& sourceVertices = sourceFace.polygon.vertices();
    for (size_t r = 0; r < parameters.iterations; ++r)
    {
      for (size_t i = 0; i < N; ++i)
      {
        const auto startCap =
          sourceVertices
          | std::views::transform([&](const auto& v) { return station(v, r, i); })
          | kdl::ranges::to<std::vector>();
        const auto endCap =
          sourceVertices
          | std::views::transform([&](const auto& v) { return station(v, r, i + 1); })
          | kdl::ranges::to<std::vector>();
        auto points = std::vector<vm::vec3d>{};
        points.reserve(startCap.size() * 2u);
        for (size_t j = 0; j < startCap.size(); ++j)
        {
          points.push_back(startCap[j]);
          points.push_back(endCap[j]);
        }

        builder.createBrush(points, materialName) | kdl::transform([&](auto brush) {
          applySegmentAttributes(
            brush,
            sourceFace,
            sourceFaceIndex,
            startCap,
            endCap,
            r * N + i,
            continuousUvLayout ? &*continuousUvLayout : nullptr,
            map.materialManager(),
            result.issues,
            r,
            i);

          auto brushNode = std::make_unique<mdl::BrushNode>(std::move(brush));
          result.brushes[&parent].push_back(std::move(brushNode));
        }) | kdl::transform_error([&](auto e) {
          map.logger().debug() << "Sweep: could not create segment brush: " << e.msg;
          result.issues.push_back(SweepIssue{sourceFaceIndex, r, i, e.msg});
        });
      }
    }
  }

  if (continuousUvLayout && !result.issues.empty())
  {
    result.brushes.clear();
  }

  return result;
}

} // namespace tb::ui
