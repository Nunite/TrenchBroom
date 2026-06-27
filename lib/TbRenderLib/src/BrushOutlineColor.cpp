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
#include <limits>

namespace tb::render
{

namespace
{

constexpr auto JackBrushDefaultColor = RgbaF{1.0f, 1.0f, 1.0f, 1.0f};
constexpr auto JackBrushRandomColorMin = RgbaF{0.0f, 100.0f / 255.0f, 100.0f / 255.0f, 1.0f};
constexpr auto JackBrushRandomColorMax = RgbaF{0.0f, 1.0f, 1.0f, 1.0f};
constexpr auto JackBrushTintMin = 0.25f;
constexpr auto JackBrushTintMax = 0.65f;

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

float normalizedHashComponent(const uint64_t hash, const uint64_t salt)
{
  const auto mixed = hash ^ salt ^ (hash >> 17u) ^ (hash << 13u);
  const auto bucket = static_cast<float>(mixed & 0xFFu);
  return bucket / 255.0f;
}

float interpolate(const float minValue, const float maxValue, const float mix)
{
  return minValue + (maxValue - minValue) * mix;
}

} // namespace

Color readable2DBrushOutlinePaletteColor(const BrushOutlineColorState& state)
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

  const auto greenMix = normalizedHashComponent(hash, 0x4a41434b47524545ull);
  const auto blueMix = normalizedHashComponent(hash, 0x4a41434b424c5545ull);
  const auto tintMix = normalizedHashComponent(hash, 0x4a41434b54494e54ull);

  const auto randomGreen = interpolate(
    JackBrushRandomColorMin.get<ColorChannel::g>(),
    JackBrushRandomColorMax.get<ColorChannel::g>(),
    greenMix);
  const auto randomBlue = interpolate(
    JackBrushRandomColorMin.get<ColorChannel::b>(),
    JackBrushRandomColorMax.get<ColorChannel::b>(),
    blueMix);
  const auto tint = interpolate(JackBrushTintMin, JackBrushTintMax, tintMix);

  return RgbaF{
    interpolate(JackBrushDefaultColor.get<ColorChannel::r>(), 0.0f, tint),
    interpolate(JackBrushDefaultColor.get<ColorChannel::g>(), randomGreen, tint),
    interpolate(JackBrushDefaultColor.get<ColorChannel::b>(), randomBlue, tint),
    JackBrushDefaultColor.get<ColorChannel::a>()};
}

const std::vector<Color>& readable2DBrushOutlinePalette()
{
  static const auto palette = std::vector<Color>{
    readable2DBrushOutlinePaletteColor(BrushOutlineColorState{
      .readable2DOutlines = true,
      .brushBounds = vm::bbox3d{{0.0, 0.0, 0.0}, {64.0, 64.0, 64.0}},
      .brushFaceCount = 6u}),
    readable2DBrushOutlinePaletteColor(BrushOutlineColorState{
      .readable2DOutlines = true,
      .brushBounds = vm::bbox3d{{128.0, 0.0, 0.0}, {192.0, 64.0, 64.0}},
      .brushFaceCount = 6u}),
    readable2DBrushOutlinePaletteColor(BrushOutlineColorState{
      .readable2DOutlines = true,
      .brushBounds = vm::bbox3d{{256.0, 0.0, 0.0}, {320.0, 64.0, 64.0}},
      .brushFaceCount = 6u}),
    readable2DBrushOutlinePaletteColor(BrushOutlineColorState{
      .readable2DOutlines = true,
      .brushBounds = vm::bbox3d{{384.0, 0.0, 0.0}, {448.0, 64.0, 64.0}},
      .brushFaceCount = 6u}),
  };
  return palette;
}

size_t readable2DBrushOutlineColorIndex(const BrushOutlineColorState& state)
{
  const auto color = readable2DBrushOutlinePaletteColor(state).to<RgbaF>();

  size_t bestIndex = 0u;
  auto bestDistance = std::numeric_limits<float>::max();

  const auto& palette = readable2DBrushOutlinePalette();
  for (size_t i = 0u; i < palette.size(); ++i)
  {
    const auto candidate = palette[i].to<RgbaF>();
    const auto dr = candidate.get<ColorChannel::r>() - color.get<ColorChannel::r>();
    const auto dg = candidate.get<ColorChannel::g>() - color.get<ColorChannel::g>();
    const auto db = candidate.get<ColorChannel::b>() - color.get<ColorChannel::b>();
    const auto distance = dr * dr + dg * dg + db * db;
    if (distance < bestDistance)
    {
      bestDistance = distance;
      bestIndex = i;
    }
  }

  return bestIndex;
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

  return readable2DBrushOutlinePaletteColor(state);
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
