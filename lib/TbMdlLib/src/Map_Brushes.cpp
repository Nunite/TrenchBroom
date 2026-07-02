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
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Transaction.h"
#include "mdl/UVCoordSystem.h"
#include "mdl/UVUtils.h"
#include "mdl/UpdateBrushFaceAttributes.h"
#include "mdl/WorldNode.h"

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

bool isSameFace(const SelectedFace& lhs, const SelectedFace& rhs)
{
  return lhs.node == rhs.node && lhs.faceIndex == rhs.faceIndex;
}

BrushFace& getFace(Brush& brush, const SelectedFace& face)
{
  return brush.face(face.faceIndex);
}

bool shareEdge(const BrushFace& lhs, const BrushFace& rhs)
{
  size_t sharedVertexCount = 0u;
  const auto rhsVertexPositions = rhs.vertexPositions();

  for (const auto& lhsPosition : lhs.vertexPositions())
  {
    if (std::ranges::any_of(rhsVertexPositions, [&](const auto& rhsPosition) {
          return vm::is_equal(lhsPosition, rhsPosition, vm::Cd::almost_zero());
        }))
    {
      ++sharedVertexCount;
    }
  }

  return sharedVertexCount >= 2u;
}

void copyUVAlignment(const BrushFace& sourceFace, BrushFace& targetFace)
{
  if (auto snapshot = sourceFace.takeUVCoordSystemSnapshot())
  {
    auto attributes = targetFace.attributes();
    attributes.setOffset(sourceFace.attributes().offset());
    attributes.setScale(sourceFace.attributes().scale());
    attributes.setRotation(sourceFace.attributes().rotation());
    targetFace.setAttributes(attributes);
    targetFace.copyUVCoordSystemFromFace(
      *snapshot, sourceFace.attributes(), sourceFace.boundary(), WrapStyle::Projection);
    targetFace.resetUVCoordSystemCache();
  }
}

bool alignAndWrapSelectedFaces(
  Map& map, const std::vector<BrushFaceHandle>& faceHandles, const UvPolicy uvPolicy)
{
  auto brushes = std::unordered_map<BrushNode*, Brush>{};
  auto selectedFaces = std::vector<SelectedFace>{};
  selectedFaces.reserve(faceHandles.size());

  for (const auto& faceHandle : faceHandles)
  {
    const auto face = SelectedFace{faceHandle.node(), faceHandle.faceIndex()};
    if (std::ranges::any_of(selectedFaces, [&](const auto& selectedFace) {
          return isSameFace(selectedFace, face);
        }))
    {
      continue;
    }

    brushes.try_emplace(face.node, face.node->brush());
    selectedFaces.push_back(face);
  }

  if (selectedFaces.empty())
  {
    return true;
  }

  auto adjacentFaces = std::vector<std::vector<size_t>>(selectedFaces.size());
  for (size_t i = 0u; i < selectedFaces.size(); ++i)
  {
    const auto& lhsFace = getFace(brushes.at(selectedFaces[i].node), selectedFaces[i]);
    for (size_t j = i + 1u; j < selectedFaces.size(); ++j)
    {
      const auto& rhsFace = getFace(brushes.at(selectedFaces[j].node), selectedFaces[j]);
      if (shareEdge(lhsFace, rhsFace))
      {
        adjacentFaces[i].push_back(j);
        adjacentFaces[j].push_back(i);
      }
    }
  }

  auto visited = std::vector<bool>(selectedFaces.size(), false);
  for (size_t componentStart = 0u; componentStart < selectedFaces.size();
       ++componentStart)
  {
    if (visited[componentStart])
    {
      continue;
    }

    evaluate(
      mdl::align(
        getFace(
          brushes.at(selectedFaces[componentStart].node), selectedFaces[componentStart]),
        uvPolicy),
      getFace(
        brushes.at(selectedFaces[componentStart].node), selectedFaces[componentStart]));

    auto queue = std::queue<size_t>{};
    visited[componentStart] = true;
    queue.push(componentStart);

    while (!queue.empty())
    {
      const auto sourceFaceIndex = queue.front();
      queue.pop();

      const auto sourceFace = selectedFaces[sourceFaceIndex];
      const auto sourceFaceCopy = getFace(brushes.at(sourceFace.node), sourceFace);

      for (const auto targetFaceIndex : adjacentFaces[sourceFaceIndex])
      {
        if (visited[targetFaceIndex])
        {
          continue;
        }

        visited[targetFaceIndex] = true;
        auto& targetFace = getFace(
          brushes.at(selectedFaces[targetFaceIndex].node),
          selectedFaces[targetFaceIndex]);
        copyUVAlignment(sourceFaceCopy, targetFace);
        queue.push(targetFaceIndex);
      }
    }
  }

  auto newNodes = std::vector<std::pair<Node*, NodeContents>>{};
  newNodes.reserve(brushes.size());
  for (auto& [brushNode, brush] : brushes)
  {
    newNodes.emplace_back(brushNode, NodeContents{std::move(brush)});
  }

  return updateNodeContents(map, "Align Texture", std::move(newNodes));
}

void alignUVIndependently(Map& map, const UvPolicy uvPolicy)
{
  applyAndSwap(
    map, "Align Texture", map.selection().allBrushFaces(), [&](auto& brushFace) {
      evaluate(mdl::align(brushFace, uvPolicy), brushFace);
      return true;
    });
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
  if (map.selection().hasBrushFaces() && map.selection().allBrushFaces().size() > 1u)
  {
    alignAndWrapSelectedFaces(map, map.selection().allBrushFaces(), uvPolicy);
    return;
  }

  alignUVIndependently(map, uvPolicy);
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
