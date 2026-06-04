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

#include "Macros.h"
#include "gl/TextureResource.h"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace tb
{
namespace gl
{
class Material;
class MaterialManager;
} // namespace gl

namespace mdl
{
class Map;
} // namespace mdl

namespace render
{
class RenderBatch;
class RenderContext;

using SkyMaterialNames = std::array<std::string, 6>;

SkyMaterialNames skyMaterialNames(const std::string& skyname);

bool shouldRenderSky(bool render3D, bool showSky, const std::string& skyname);

bool skyMaterialsReady(const std::array<const gl::Material*, 6>& materials);

std::array<std::filesystem::path, 6> looseSkyMaterialPaths(const std::string& name);

bool skyTexturesReady(
  const std::array<std::shared_ptr<gl::TextureResource>, 6>& textures);

class SkyRenderer
{
private:
  mdl::Map& m_map;
  std::optional<std::string> m_cachedSkyname;
  std::array<std::shared_ptr<gl::TextureResource>, 6> m_textures = {};

public:
  explicit SkyRenderer(mdl::Map& map);
  ~SkyRenderer();

  deleteCopyAndMove(SkyRenderer);

  void invalidate();
  void render(RenderContext& renderContext, RenderBatch& renderBatch);

private:
  bool validate();
};

} // namespace render
} // namespace tb
