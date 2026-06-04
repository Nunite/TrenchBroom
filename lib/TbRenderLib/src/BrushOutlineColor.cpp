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

#include "render/BrushOutlineColor.h"

#include "mdl/Brush.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/EntityProperties.h"

#include <cmath>
#include <functional>

namespace tb::render
{

const std::vector<Color>& readable2DBrushOutlinePalette()
{
  static const auto palette = std::vector<Color>{
    RgbaF{0.44f, 0.78f, 0.86f, 0.72f},
    RgbaF{0.58f, 0.72f, 0.98f, 0.72f},
    RgbaF{0.56f, 0.82f, 0.56f, 0.72f},
    RgbaF{0.86f, 0.76f, 0.43f, 0.72f},
    RgbaF{0.78f, 0.62f, 0.92f, 0.72f},
    RgbaF{0.90f, 0.58f, 0.55f, 0.72f},
    RgbaF{0.52f, 0.84f, 0.72f, 0.72f},
    RgbaF{0.80f, 0.80f, 0.64f, 0.72f},
  };
  return palette;
}

namespace
{

int64_t quantize(const double value)
{
  return static_cast<int64_t>(std::llround(value * 1000.0));
}

void combineHash(uint64_t& hash, const int64_t value)
{
  hash ^= static_cast<uint64_t>(value);
  hash *= uint64_t{1099511628211ull};
}

void combineBounds(uint64_t& hash, const vm::bbox3d& bounds)
{
  combineHash(hash, quantize(bounds.min.x()));
  combineHash(hash, quantize(bounds.min.y()));
  combineHash(hash, quantize(bounds.min.z()));
  combineHash(hash, quantize(bounds.max.x()));
  combineHash(hash, quantize(bounds.max.y()));
  combineHash(hash, quantize(bounds.max.z()));
}

} // namespace

size_t readable2DBrushOutlineColorIndex(const BrushOutlineColorState& state)
{
  auto hash = uint64_t{1469598103934665603ull};

  if (state.brushEntityBounds)
  {
    combineBounds(hash, *state.brushEntityBounds);
    combineHash(
      hash,
      static_cast<int64_t>(std::hash<std::string_view>{}(state.brushEntityClassname)));
  }
  else
  {
    combineBounds(hash, state.brushBounds);
    combineHash(hash, static_cast<int64_t>(state.brushFaceCount));
  }

  const auto mixedHash = hash ^ (hash >> 32u);
  return static_cast<size_t>(mixedHash % readable2DBrushOutlinePalette().size());
}

Color brushOutlineColor(
  const BrushOutlineColorState& state,
  const Color& defaultEdgeColor,
  const Color& selectedEdgeColor,
  const Color& lockedEdgeColor)
{
  if (!state.readable2DOutlines)
  {
    return defaultEdgeColor;
  }

  if (state.locked)
  {
    return lockedEdgeColor;
  }

  if (state.selected || state.hasSelectedFaces)
  {
    return selectedEdgeColor;
  }

  return readable2DBrushOutlinePalette()[readable2DBrushOutlineColorIndex(state)];
}

BrushOutlineColorState brushOutlineColorState(
  const mdl::BrushNode& brushNode, const bool readable2DOutlines)
{
  auto state = BrushOutlineColorState{};
  state.readable2DOutlines = readable2DOutlines;
  state.locked = brushNode.locked();
  state.selected =
    brushNode.selected() || brushNode.parentSelected() || brushNode.descendantSelected();
  state.hasSelectedFaces = brushNode.hasSelectedFaces();
  state.brushBounds = brushNode.brush().bounds();
  state.brushFaceCount = brushNode.brush().faces().size();

  if (const auto* entityNode = brushNode.entity();
      entityNode
      && entityNode->entity().classname()
           != mdl::EntityPropertyValues::WorldspawnClassname)
  {
    state.brushEntityBounds = entityNode->logicalBounds();
    state.brushEntityClassname = entityNode->entity().classname();
  }

  return state;
}

} // namespace tb::render
