/*
 Copyright (C) 2026 XiangXtreme

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

#include "ui/GoldSrcSpritePreview.h"

#include "base/Macros.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <type_traits>

namespace tb::ui
{
namespace
{

constexpr auto SpriteMagic = std::array<char, 4>{'I', 'D', 'S', 'P'};
constexpr auto HalfLifeSpriteVersion = 2;

enum class SpriteFrameType
{
  Single = 0,
  Group = 1,
};

enum class SpriteAlphaType
{
  Normal = 0,
  Additive = 1,
  IndexAlpha = 2,
  AlphaTest = 3,
};

class ByteReader
{
private:
  std::span<const std::uint8_t> m_data;
  std::size_t m_offset = 0u;

public:
  explicit ByteReader(std::span<const std::uint8_t> data)
    : m_data{data}
  {
  }

  std::size_t remaining() const { return m_data.size() - m_offset; }

  bool skip(const std::size_t count)
  {
    if (count > remaining())
    {
      return false;
    }
    m_offset += count;
    return true;
  }

  template <typename T>
  std::optional<T> read()
  {
    static_assert(std::is_integral_v<T> || std::is_same_v<T, float>);

    if (sizeof(T) > remaining())
    {
      return std::nullopt;
    }

    auto rawValue = std::uint64_t{0u};
    for (std::size_t i = 0u; i < sizeof(T); ++i)
    {
      rawValue |= std::uint64_t(m_data[m_offset + i]) << (i * 8u);
    }
    m_offset += sizeof(T);

    if constexpr (std::is_same_v<T, float>)
    {
      const auto floatBits = std::uint32_t(rawValue);
      auto value = T{};
      std::memcpy(&value, &floatBits, sizeof(value));
      return value;
    }
    else if constexpr (std::is_signed_v<T>)
    {
      using Unsigned = std::make_unsigned_t<T>;
      return T(Unsigned(rawValue));
    }
    else
    {
      return T(rawValue);
    }
  }

  std::optional<std::span<const std::uint8_t>> readBytes(const std::size_t count)
  {
    if (count > remaining())
    {
      return std::nullopt;
    }

    const auto result = m_data.subspan(m_offset, count);
    m_offset += count;
    return result;
  }
};

bool checkedPixelCount(
  const std::int32_t width, const std::int32_t height, std::size_t& pixelCount)
{
  if (width <= 0 || height <= 0)
  {
    return false;
  }

  const auto unsignedWidth = std::size_t(width);
  const auto unsignedHeight = std::size_t(height);
  if (unsignedHeight > std::numeric_limits<std::size_t>::max() / unsignedWidth)
  {
    return false;
  }

  pixelCount = unsignedWidth * unsignedHeight;
  return true;
}

std::optional<GoldSrcSpritePreview> decodeFrame(
  ByteReader& reader,
  const std::span<const std::uint8_t> palette,
  const SpriteAlphaType alphaType)
{
  const auto originX = reader.read<std::int32_t>();
  const auto originY = reader.read<std::int32_t>();
  const auto frameWidth = reader.read<std::int32_t>();
  const auto frameHeight = reader.read<std::int32_t>();
  if (!originX || !originY || !frameWidth || !frameHeight)
  {
    return std::nullopt;
  }

  unused(*originX);
  unused(*originY);

  auto pixelCount = std::size_t{};
  if (!checkedPixelCount(*frameWidth, *frameHeight, pixelCount))
  {
    return std::nullopt;
  }

  const auto pixels = reader.readBytes(pixelCount);
  if (!pixels)
  {
    return std::nullopt;
  }

  auto rgba = std::vector<std::uint8_t>(pixelCount * 4u, 0u);
  for (std::size_t i = 0u; i < pixelCount; ++i)
  {
    const auto paletteIndex = std::size_t((*pixels)[i]);
    const auto paletteOffset = paletteIndex * 3u;
    if (paletteOffset + 2u >= palette.size())
    {
      return std::nullopt;
    }

    const auto outOffset = i * 4u;
    rgba[outOffset + 0u] = palette[paletteOffset + 0u];
    rgba[outOffset + 1u] = palette[paletteOffset + 1u];
    rgba[outOffset + 2u] = palette[paletteOffset + 2u];

    switch (alphaType)
    {
    case SpriteAlphaType::AlphaTest:
      rgba[outOffset + 3u] = (*pixels)[i] == 255u ? 0u : 255u;
      break;
    case SpriteAlphaType::IndexAlpha:
      rgba[outOffset + 3u] = (*pixels)[i];
      break;
    case SpriteAlphaType::Normal:
    case SpriteAlphaType::Additive:
      rgba[outOffset + 3u] = 255u;
      break;
    }
  }

  return GoldSrcSpritePreview{
    std::size_t(*frameWidth), std::size_t(*frameHeight), std::move(rgba)};
}

} // namespace

std::optional<GoldSrcSpritePreview> loadGoldSrcSpritePreview(
  const std::span<const std::uint8_t> data)
{
  auto reader = ByteReader{data};
  const auto magicBytes = reader.readBytes(SpriteMagic.size());
  if (!magicBytes || !std::ranges::equal(*magicBytes, SpriteMagic))
  {
    return std::nullopt;
  }

  const auto version = reader.read<std::int32_t>();
  const auto spriteType = reader.read<std::int32_t>();
  const auto rawAlphaType = reader.read<std::int32_t>();
  const auto radius = reader.read<float>();
  const auto width = reader.read<std::int32_t>();
  const auto height = reader.read<std::int32_t>();
  const auto frameCount = reader.read<std::int32_t>();
  const auto beamLength = reader.read<float>();
  const auto syncType = reader.read<std::int32_t>();
  if (
    !version || !spriteType || !rawAlphaType || !radius || !width || !height
    || !frameCount || !beamLength || !syncType || *version != HalfLifeSpriteVersion
    || *frameCount <= 0)
  {
    return std::nullopt;
  }

  unused(*spriteType);
  unused(*radius);
  unused(*beamLength);
  unused(*syncType);

  if (
    *rawAlphaType < int(SpriteAlphaType::Normal)
    || *rawAlphaType > int(SpriteAlphaType::AlphaTest))
  {
    return std::nullopt;
  }
  const auto alphaType = SpriteAlphaType{*rawAlphaType};

  auto headerPixelCount = std::size_t{};
  if (!checkedPixelCount(*width, *height, headerPixelCount))
  {
    return std::nullopt;
  }

  const auto paletteSize = reader.read<std::uint16_t>();
  if (!paletteSize || *paletteSize == 0u)
  {
    return std::nullopt;
  }

  const auto paletteByteCount = std::size_t(*paletteSize) * 3u;
  const auto palette = reader.readBytes(paletteByteCount);
  if (!palette)
  {
    return std::nullopt;
  }

  const auto rawFrameType = reader.read<std::int32_t>();
  if (!rawFrameType)
  {
    return std::nullopt;
  }

  if (*rawFrameType == int(SpriteFrameType::Single))
  {
    return decodeFrame(reader, *palette, alphaType);
  }

  if (*rawFrameType != int(SpriteFrameType::Group))
  {
    return std::nullopt;
  }

  const auto groupFrameCount = reader.read<std::int32_t>();
  if (!groupFrameCount || *groupFrameCount <= 0)
  {
    return std::nullopt;
  }

  if (!reader.skip(std::size_t(*groupFrameCount) * sizeof(float)))
  {
    return std::nullopt;
  }

  return decodeFrame(reader, *palette, alphaType);
}

} // namespace tb::ui
