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
#include "mdl/UVCoordSystem.h"
#include "mdl/UVUtils.h"
#include "mdl/UpdateBrushFaceAttributes.h"
#include "mdl/WorldNode.h"

#include <algorithm>
#include <numeric>
#include <optional>
#include <queue>
#include <unordered_map>

namespace tb::mdl
{
namespace
{

struct SelectedFace
{
  BrushNode* node = nullptr;
  size_t faceIndex = 0u;
};

struct FaceEdge
{
  vm::vec3d first;
  vm::vec3d second;
};

struct FaceComponentPath
{
  std::vector<size_t> component;
  std::vector<size_t> path;
};

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

bool sameFace(const SelectedFace& lhs, const SelectedFace& rhs)
{
  return lhs.node == rhs.node && lhs.faceIndex == rhs.faceIndex;
}

BrushFace& faceAt(Brush& brush, const SelectedFace& face)
{
  return brush.face(face.faceIndex);
}

bool samePoint(const vm::vec3d& lhs, const vm::vec3d& rhs)
{
  return vm::is_equal(lhs, rhs, vm::Cd::almost_zero());
}

bool edgeContainsPoint(const FaceEdge& edge, const vm::vec3d& point)
{
  return samePoint(edge.first, point) || samePoint(edge.second, point);
}

FaceEdge reverseEdge(const FaceEdge& edge)
{
  return {edge.second, edge.first};
}

std::optional<FaceEdge> sharedEdge(const BrushFace& lhs, const BrushFace& rhs)
{
  auto sharedVertices = std::vector<vm::vec3d>{};
  const auto rhsVertices = rhs.vertexPositions();
  for (const auto& lhsVertex : lhs.vertexPositions())
  {
    if (std::ranges::any_of(rhsVertices, [&](const auto& rhsVertex) {
          return vm::is_equal(lhsVertex, rhsVertex, vm::Cd::almost_zero());
        }))
    {
      sharedVertices.push_back(lhsVertex);
    }
  }
  if (sharedVertices.size() < 2u)
  {
    return std::nullopt;
  }
  return FaceEdge{sharedVertices[0], sharedVertices[1]};
}

bool shareEdge(const BrushFace& lhs, const BrushFace& rhs)
{
  return sharedEdge(lhs, rhs).has_value();
}

std::optional<FaceEdge> oppositeEdge(const BrushFace& face, const FaceEdge& edge)
{
  const auto vertices = face.vertexPositions();
  auto result = std::optional<FaceEdge>{};
  auto bestDot = -1.0;
  const auto edgeDirection = vm::normalize(edge.second - edge.first);

  for (size_t i = 0u; i < vertices.size(); ++i)
  {
    const auto candidate = FaceEdge{vertices[i], vertices[(i + 1u) % vertices.size()]};
    if (
      edgeContainsPoint(edge, candidate.first)
      || edgeContainsPoint(edge, candidate.second))
    {
      continue;
    }

    const auto candidateVector = candidate.second - candidate.first;
    if (vm::is_zero(candidateVector, vm::Cd::almost_zero()))
    {
      continue;
    }

    const auto candidateDot =
      vm::abs(vm::dot(vm::normalize(candidateVector), edgeDirection));
    if (candidateDot > bestDot)
    {
      bestDot = candidateDot;
      result = candidate;
    }
  }
  return result;
}

FaceEdge orientEdgeForPairing(const FaceEdge& edge, const FaceEdge& reference)
{
  const auto sameDirectionDistance = vm::squared_length(edge.first - reference.first)
                                     + vm::squared_length(edge.second - reference.second);
  const auto reversedDistance = vm::squared_length(edge.second - reference.first)
                                + vm::squared_length(edge.first - reference.second);
  return sameDirectionDistance <= reversedDistance ? edge : reverseEdge(edge);
}

bool alignUnwrappedFace(
  BrushFace& face,
  FaceEdge previousEdge,
  FaceEdge nextEdge,
  const double startU,
  double& endU)
{
  const auto travelVector = nextEdge.first - previousEdge.first;
  const auto widthVector = previousEdge.second - previousEdge.first;
  if (
    vm::is_zero(travelVector, vm::Cd::almost_zero())
    || vm::is_zero(widthVector, vm::Cd::almost_zero()))
  {
    return false;
  }

  const auto travelLength = (vm::length(nextEdge.first - previousEdge.first)
                             + vm::length(nextEdge.second - previousEdge.second))
                            / 2.0;
  const auto widthLength = vm::length(widthVector);

  const auto aa = vm::dot(travelVector, travelVector);
  const auto ab = vm::dot(travelVector, widthVector);
  const auto bb = vm::dot(widthVector, widthVector);
  const auto determinant = aa * bb - ab * ab;
  if (vm::is_zero(determinant, vm::Cd::almost_zero()))
  {
    return false;
  }

  auto uAxis = travelLength * (bb * travelVector - ab * widthVector) / determinant;
  auto vAxis = widthLength * (aa * widthVector - ab * travelVector) / determinant;

  if (vm::dot(vm::cross(uAxis, vAxis), face.normal()) < 0.0)
  {
    previousEdge = reverseEdge(previousEdge);
    nextEdge = reverseEdge(nextEdge);
    vAxis = -vAxis;
  }

  auto attributes = face.attributes();
  attributes.setOffset(vm::vec2f{
    float(startU - vm::dot(previousEdge.first, uAxis)),
    -float(vm::dot(previousEdge.first, vAxis)),
  });
  attributes.setScale(vm::vec2f{1, 1});
  attributes.setRotation(0.0f);
  face.setAttributes(attributes);

  const auto snapshot = ParallelUVCoordSystemSnapshot{uAxis, vAxis};
  face.restoreUVCoordSystemSnapshot(snapshot);
  face.resetUVCoordSystemCache();

  endU = startU + travelLength;
  return true;
}

bool alignUnwrappedFacePath(
  const std::vector<size_t>& order,
  std::unordered_map<BrushNode*, Brush>& brushes,
  const std::vector<SelectedFace>& faces)
{
  if (order.size() < 2u)
  {
    return false;
  }

  auto previousNextEdge = std::optional<FaceEdge>{};
  auto startU = 0.0;
  for (size_t i = 0u; i < order.size(); ++i)
  {
    auto& face = faceAt(brushes.at(faces[order[i]].node), faces[order[i]]);

    auto previousEdge = std::optional<FaceEdge>{};
    auto nextEdge = std::optional<FaceEdge>{};
    if (i == 0u)
    {
      const auto& followingFace =
        faceAt(brushes.at(faces[order[i + 1u]].node), faces[order[i + 1u]]);
      nextEdge = sharedEdge(face, followingFace);
      previousEdge = nextEdge ? oppositeEdge(face, *nextEdge) : std::nullopt;
      if (previousEdge && nextEdge)
      {
        nextEdge = orientEdgeForPairing(*nextEdge, *previousEdge);
      }
    }
    else
    {
      previousEdge = previousNextEdge;
      if (i + 1u < order.size())
      {
        const auto& followingFace =
          faceAt(brushes.at(faces[order[i + 1u]].node), faces[order[i + 1u]]);
        nextEdge = sharedEdge(face, followingFace);
      }
      else if (previousEdge)
      {
        nextEdge = oppositeEdge(face, *previousEdge);
      }
      if (previousEdge && nextEdge)
      {
        nextEdge = orientEdgeForPairing(*nextEdge, *previousEdge);
      }
    }

    if (!previousEdge || !nextEdge)
    {
      return false;
    }

    auto endU = startU;
    if (!alignUnwrappedFace(face, *previousEdge, *nextEdge, startU, endU))
    {
      return false;
    }

    startU = endU;
    previousNextEdge = *nextEdge;
  }

  return true;
}

void alignFacesIndividually(
  const std::vector<size_t>& order,
  std::unordered_map<BrushNode*, Brush>& brushes,
  const std::vector<SelectedFace>& faces,
  const UvPolicy uvPolicy)
{
  for (const auto index : order)
  {
    evaluate(
      mdl::align(faceAt(brushes.at(faces[index].node), faces[index]), uvPolicy),
      faceAt(brushes.at(faces[index].node), faces[index]));
  }
}

FaceComponentPath faceComponentPath(
  const size_t start,
  const std::vector<std::vector<size_t>>& adjacentFaces,
  std::vector<bool>& visited)
{
  auto component = std::vector<size_t>{};
  auto queue = std::queue<size_t>{};
  auto inComponent = std::vector<bool>(adjacentFaces.size(), false);

  visited[start] = true;
  queue.push(start);
  while (!queue.empty())
  {
    const auto index = queue.front();
    queue.pop();

    component.push_back(index);
    inComponent[index] = true;
    for (const auto adjacentIndex : adjacentFaces[index])
    {
      if (!visited[adjacentIndex])
      {
        visited[adjacentIndex] = true;
        queue.push(adjacentIndex);
      }
    }
  }

  if (component.size() <= 1u)
  {
    return {component, component};
  }

  auto endpoint = std::optional<size_t>{};
  size_t endpointCount = 0u;
  for (const auto index : component)
  {
    const auto degree = std::ranges::count_if(
      adjacentFaces[index], [&](const auto i) { return inComponent[i]; });
    if (degree > 2)
    {
      return {component, {}};
    }
    if (degree == 1)
    {
      ++endpointCount;
      endpoint = endpoint.value_or(index);
    }
  }
  if (endpointCount != 2u || !endpoint)
  {
    return {component, {}};
  }

  auto result = std::vector<size_t>{};
  auto pathVisited = std::vector<bool>(adjacentFaces.size(), false);
  auto previous = std::optional<size_t>{};
  auto current = *endpoint;
  while (true)
  {
    result.push_back(current);
    pathVisited[current] = true;

    auto next = std::optional<size_t>{};
    for (const auto adjacentIndex : adjacentFaces[current])
    {
      if (
        inComponent[adjacentIndex] && adjacentIndex != previous
        && !pathVisited[adjacentIndex])
      {
        next = adjacentIndex;
        break;
      }
    }

    if (!next)
    {
      break;
    }
    previous = current;
    current = *next;
  }

  return {
    component,
    result.size() == component.size() ? result : std::vector<size_t>{},
  };
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

bool alignUVContinuously(Map& map, const UvPolicy uvPolicy)
{
  const auto mapFormat = map.worldNode().mapFormat();
  auto brushes = std::unordered_map<BrushNode*, Brush>{};
  auto faces = std::vector<SelectedFace>{};
  for (const auto& handle : map.selection().allBrushFaces())
  {
    auto face = SelectedFace{handle.node(), handle.faceIndex()};
    if (std::ranges::any_of(
          faces, [&](const auto& existing) { return sameFace(existing, face); }))
    {
      continue;
    }

    brushes.try_emplace(face.node, face.node->brush());
    faces.push_back(face);
  }

  if (faces.empty())
  {
    return true;
  }

  auto adjacentFaces = std::vector<std::vector<size_t>>(faces.size());
  for (size_t i = 0u; i < faces.size(); ++i)
  {
    const auto& lhs = faceAt(brushes.at(faces[i].node), faces[i]);
    for (size_t j = i + 1u; j < faces.size(); ++j)
    {
      const auto& rhs = faceAt(brushes.at(faces[j].node), faces[j]);
      if (shareEdge(lhs, rhs))
      {
        adjacentFaces[i].push_back(j);
        adjacentFaces[j].push_back(i);
      }
    }
  }

  if (!isParallelUVCoordSystem(mapFormat))
  {
    auto allFaces = std::vector<size_t>(faces.size());
    std::iota(allFaces.begin(), allFaces.end(), 0u);
    alignFacesIndividually(allFaces, brushes, faces, uvPolicy);
  }
  else
  {
    auto visited = std::vector<bool>(faces.size(), false);
    for (size_t start = 0u; start < faces.size(); ++start)
    {
      if (visited[start])
      {
        continue;
      }

      const auto componentPath = faceComponentPath(start, adjacentFaces, visited);
      if (
        componentPath.path.size() < 2u
        || !alignUnwrappedFacePath(componentPath.path, brushes, faces))
      {
        alignFacesIndividually(componentPath.component, brushes, faces, uvPolicy);
      }
    }
  }

  auto newNodes = std::vector<std::pair<Node*, NodeContents>>{};
  newNodes.reserve(brushes.size());
  for (auto& [brushNode, brush] : brushes)
  {
    newNodes.emplace_back(brushNode, NodeContents{std::move(brush)});
  }
  return updateNodeContents(map, "Continuously Align Texture", std::move(newNodes));
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
