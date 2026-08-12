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

#include "base/Color.h"

#include "vm/bbox.h"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace tb::mdl
{
class BrushNode;
}

namespace tb::render
{

struct BrushOutlineColorState
{
  bool readable2DOutlines = false;
  bool locked = false;
  bool selected = false;
  bool hasSelectedFaces = false;
  vm::bbox3d brushBounds;
  size_t brushFaceCount = 0;
  std::optional<vm::bbox3d> brushEntityBounds;
  std::string_view brushEntityClassname;
};

const std::vector<Color>& readable2DBrushOutlinePalette();

size_t readable2DBrushOutlineColorIndex(const BrushOutlineColorState& state);

Color brushOutlineColor(
  const BrushOutlineColorState& state,
  const Color& defaultEdgeColor,
  const Color& selectedEdgeColor,
  const Color& lockedEdgeColor);

BrushOutlineColorState brushOutlineColorState(
  const mdl::BrushNode& brushNode, bool readable2DOutlines);

} // namespace tb::render
