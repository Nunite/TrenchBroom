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

#include "base/Logger.h"
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
#include "mdl/Transaction.h"
#include "mdl/UpdateBrushFaceAttributes.h"
#include "mdl/UvAlignment.h"
#include "mdl/UvUtils.h"
#include "mdl/WorldNode.h"

#include "vm/scalar.h"

#include <array>
#include <cmath>
#include <optional>

namespace tb::mdl
{
namespace
{

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
      brushFace.toUvCoordSystemMatrix(
        brushFace.uvAttributes().offset, brushFace.uvAttributes().scale)
      * *vertex};

    f();

    const auto newUvCoords = vm::vec2f{
      brushFace.toUvCoordSystemMatrix(
        brushFace.uvAttributes().offset, brushFace.uvAttributes().scale)
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

std::optional<vm::vec3d> solveAxis(
  const std::array<vm::vec3d, 3>& points, const std::array<float, 3>& coords)
{
  const auto edge1 = points[1] - points[0];
  const auto edge2 = points[2] - points[0];
  const auto a = vm::dot(edge1, edge1);
  const auto b = vm::dot(edge1, edge2);
  const auto c = vm::dot(edge2, edge2);
  const auto determinant = (a * c) - (b * b);
  if (vm::is_zero(determinant, vm::Cd::almost_zero()))
  {
    return std::nullopt;
  }

  const auto d1 = double(coords[1] - coords[0]);
  const auto d2 = double(coords[2] - coords[0]);
  const auto s = ((d1 * c) - (d2 * b)) / determinant;
  const auto t = ((d2 * a) - (d1 * b)) / determinant;
  return (s * edge1) + (t * edge2);
}

struct FaceUVProjection
{
  vm::vec3d uAxis;
  vm::vec3d vAxis;
  vm::vec2f offset;
};

std::optional<FaceUVProjection> solveFaceUVProjection(
  const std::vector<vm::vec3d>& points, const std::vector<vm::vec2f>& uvs)
{
  if (points.size() != uvs.size() || points.size() < 3u)
  {
    return std::nullopt;
  }

  auto basis = std::optional<std::array<size_t, 3>>{};
  for (size_t i = 1; i + 1u < points.size() && !basis; ++i)
  {
    for (size_t j = i + 1u; j < points.size() && !basis; ++j)
    {
      const auto edge1 = points[i] - points[0];
      const auto edge2 = points[j] - points[0];
      if (!vm::is_zero(
            vm::dot(vm::cross(edge1, edge2), vm::cross(edge1, edge2)),
            vm::Cd::almost_zero()))
      {
        basis = std::array<size_t, 3>{0u, i, j};
      }
    }
  }
  if (!basis)
  {
    return std::nullopt;
  }

  const auto basisPoints = std::array<vm::vec3d, 3>{
    points[(*basis)[0]],
    points[(*basis)[1]],
    points[(*basis)[2]],
  };
  const auto uCoords = std::array<float, 3>{
    uvs[(*basis)[0]].x(),
    uvs[(*basis)[1]].x(),
    uvs[(*basis)[2]].x(),
  };
  const auto vCoords = std::array<float, 3>{
    uvs[(*basis)[0]].y(),
    uvs[(*basis)[1]].y(),
    uvs[(*basis)[2]].y(),
  };
  const auto uAxis = solveAxis(basisPoints, uCoords);
  const auto vAxis = solveAxis(basisPoints, vCoords);
  if (!uAxis || !vAxis)
  {
    return std::nullopt;
  }

  const auto offset = vm::vec2f{
    uvs[0].x() - float(vm::dot(points[0], *uAxis)),
    uvs[0].y() - float(vm::dot(points[0], *vAxis)),
  };

  for (size_t i = 0; i < points.size(); ++i)
  {
    const auto projectedUV = vm::vec2f{
      float(vm::dot(points[i], *uAxis)) + offset.x(),
      float(vm::dot(points[i], *vAxis)) + offset.y(),
    };
    if (
      std::abs(projectedUV.x() - uvs[i].x()) > 0.001f
      || std::abs(projectedUV.y() - uvs[i].y()) > 0.001f)
    {
      return std::nullopt;
    }
  }

  return FaceUVProjection{*uAxis, *vAxis, offset};
}

bool applyFaceUV(BrushFace& face, const FaceUVUpdate& update)
{
  if (face.vertexCount() != update.points.size())
  {
    return false;
  }

  const auto projection = solveFaceUVProjection(update.points, update.uvs);
  if (!projection)
  {
    return false;
  }

  face.setUvAttributes(UvAttributes{
    .offset = projection->offset,
    .scale = vm::vec2f{1, 1},
    .rotation = 0.0f,
  });
  face.restoreUvCoordSystemSnapshot(
    UvCoordSystemSnapshot{projection->uAxis, projection->vAxis});
  return true;
}

bool applyTriangleUV(BrushFace& face, const TriangleUVUpdate& update)
{
  if (face.vertexCount() != 3u)
  {
    return false;
  }

  return applyFaceUV(
    face,
    FaceUVUpdate{
      update.face,
      {update.points[0], update.points[1], update.points[2]},
      {update.uvs[0], update.uvs[1], update.uvs[2]},
    });
}

} // namespace

bool createBrush(Map& map, const std::vector<vm::vec3d>& points)
{
  const auto builder = BrushBuilder{
    map.worldNode().mapFormat(),
    map.worldBounds(),
    map.gameInfo().gameConfig.faceAttribsConfig.defaultUvAttributes,
    map.gameInfo().gameConfig.faceAttribsConfig.defaultSurfaceAttributes};

  return builder.createBrush(points, map.currentMaterialName())
         | kdl::and_then([&](auto b) -> Result<void> {
             auto* brushNode = new BrushNode{std::move(b)};

             auto transaction = Transaction{map, "Create Brush"};
             deselectAll(map);
             if (addNodes(map, {{&parentForNodes(map), {brushNode}}}).empty())
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

bool setTriangleUVs(Map& map, const std::vector<TriangleUVUpdate>& updates)
{
  if (!isParallelUvCoordSystem(map.worldNode().mapFormat()))
  {
    return false;
  }

  auto faceHandles = std::vector<BrushFaceHandle>{};
  faceHandles.reserve(updates.size());
  for (const auto& update : updates)
  {
    faceHandles.push_back(update.face);
  }

  auto updateIndex = size_t{0};
  return applyAndSwap(map, "Set Triangle UVs", faceHandles, [&](auto& face) {
    return applyTriangleUV(face, updates[updateIndex++]);
  });
}

bool setFaceUVs(Map& map, const std::vector<FaceUVUpdate>& updates)
{
  if (!isParallelUvCoordSystem(map.worldNode().mapFormat()))
  {
    return false;
  }

  auto faceHandles = std::vector<BrushFaceHandle>{};
  faceHandles.reserve(updates.size());
  for (const auto& update : updates)
  {
    faceHandles.push_back(update.face);
  }

  auto updateIndex = size_t{0};
  return applyAndSwap(map, "Set Face UVs", faceHandles, [&](auto& face) {
    const auto& update = updates[updateIndex++];
    return applyFaceUV(face, update);
  });
}

bool copyUv(
  Map& map,
  const UvCoordSystemSnapshot& coordSystemSnapshot,
  const UvAttributes& uvAttributes,
  const vm::plane3d& sourceFacePlane,
  const WrapStyle wrapStyle)
{
  return applyAndSwap(
    map, "Copy UV Alignment", map.selection().allBrushFaces(), [&](auto& face) {
      face.copyUvCoordSystemFromFace(
        coordSystemSnapshot, uvAttributes, sourceFacePlane, wrapStyle);
      return true;
    });
}

bool translateUv(
  Map& map,
  const vm::vec3f& cameraUp,
  const vm::vec3f& cameraRight,
  const vm::vec2f& delta)
{
  return applyAndSwap(
    map, "Translate UV", map.selection().allBrushFaces(), [&](auto& face) {
      face.translateUv(vm::vec3d{cameraUp}, vm::vec3d{cameraRight}, delta);
      return true;
    });
}

bool rotateUv(Map& map, const float angle)
{
  return applyAndSwap(map, "Rotate UV", map.selection().allBrushFaces(), [&](auto& face) {
    face.rotateUv(angle);
    return true;
  });
}

bool shearUv(Map& map, const vm::vec2f& factors)
{
  return applyAndSwap(map, "Shear UV", map.selection().allBrushFaces(), [&](auto& face) {
    face.shearUv(factors);
    return true;
  });
}

bool flipUv(
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
      face.flipUv(
        vm::vec3d{cameraUp}, vm::vec3d{cameraRight}, cameraRelativeFlipDirection);
      return true;
    });
}

void alignUv(Map& map, const UvPolicy uvPolicy)
{
  applyAndSwap(
    map, "Align Texture", map.selection().allBrushFaces(), [&](auto& brushFace) {
      evaluate(mdl::align(brushFace, uvPolicy), brushFace);
      return true;
    });
}

void justifyUv(
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

void fitUv(
  Map& map,
  const UvFitDirection uvFitDirection,
  const UvPolicy uvPolicy,
  const UvFitMode uvFitMode)
{
  applyAndSwap(map, "Fit Texture", map.selection().allBrushFaces(), [&](auto& brushFace) {
    const auto [uvAxis, uvSign] = convertFitDirection(brushFace, uvFitDirection);

    const auto invariantVertex = anchorVertex(brushFace, uvAxis, uvSign);
    compensateOffset(brushFace, invariantVertex, [&] {
      evaluate(mdl::fit(brushFace, uvAxis, uvPolicy, uvFitMode), brushFace);
    });
    return true;
  });
}

void autoFitUv(Map& map)
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
        evaluate(
          mdl::fit(brushFace, UvAxis::u, UvPolicy::best, UvFitMode::fitToFace),
          brushFace);
      });

      const auto invariantVVertex = anchorVertex(brushFace, UvAxis::v, UvSign::plus);
      compensateOffset(brushFace, invariantVVertex, [&] {
        evaluate(
          mdl::fit(brushFace, UvAxis::v, UvPolicy::best, UvFitMode::fitToFace),
          brushFace);
      });

      return true;
    });
}

} // namespace tb::mdl
