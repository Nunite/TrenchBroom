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

#include "ColorChannel.h"

#include <set>

#include <catch2/catch_test_macros.hpp>

namespace tb::render
{
namespace
{

const auto DefaultEdgeColor = Color{RgbF{0.9f, 0.9f, 0.9f}};
const auto SelectedEdgeColor = Color{RgbF{1.0f, 0.0f, 0.0f}};
const auto LockedEdgeColor = Color{RgbF{0.0f, 0.0f, 1.0f}};
constexpr auto Epsilon = 0.0001f;

BrushOutlineColorState makeState(const vm::bbox3d& bounds)
{
  auto state = BrushOutlineColorState{};
  state.readable2DOutlines = true;
  state.brushBounds = bounds;
  state.brushFaceCount = 6u;
  return state;
}

bool sameColor(const Color& lhs, const Color& rhs)
{
  return lhs.to<RgbaF>() == rhs.to<RgbaF>();
}

bool withinEpsilon(const float lhs, const float rhs)
{
  return std::abs(lhs - rhs) <= Epsilon;
}

} // namespace

TEST_CASE("BrushOutlineColor.defaultEdgeColor")
{
  auto state = makeState(vm::bbox3d{{0, 0, 0}, {64, 64, 64}});
  state.readable2DOutlines = false;

  CHECK(sameColor(
    brushOutlineColor(state, DefaultEdgeColor, SelectedEdgeColor, LockedEdgeColor),
    DefaultEdgeColor));
}

TEST_CASE("BrushOutlineColor.readableWorldBrushColor")
{
  const auto state = makeState(vm::bbox3d{{0, 0, 0}, {64, 64, 64}});

  const auto color =
    brushOutlineColor(state, DefaultEdgeColor, SelectedEdgeColor, LockedEdgeColor);

  CHECK_FALSE(sameColor(color, DefaultEdgeColor));
  CHECK(
    readable2DBrushOutlineColorIndex(state) == readable2DBrushOutlineColorIndex(state));
}

TEST_CASE("BrushOutlineColor.entityBrushesShareColor")
{
  auto firstBrush = makeState(vm::bbox3d{{0, 0, 0}, {64, 64, 64}});
  firstBrush.brushEntityBounds = vm::bbox3d{{0, 0, 0}, {128, 128, 64}};
  firstBrush.brushEntityClassname = "func_detail";

  auto secondBrush = makeState(vm::bbox3d{{64, 0, 0}, {128, 64, 64}});
  secondBrush.brushFaceCount = 8u;
  secondBrush.brushEntityBounds = firstBrush.brushEntityBounds;
  secondBrush.brushEntityClassname = firstBrush.brushEntityClassname;

  CHECK(
    readable2DBrushOutlineColorIndex(firstBrush)
    == readable2DBrushOutlineColorIndex(secondBrush));
}

TEST_CASE("BrushOutlineColor.worldBrushesCanReceiveDifferentColors")
{
  auto usedColors = std::set<size_t>{};
  for (size_t i = 0; i < 16; ++i)
  {
    const auto x = static_cast<double>(i * 128);
    const auto brush = makeState(vm::bbox3d{{x, 0, 0}, {x + 64, 64, 64}});
    usedColors.insert(readable2DBrushOutlineColorIndex(brush));
  }

  CHECK(usedColors.size() > 1u);
}

TEST_CASE("BrushOutlineColor.readableWorldBrushColorsStayInJackBrushTintRange")
{
  for (size_t i = 0; i < 16; ++i)
  {
    const auto x = static_cast<double>(i * 128);
    const auto brush = makeState(vm::bbox3d{{x, 0, 0}, {x + 64, 64, 64}});
    const auto color =
      brushOutlineColor(brush, DefaultEdgeColor, SelectedEdgeColor, LockedEdgeColor)
        .to<RgbaF>();

    CHECK(color.get<ColorChannel::r>() >= (0.35f - Epsilon));
    CHECK(color.get<ColorChannel::r>() <= (0.75f + Epsilon));
    CHECK(color.get<ColorChannel::g>() >= (0.60f - Epsilon));
    CHECK(color.get<ColorChannel::g>() <= (1.0f + Epsilon));
    CHECK(color.get<ColorChannel::b>() >= (0.60f - Epsilon));
    CHECK(color.get<ColorChannel::b>() <= (1.0f + Epsilon));
    CHECK(withinEpsilon(color.get<ColorChannel::a>(), 1.0f));
  }
}

TEST_CASE("BrushOutlineColor.readableWorldBrushColorsRemainBlueGreenBiased")
{
  for (size_t i = 0; i < 16; ++i)
  {
    const auto x = static_cast<double>(i * 128);
    const auto brush = makeState(vm::bbox3d{{x, 0, 0}, {x + 64, 64, 64}});
    const auto color =
      brushOutlineColor(brush, DefaultEdgeColor, SelectedEdgeColor, LockedEdgeColor)
        .to<RgbaF>();

    CHECK(color.get<ColorChannel::g>() >= color.get<ColorChannel::r>());
    CHECK(color.get<ColorChannel::b>() >= color.get<ColorChannel::r>());
  }
}

TEST_CASE("BrushOutlineColor.selectedPriority")
{
  auto state = makeState(vm::bbox3d{{0, 0, 0}, {64, 64, 64}});
  state.selected = true;

  CHECK(sameColor(
    brushOutlineColor(state, DefaultEdgeColor, SelectedEdgeColor, LockedEdgeColor),
    SelectedEdgeColor));
}

TEST_CASE("BrushOutlineColor.selectedFacesPriority")
{
  auto state = makeState(vm::bbox3d{{0, 0, 0}, {64, 64, 64}});
  state.hasSelectedFaces = true;

  CHECK(sameColor(
    brushOutlineColor(state, DefaultEdgeColor, SelectedEdgeColor, LockedEdgeColor),
    SelectedEdgeColor));
}

TEST_CASE("BrushOutlineColor.lockedPriority")
{
  auto state = makeState(vm::bbox3d{{0, 0, 0}, {64, 64, 64}});
  state.locked = true;
  state.selected = true;

  CHECK(sameColor(
    brushOutlineColor(state, DefaultEdgeColor, SelectedEdgeColor, LockedEdgeColor),
    LockedEdgeColor));
}

} // namespace tb::render
