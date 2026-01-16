/*
 Copyright (C) 2010 Kristian Duske

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

#include "ReadMipTexture.h"

#include "Color.h"
#include "io/MaterialUtils.h"
#include "io/Reader.h"
#include "io/ReaderException.h"
#include "mdl/Palette.h"
#include "mdl/Texture.h"
#include "mdl/TextureBuffer.h"

#include "kdl/result.h"

#include <fmt/format.h>

namespace tb::io
{
namespace MipLayout
{
static constexpr size_t TextureNameLength = 16;
}

namespace
{

using GetMipPalette = std::function<Result<mdl::Palette>(Reader& reader)>;

Result<mdl::Palette> readHlMipPalette(Reader& reader)
{
  reader.seekFromBegin(0);
  reader.seekFromBegin(MipLayout::TextureNameLength);

  const auto width = reader.readSize<int32_t>();
  const auto height = reader.readSize<int32_t>();
  const auto mip0Offset = reader.readSize<int32_t>();

  // forward to the address of the color count
  reader.seekFromBegin(mip0Offset + (width * height * 85 >> 6));
  const auto colorCount = reader.readSize<uint16_t>();

  // palette data starts right after the color count
  auto data = std::vector<unsigned char>(colorCount * 3);
  reader.read(data.data(), data.size());
  return mdl::makePalette(data, mdl::PaletteColorFormat::Rgb);
}

void fixTransparentPixels(mdl::TextureBuffer& buffer, int width, int height)
{
  const int bpp = 4;
  if (buffer.size() != size_t(width * height * bpp))
    return;

  auto* data = buffer.data();

  for (int y = 0; y < height; ++y)
  {
    for (int x = 0; x < width; ++x)
    {
      size_t idx = static_cast<size_t>(y * width + x) * bpp;
      if (data[idx + 3] == 0) // Transparent
      {
        int r = 0, g = 0, b = 0, count = 0;

        // Check 4 neighbors
        const int dx[] = {0, 0, -1, 1};
        const int dy[] = {-1, 1, 0, 0};

        for (int k = 0; k < 4; ++k)
        {
          int nx = x + dx[k];
          int ny = y + dy[k];
          if (nx >= 0 && nx < width && ny >= 0 && ny < height)
          {
            size_t nidx = static_cast<size_t>(ny * width + nx) * bpp;
            if (data[nidx + 3] != 0) // Opaque neighbor
            {
              r += data[nidx + 0];
              g += data[nidx + 1];
              b += data[nidx + 2];
              count++;
            }
          }
        }

        if (count > 0)
        {
          data[idx + 0] = static_cast<unsigned char>(r / count);
          data[idx + 1] = static_cast<unsigned char>(g / count);
          data[idx + 2] = static_cast<unsigned char>(b / count);
        }
      }
    }
  }
}

Result<mdl::Texture> readMipTexture(
  Reader& reader, const GetMipPalette& getMipPalette, const mdl::TextureMask mask)
{
  static const auto MipLevels = size_t(4);

  auto averageColor = Color{RgbaF{}};
  auto buffers = mdl::TextureBufferList{MipLevels};
  size_t offset[MipLevels];

  try
  {
    // This is unused, we use the one from the wad directory (they're usually the same,
    // but could be different in broken .wad's.)
    reader.readString(MipLayout::TextureNameLength);

    const auto width = reader.readSize<int32_t>();
    const auto height = reader.readSize<int32_t>();

    if (!checkTextureDimensions(width, height))
    {
      return Error{fmt::format("Invalid texture dimensions: {}*{}", width, height)};
    }

    for (size_t i = 0; i < MipLevels; ++i)
    {
      offset[i] = static_cast<size_t>(reader.readSize<int32_t>());
    }

    const auto transparency = mask == mdl::TextureMask::On
                                ? mdl::PaletteTransparency::Index255Transparent
                                : mdl::PaletteTransparency::Opaque;

    mdl::setMipBufferSize(buffers, MipLevels, static_cast<size_t>(width), static_cast<size_t>(height), GL_RGBA);
    return getMipPalette(reader) | kdl::transform([&](const auto& palette) {
             for (size_t i = 0; i < MipLevels; ++i)
             {
               reader.seekFromBegin(offset[i]);
               const auto size = mipSize(width, height, i);

               auto tempColor = Color{RgbaF{}};
               palette.indexedToRgba(reader, size, buffers[i], transparency, tempColor);
               
               if (transparency == mdl::PaletteTransparency::Index255Transparent)
               {
                 const auto curWidth = std::max<int>(1, static_cast<int>(width >> i));
                 const auto curHeight = std::max<int>(1, static_cast<int>(height >> i));
                 fixTransparentPixels(buffers[i], curWidth, curHeight);
               }

               if (i == 0)
               {
                 averageColor = tempColor;
               }
             }

             return mdl::Texture{
               width,
               height,
               averageColor,
               GL_RGBA,
               mask,
               mdl::NoEmbeddedDefaults{},
               std::move(buffers)};
           });
  }
  catch (const ReaderException& e)
  {
    return Error{e.what()};
  }
}

} // namespace

std::string readMipTextureName(Reader& reader)
{
  try
  {
    auto nameReader = reader.buffer();
    return nameReader.readString(MipLayout::TextureNameLength);
  }
  catch (const ReaderException&)
  {
    return "";
  }
}

Result<mdl::Texture> readIdMipTexture(
  Reader& reader, const mdl::Palette& palette, const mdl::TextureMask mask)
{
  return readMipTexture(reader, [&](Reader&) { return palette; }, mask);
}

Result<mdl::Texture> readHlMipTexture(Reader& reader, const mdl::TextureMask mask)
{
  return readMipTexture(reader, readHlMipPalette, mask);
}

} // namespace tb::io
