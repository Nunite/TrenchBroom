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
#include "mdl/Game.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Transaction.h"
#include "mdl/UpdateBrushFaceAttributes.h"
#include "mdl/WorldNode.h"

#include "vm/bbox.h"

namespace tb::mdl
{

namespace
{

vm::bbox2f computeBoundsInUVCoords(const BrushFace& face)
{
  const auto toUV = face.toUVCoordSystemMatrix(
    face.attributes().offset(), face.attributes().scale(), true);

  const auto vertices = face.vertices();
  auto it = std::begin(vertices);
  auto end = std::end(vertices);

  auto bounds = vm::bbox2f{};
  const auto firstUV = vm::vec2f{toUV * (*it++)->position()};
  bounds.min = bounds.max = firstUV;

  while (it != end)
  {
    bounds = merge(bounds, vm::vec2f{toUV * (*it++)->position()});
  }

  return bounds;
}

vm::bbox2f computeBoundsInRawUVCoords(const BrushFace& face)
{
  const auto toRaw = face.toUVCoordSystemMatrix(vm::vec2f{0, 0}, vm::vec2f{1, 1}, true);

  const auto vertices = face.vertices();
  auto it = std::begin(vertices);
  auto end = std::end(vertices);

  auto bounds = vm::bbox2f{};
  const auto firstUV = vm::vec2f{toRaw * (*it++)->position()};
  bounds.min = bounds.max = firstUV;

  while (it != end)
  {
    bounds = merge(bounds, vm::vec2f{toRaw * (*it++)->position()});
  }

  return bounds;
}

} // namespace

bool createBrush(Map& map, const std::vector<vm::vec3d>& points)
{
  const auto builder = BrushBuilder{
    map.world()->mapFormat(),
    map.worldBounds(),
    map.game()->config().faceAttribsConfig.defaults};

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
    map, "Copy UV Alignment", map.selection().brushFaces, [&](auto& face) {
      face.copyUVCoordSystemFromFace(
        coordSystemSnapshot, attribs, sourceFacePlane, wrapStyle);
      return true;
    });
}

bool alignUV(Map& map, const UVAlign align)
{
  return applyAndSwap(
    map, "Align UV", map.selection().allBrushFaces(), [&](auto& face) {
      const auto bounds = computeBoundsInUVCoords(face);
      const auto textureSize = face.textureSize();

      auto offset = face.attributes().offset();
      switch (align)
      {
      case UVAlign::Left:
        offset[0] += -bounds.min.x();
        break;
      case UVAlign::Right:
        offset[0] += textureSize.x() - bounds.max.x();
        break;
      case UVAlign::Bottom:
        offset[1] += -bounds.min.y();
        break;
      case UVAlign::Top:
        offset[1] += textureSize.y() - bounds.max.y();
        break;
      case UVAlign::Center:
        offset = offset + (textureSize / 2.0f - bounds.center());
        break;
      }

      offset = vm::correct(face.attributes().modOffset(offset, textureSize), 4, 0.0f);
      auto attributes = face.attributes();
      attributes.setOffset(offset);
      face.setAttributes(attributes);
      return true;
    });
}

bool fitUV(Map& map)
{
  return applyAndSwap(map, "Fit UV", map.selection().allBrushFaces(), [&](auto& face) {
    const auto rawBounds = computeBoundsInRawUVCoords(face);
    const auto rawSize = rawBounds.size();
    const auto textureSize = face.textureSize();

    auto scale = face.attributes().scale();

    if (rawSize.x() != 0.0f)
    {
      const auto scaleX = rawSize.x() / textureSize.x();
      scale[0] = (scale[0] < 0.0f ? -scaleX : scaleX);
    }
    if (rawSize.y() != 0.0f)
    {
      const auto scaleY = rawSize.y() / textureSize.y();
      scale[1] = (scale[1] < 0.0f ? -scaleY : scaleY);
    }

    if (scale.x() == 0.0f || scale.y() == 0.0f)
    {
      return true;
    }

    auto offset = vm::vec2f{};
    offset[0] = scale.x() >= 0.0f ? -rawBounds.min.x() / scale.x()
                                  : -rawBounds.max.x() / scale.x();
    offset[1] = scale.y() >= 0.0f ? -rawBounds.min.y() / scale.y()
                                  : -rawBounds.max.y() / scale.y();

    offset = vm::correct(face.attributes().modOffset(offset, textureSize), 4, 0.0f);
    scale = vm::correct(scale, 4, 1.0f);

    auto attributes = face.attributes();
    attributes.setScale(scale);
    attributes.setOffset(offset);
    face.setAttributes(attributes);
    return true;
  });
}

bool translateUV(
  Map& map,
  const vm::vec3f& cameraUp,
  const vm::vec3f& cameraRight,
  const vm::vec2f& delta)
{
  return applyAndSwap(map, "Translate UV", map.selection().brushFaces, [&](auto& face) {
    face.moveUV(vm::vec3d{cameraUp}, vm::vec3d{cameraRight}, delta);
    return true;
  });
}

bool rotateUV(Map& map, const float angle)
{
  return applyAndSwap(map, "Rotate UV", map.selection().brushFaces, [&](auto& face) {
    face.rotateUV(angle);
    return true;
  });
}

bool shearUV(Map& map, const vm::vec2f& factors)
{
  return applyAndSwap(map, "Shear UV", map.selection().brushFaces, [&](auto& face) {
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
    map.selection().brushFaces,
    [&](auto& face) {
      face.flipUV(
        vm::vec3d{cameraUp}, vm::vec3d{cameraRight}, cameraRelativeFlipDirection);
      return true;
    });
}

} // namespace tb::mdl
