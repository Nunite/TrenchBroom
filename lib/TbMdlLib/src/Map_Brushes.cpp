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

#include "mdl/Map_Brushes.h"

#include "Logger.h"
#include "mdl/ApplyAndSwap.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/GameConfig.h"
#include "mdl/GameInfo.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/ParallelUVCoordSystem.h"
#include "mdl/Transaction.h"
#include "mdl/UVUtils.h"
#include "mdl/UpdateBrushFaceAttributes.h"
#include "mdl/WorldNode.h"

#include <unordered_map>

namespace tb::mdl
{
namespace
{

struct SelectedTriangleFace
{
  BrushNode* node;
  size_t faceIndex;
  std::vector<vm::vec3d> vertices;
};

struct UnwrapQuad
{
  size_t firstFace;
  size_t secondFace;
  std::array<vm::vec3d, 4> corners;
};

struct TriangleUV
{
  size_t faceIndex;
  std::array<vm::vec3d, 3> points;
  std::array<vm::vec2f, 3> uvs;
};

bool samePoint(const vm::vec3d& lhs, const vm::vec3d& rhs)
{
  return vm::is_equal(lhs, rhs, vm::Cd::almost_zero());
}

bool containsPoint(const std::vector<vm::vec3d>& points, const vm::vec3d& point)
{
  return std::ranges::any_of(
    points, [&](const auto& candidate) { return samePoint(candidate, point); });
}

std::vector<vm::vec3d> sharedPoints(
  const std::vector<vm::vec3d>& lhs, const std::vector<vm::vec3d>& rhs)
{
  auto result = std::vector<vm::vec3d>{};
  for (const auto& point : lhs)
  {
    if (containsPoint(rhs, point))
    {
      result.push_back(point);
    }
  }
  return result;
}

std::optional<vm::vec3d> unsharedPoint(
  const std::vector<vm::vec3d>& points, const std::vector<vm::vec3d>& shared)
{
  const auto it = std::ranges::find_if(
    points, [&](const auto& point) { return !containsPoint(shared, point); });
  if (it == std::end(points))
  {
    return std::nullopt;
  }
  return *it;
}

std::optional<UnwrapQuad> makeUnwrapQuad(
  const size_t firstFace,
  const size_t secondFace,
  const std::vector<SelectedTriangleFace>& faces)
{
  const auto shared = sharedPoints(faces[firstFace].vertices, faces[secondFace].vertices);
  if (shared.size() != 2u)
  {
    return std::nullopt;
  }

  const auto firstOuter = unsharedPoint(faces[firstFace].vertices, shared);
  const auto secondOuter = unsharedPoint(faces[secondFace].vertices, shared);
  if (!firstOuter || !secondOuter)
  {
    return std::nullopt;
  }

  return UnwrapQuad{
    firstFace, secondFace, {*firstOuter, shared[1], *secondOuter, shared[0]}};
}

std::optional<std::pair<size_t, size_t>> matchingQuadEdge(
  const UnwrapQuad& lhs, const UnwrapQuad& rhs)
{
  for (size_t i = 0; i < lhs.corners.size(); ++i)
  {
    const auto lhsNext = (i + 1u) % lhs.corners.size();
    for (size_t j = 0; j < rhs.corners.size(); ++j)
    {
      const auto rhsNext = (j + 1u) % rhs.corners.size();
      if (
        samePoint(lhs.corners[i], rhs.corners[j])
        && samePoint(lhs.corners[lhsNext], rhs.corners[rhsNext]))
      {
        return std::pair{i, j};
      }
      if (
        samePoint(lhs.corners[i], rhs.corners[rhsNext])
        && samePoint(lhs.corners[lhsNext], rhs.corners[j]))
      {
        return std::pair{i, j};
      }
    }
  }
  return std::nullopt;
}

double edgeLength(const std::array<vm::vec3d, 4>& corners, const size_t index)
{
  return vm::length(corners[(index + 1u) % corners.size()] - corners[index]);
}

std::array<vm::vec2f, 4> makeFirstQuadUVs(const UnwrapQuad& quad)
{
  const auto width = float(edgeLength(quad.corners, 0u));
  const auto height = float(edgeLength(quad.corners, 1u));
  return {
    vm::vec2f{0, 0},
    vm::vec2f{width, 0},
    vm::vec2f{width, height},
    vm::vec2f{0, height},
  };
}

std::array<vm::vec2f, 4> attachQuadUVs(
  const std::array<vm::vec2f, 4>& sourceUVs,
  const UnwrapQuad& sourceQuad,
  const UnwrapQuad& targetQuad,
  const size_t sourceEdge,
  const size_t targetEdge)
{
  auto result = std::array<vm::vec2f, 4>{};
  const auto sourceNext = (sourceEdge + 1u) % 4u;
  const auto targetNext = (targetEdge + 1u) % 4u;
  const auto targetPrev = (targetEdge + 3u) % 4u;
  const auto targetOpposite = (targetEdge + 2u) % 4u;
  const auto height = float(edgeLength(targetQuad.corners, targetNext));
  const auto sharedDelta = sourceUVs[sourceNext] - sourceUVs[sourceEdge];
  const auto normal = vm::normalize(vm::vec2f{sharedDelta.y(), -sharedDelta.x()});

  if (
    samePoint(sourceQuad.corners[sourceEdge], targetQuad.corners[targetEdge])
    && samePoint(sourceQuad.corners[sourceNext], targetQuad.corners[targetNext]))
  {
    result[targetEdge] = sourceUVs[sourceEdge];
    result[targetNext] = sourceUVs[sourceNext];
    result[targetOpposite] = sourceUVs[sourceNext] + normal * height;
    result[targetPrev] = sourceUVs[sourceEdge] + normal * height;
  }
  else
  {
    result[targetEdge] = sourceUVs[sourceNext];
    result[targetNext] = sourceUVs[sourceEdge];
    result[targetOpposite] = sourceUVs[sourceEdge] + normal * height;
    result[targetPrev] = sourceUVs[sourceNext] + normal * height;
  }
  return result;
}

std::optional<vm::vec2f> uvForPoint(
  const std::array<vm::vec3d, 4>& points,
  const std::array<vm::vec2f, 4>& uvs,
  const vm::vec3d& point)
{
  for (size_t i = 0; i < points.size(); ++i)
  {
    if (samePoint(points[i], point))
    {
      return uvs[i];
    }
  }
  return std::nullopt;
}

bool fitTriangleUV(BrushFace& face, const TriangleUV& triangleUV)
{
  const auto edgeA = triangleUV.points[1] - triangleUV.points[0];
  const auto edgeB = triangleUV.points[2] - triangleUV.points[0];
  const auto aa = vm::dot(edgeA, edgeA);
  const auto ab = vm::dot(edgeA, edgeB);
  const auto bb = vm::dot(edgeB, edgeB);
  const auto determinant = aa * bb - ab * ab;
  if (vm::is_zero(determinant, vm::Cd::almost_zero()))
  {
    return false;
  }

  const auto solveAxis = [&](const double uvA, const double uvB) {
    return ((uvA * bb - uvB * ab) * edgeA + (uvB * aa - uvA * ab) * edgeB) / determinant;
  };

  const auto uvDeltaA = triangleUV.uvs[1] - triangleUV.uvs[0];
  const auto uvDeltaB = triangleUV.uvs[2] - triangleUV.uvs[0];
  const auto uAxis = solveAxis(uvDeltaA.x(), uvDeltaB.x());
  const auto vAxis = solveAxis(uvDeltaA.y(), uvDeltaB.y());

  auto attributes = face.attributes();
  attributes.setOffset(vm::vec2f{
    float(triangleUV.uvs[0].x() - vm::dot(triangleUV.points[0], uAxis)),
    float(triangleUV.uvs[0].y() - vm::dot(triangleUV.points[0], vAxis)),
  });
  attributes.setScale(vm::vec2f{1, 1});
  attributes.setRotation(0.0f);
  face.setAttributes(attributes);

  const auto snapshot = ParallelUVCoordSystemSnapshot{uAxis, vAxis};
  face.restoreUVCoordSystemSnapshot(snapshot);
  face.resetUVCoordSystemCache();
  return true;
}

auto invertHorizontalAxis(
  const mdl::UvAxis horizontalUvAxis,
  const vm::vec3f& uAxis,
  const vm::vec3f& vAxis,
  const vm::vec3f& rightAxis)
{
  switch (horizontalUvAxis)
  {
  case mdl::UvAxis::u:
    return vm::dot(uAxis, rightAxis) >= 0.0f;
  case mdl::UvAxis::v:
    return vm::dot(vAxis, rightAxis) >= 0.0f;
    switchDefault();
  }
}

auto invertVerticalAxis(
  const mdl::UvAxis horizontalUvAxis,
  const vm::vec3f& uAxis,
  const vm::vec3f& vAxis,
  const vm::vec3f& upAxis)
{
  switch (horizontalUvAxis)
  {
  case mdl::UvAxis::u:
    return vm::dot(vAxis, upAxis) >= 0.0f;
  case mdl::UvAxis::v:
    return vm::dot(uAxis, upAxis) >= 0.0f;
    switchDefault();
  }
}

auto getDirectionAxes(
  const vm::vec3f& uAxis,
  const vm::vec3f& vAxis,
  const vm::vec3f& upAxis,
  const vm::vec3f& rightAxis)
{
  // Which UV axis corresponds is horizontal?
  const auto [horizontalUvAxis, verticalUvAxis] =
    vm::abs(vm::dot(uAxis, rightAxis)) >= vm::abs(vm::dot(vAxis, rightAxis))
      ? std::tuple{mdl::UvAxis::u, mdl::UvAxis::v}
      : std::tuple{mdl::UvAxis::v, mdl::UvAxis::u};

  const auto invertH = invertHorizontalAxis(horizontalUvAxis, uAxis, vAxis, rightAxis);
  const auto invertV = invertVerticalAxis(horizontalUvAxis, uAxis, vAxis, upAxis);

  return std::tuple{horizontalUvAxis, verticalUvAxis, invertH, invertV};
}

std::tuple<mdl::UvAxis, mdl::UvSign> convertJustifyDirection(
  const BrushFace& brushFace, const UvJustifyDirection uvJustifyDirection)
{
  const auto [cameraUp, cameraRight] = computeCameraAxesForFaceNormal(brushFace.normal());

  const auto uAxis = vm::vec3f{brushFace.uAxis()};
  const auto vAxis = vm::vec3f{brushFace.vAxis()};

  const auto [horizontalUvAxis, verticalUvAxis, invertH, invertV] =
    getDirectionAxes(uAxis, vAxis, vm::vec3f{cameraUp}, vm::vec3f{cameraRight});

  switch (uvJustifyDirection)
  {
  case UvJustifyDirection::Left:
    return std::tuple{horizontalUvAxis, invertH ? mdl::UvSign::minus : mdl::UvSign::plus};
  case UvJustifyDirection::Right:
    return std::tuple{horizontalUvAxis, invertH ? mdl::UvSign::plus : mdl::UvSign::minus};
  case UvJustifyDirection::Up:
    return std::tuple{verticalUvAxis, invertV ? mdl::UvSign::minus : mdl::UvSign::plus};
  case UvJustifyDirection::Down:
    return std::tuple{verticalUvAxis, invertV ? mdl::UvSign::plus : mdl::UvSign::minus};
    switchDefault();
  }
}

std::tuple<mdl::UvAxis, mdl::UvSign> convertFitDirection(
  const BrushFace& brushFace, const UvFitDirection fitDirection)
{
  const auto [cameraUp, cameraRight] = computeCameraAxesForFaceNormal(brushFace.normal());

  const auto uAxis = vm::vec3f{brushFace.uAxis()};
  const auto vAxis = vm::vec3f{brushFace.vAxis()};

  const auto [horizontalUvAxis, verticalUvAxis, invertH, invertV] =
    getDirectionAxes(uAxis, vAxis, vm::vec3f{cameraUp}, vm::vec3f{cameraRight});

  switch (fitDirection)
  {
  case UvFitDirection::Horizontal:
    return std::tuple{horizontalUvAxis, invertH ? mdl::UvSign::minus : mdl::UvSign::plus};
  case UvFitDirection::Vertical:
    return std::tuple{verticalUvAxis, invertV ? mdl::UvSign::minus : mdl::UvSign::plus};
    switchDefault();
  }
}

template <typename F>
void compensateOffset(
  BrushFace& brushFace, const std::optional<vm::vec3d>& vertex, const F& f)
{
  if (vertex)
  {
    const auto previousUvCoords = vm::vec2f{
      brushFace.toUVCoordSystemMatrix(
        brushFace.attributes().offset(), brushFace.attributes().scale())
      * *vertex};

    f();

    const auto newUvCoords = vm::vec2f{
      brushFace.toUVCoordSystemMatrix(
        brushFace.attributes().offset(), brushFace.attributes().scale())
      * *vertex};
    const auto delta = previousUvCoords - newUvCoords;

    evaluate(
      UpdateBrushFaceAttributes{
        .xOffset = mdl::AddValue{delta.x()},
        .yOffset = mdl::AddValue{delta.y()},
      },
      brushFace);
  }
  else
  {
    f();
  }
}

} // namespace

bool createBrush(Map& map, const std::vector<vm::vec3d>& points)
{
  const auto builder = BrushBuilder{
    map.worldNode().mapFormat(),
    map.worldBounds(),
    map.gameInfo().gameConfig.faceAttribsConfig.defaults};

  return builder.createBrush(points, map.currentMaterialName())
         | kdl::and_then([&](auto b) -> Result<void> {
             auto* brushNode = new BrushNode{std::move(b)};

             auto transaction = Transaction{map, "Create Brush"};
             deselectAll(map);
             if (addNodes(map, {{parentForNodes(map), {brushNode}}}).empty())
             {
               transaction.cancel();
               return Error{"Could not add brush to document"};
             }
             selectNodes(map, {brushNode});
             if (!transaction.commit())
             {
               return Error{"Could not add brush to document"};
             }

             return kdl::void_success;
           })
         | kdl::if_error(
           [&](auto e) { map.logger().error() << "Could not create brush: " << e.msg; })
         | kdl::is_success();
}

bool setBrushFaceAttributes(Map& map, const UpdateBrushFaceAttributes& update)
{
  return applyAndSwap(
    map, "Change Face Attributes", map.selection().allBrushFaces(), [&](auto& brushFace) {
      evaluate(update, brushFace);
      return true;
    });
}

bool copyUV(
  Map& map,
  const UVCoordSystemSnapshot& coordSystemSnapshot,
  const BrushFaceAttributes& attribs,
  const vm::plane3d& sourceFacePlane,
  const WrapStyle wrapStyle)
{
  return applyAndSwap(
    map, "Copy UV Alignment", map.selection().allBrushFaces(), [&](auto& face) {
      face.copyUVCoordSystemFromFace(
        coordSystemSnapshot, attribs, sourceFacePlane, wrapStyle);
      return true;
    });
}

bool translateUV(
  Map& map,
  const vm::vec3f& cameraUp,
  const vm::vec3f& cameraRight,
  const vm::vec2f& delta)
{
  return applyAndSwap(
    map, "Translate UV", map.selection().allBrushFaces(), [&](auto& face) {
      face.translateUV(vm::vec3d{cameraUp}, vm::vec3d{cameraRight}, delta);
      return true;
    });
}

bool rotateUV(Map& map, const float angle)
{
  return applyAndSwap(map, "Rotate UV", map.selection().allBrushFaces(), [&](auto& face) {
    face.rotateUV(angle);
    return true;
  });
}

bool shearUV(Map& map, const vm::vec2f& factors)
{
  return applyAndSwap(map, "Shear UV", map.selection().allBrushFaces(), [&](auto& face) {
    face.shearUV(factors);
    return true;
  });
}

bool flipUV(
  Map& map,
  const vm::vec3f& cameraUp,
  const vm::vec3f& cameraRight,
  const vm::direction cameraRelativeFlipDirection)
{
  const bool isHFlip =
    (cameraRelativeFlipDirection == vm::direction::left
     || cameraRelativeFlipDirection == vm::direction::right);
  return applyAndSwap(
    map,
    isHFlip ? "Flip UV Horizontally" : "Flip UV Vertically",
    map.selection().allBrushFaces(),
    [&](auto& face) {
      face.flipUV(
        vm::vec3d{cameraUp}, vm::vec3d{cameraRight}, cameraRelativeFlipDirection);
      return true;
    });
}

void alignUV(Map& map, const UvPolicy uvPolicy)
{
  applyAndSwap(
    map, "Align Texture", map.selection().allBrushFaces(), [&](auto& brushFace) {
      evaluate(mdl::align(brushFace, uvPolicy), brushFace);
      return true;
    });
}

bool unwrapUVAsQuads(Map& map)
{
  if (!isParallelUVCoordSystem(map.worldNode().mapFormat()))
  {
    return false;
  }

  auto brushes = std::unordered_map<BrushNode*, Brush>{};
  auto faces = std::vector<SelectedTriangleFace>{};
  for (const auto& handle : map.selection().allBrushFaces())
  {
    brushes.try_emplace(handle.node(), handle.node()->brush());
    const auto& face = brushes.at(handle.node()).face(handle.faceIndex());
    if (face.vertexCount() == 3u)
    {
      faces.push_back({handle.node(), handle.faceIndex(), face.vertexPositions()});
    }
  }

  if (faces.empty() || faces.size() % 2u != 0u)
  {
    return false;
  }

  auto quads = std::vector<UnwrapQuad>{};
  auto usedFaces = std::vector<bool>(faces.size(), false);
  for (size_t i = 0u; i < faces.size(); ++i)
  {
    if (usedFaces[i])
    {
      continue;
    }

    auto match = std::optional<UnwrapQuad>{};
    for (size_t j = i + 1u; j < faces.size(); ++j)
    {
      if (usedFaces[j])
      {
        continue;
      }
      if (const auto quad = makeUnwrapQuad(i, j, faces))
      {
        if (match)
        {
          return false;
        }
        match = quad;
      }
    }

    if (!match)
    {
      return false;
    }

    usedFaces[i] = true;
    usedFaces[match->secondFace] = true;
    quads.push_back(*match);
  }

  if (quads.empty())
  {
    return false;
  }

  auto quadUVs = std::vector<std::optional<std::array<vm::vec2f, 4>>>(quads.size());
  quadUVs[0] = makeFirstQuadUVs(quads[0]);
  auto changed = true;
  while (changed)
  {
    changed = false;
    for (size_t i = 0u; i < quads.size(); ++i)
    {
      if (!quadUVs[i])
      {
        continue;
      }
      for (size_t j = 0u; j < quads.size(); ++j)
      {
        if (quadUVs[j])
        {
          continue;
        }
        if (const auto edge = matchingQuadEdge(quads[i], quads[j]))
        {
          quadUVs[j] =
            attachQuadUVs(*quadUVs[i], quads[i], quads[j], edge->first, edge->second);
          changed = true;
        }
      }
    }
  }

  if (std::ranges::any_of(quadUVs, [](const auto& uv) { return !uv; }))
  {
    return false;
  }

  auto triangleUVs = std::vector<TriangleUV>{};
  triangleUVs.reserve(faces.size());
  for (size_t quadIndex = 0u; quadIndex < quads.size(); ++quadIndex)
  {
    const auto& quad = quads[quadIndex];
    const auto& uvs = *quadUVs[quadIndex];
    for (const auto faceIndex : {quad.firstFace, quad.secondFace})
    {
      auto triangleUV = TriangleUV{
        faceIndex,
        {faces[faceIndex].vertices[0],
         faces[faceIndex].vertices[1],
         faces[faceIndex].vertices[2]},
        {},
      };
      for (size_t i = 0u; i < triangleUV.points.size(); ++i)
      {
        const auto uv = uvForPoint(quad.corners, uvs, triangleUV.points[i]);
        if (!uv)
        {
          return false;
        }
        triangleUV.uvs[i] = *uv;
      }
      triangleUVs.push_back(triangleUV);
    }
  }

  for (const auto& triangleUV : triangleUVs)
  {
    auto& selectedFace = faces[triangleUV.faceIndex];
    auto& face = brushes.at(selectedFace.node).face(selectedFace.faceIndex);
    if (!fitTriangleUV(face, triangleUV))
    {
      return false;
    }
  }

  auto newNodes = std::vector<std::pair<Node*, NodeContents>>{};
  newNodes.reserve(brushes.size());
  for (auto& [brushNode, brush] : brushes)
  {
    newNodes.emplace_back(brushNode, NodeContents{std::move(brush)});
  }
  return updateNodeContents(map, "Unwrap Texture as Quads", std::move(newNodes));
}

void justifyUV(
  Map& map, const UvJustifyDirection uvJustifyDirection, const UvPolicy uvPolicy)
{
  applyAndSwap(
    map, "Justify Texture", map.selection().allBrushFaces(), [&](auto& brushFace) {
      const auto [uvAxis, uvSign] =
        convertJustifyDirection(brushFace, uvJustifyDirection);

      evaluate(mdl::justify(brushFace, uvAxis, uvSign, uvPolicy), brushFace);
      return true;
    });
}

void fitUV(Map& map, const UvFitDirection uvFitDirection, const UvPolicy uvPolicy)
{
  applyAndSwap(map, "Fit Texture", map.selection().allBrushFaces(), [&](auto& brushFace) {
    const auto [uvAxis, uvSign] = convertFitDirection(brushFace, uvFitDirection);

    const auto invariantVertex = anchorVertex(brushFace, uvAxis, uvSign);
    compensateOffset(brushFace, invariantVertex, [&] {
      evaluate(mdl::fit(brushFace, uvAxis, uvPolicy), brushFace);
    });
    return true;
  });
}

void autoFitUV(Map& map)
{
  applyAndSwap(
    map, "Auto Fit Texture", map.selection().allBrushFaces(), [&](auto& brushFace) {
      evaluate(mdl::align(brushFace, UvPolicy::best), brushFace);

      evaluate(
        mdl::justify(brushFace, UvAxis::u, UvSign::plus, UvPolicy::best), brushFace);
      evaluate(
        mdl::justify(brushFace, UvAxis::v, UvSign::plus, UvPolicy::best), brushFace);

      const auto invariantUVertex = anchorVertex(brushFace, UvAxis::u, UvSign::plus);
      compensateOffset(brushFace, invariantUVertex, [&] {
        evaluate(mdl::fit(brushFace, UvAxis::u, UvPolicy::best), brushFace);
      });

      const auto invariantVVertex = anchorVertex(brushFace, UvAxis::v, UvSign::plus);
      compensateOffset(brushFace, invariantVVertex, [&] {
        evaluate(mdl::fit(brushFace, UvAxis::v, UvPolicy::best), brushFace);
      });

      return true;
    });
}

} // namespace tb::mdl
