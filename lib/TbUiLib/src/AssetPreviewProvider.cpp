/*
 Copyright (C) 2026

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

#include "ui/AssetPreviewProvider.h"

#include "fs/DiskIO.h"
#include "fs/File.h"
#include "fs/Reader.h"
#include "mdl/GameFileSystem.h"

#include <cstdint>
#include <memory>
#include <span>

namespace tb::ui
{

AssetPreviewProvider::AssetPreviewProvider(const mdl::GameFileSystem& gameFileSystem)
  : m_gameFileSystem{gameFileSystem}
{
}

AssetPreviewState AssetPreviewProvider::preview(const BrowserAsset& asset) const
{
  switch (asset.type)
  {
  case BrowserCellType::Sprite:
    return spritePreview(asset);
  case BrowserCellType::Sound:
    return soundPreview(asset);
  case BrowserCellType::Folder:
  case BrowserCellType::Model:
    return {AssetPreviewStatus::Unsupported, std::nullopt};
  }

  return {AssetPreviewStatus::Unsupported, std::nullopt};
}

AssetPreviewState AssetPreviewProvider::spritePreview(const BrowserAsset& asset) const
{
  const auto& path = asset.absolutePath.empty() ? asset.path : asset.absolutePath;

  auto file = std::shared_ptr<fs::File>{};
  if (path.is_absolute())
  {
    auto fileResult = fs::Disk::openFile(path);
    if (fileResult.is_error())
    {
      return {AssetPreviewStatus::Missing, std::nullopt};
    }
    file = fileResult.value();
  }
  else
  {
    auto fileResult = m_gameFileSystem.openFile(path);
    if (fileResult.is_error())
    {
      return {AssetPreviewStatus::Missing, std::nullopt};
    }
    file = fileResult.value();
  }

  auto reader = file->reader().buffer();
  const auto bytes = reader.stringView();
  auto preview = loadGoldSrcSpritePreview(
    std::span{reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
  if (!preview)
  {
    return {AssetPreviewStatus::Error, std::nullopt};
  }

  return {AssetPreviewStatus::Ready, std::move(preview)};
}

AssetPreviewState AssetPreviewProvider::soundPreview(const BrowserAsset& asset) const
{
  auto soundPath = std::filesystem::path{};
  if (!asset.absolutePath.empty() && asset.absolutePath.is_absolute())
  {
    soundPath = asset.absolutePath;
  }
  else if (asset.path.is_absolute())
  {
    soundPath = asset.path;
  }
  else if (auto absPathResult = m_gameFileSystem.makeAbsolute(asset.path);
           !absPathResult.is_error())
  {
    soundPath = absPathResult.value();
  }

  if (soundPath.empty())
  {
    return {AssetPreviewStatus::Missing, std::nullopt};
  }

  auto fileResult = fs::Disk::openFile(soundPath);
  return fileResult.is_error()
           ? AssetPreviewState{AssetPreviewStatus::Missing, std::nullopt}
           : AssetPreviewState{AssetPreviewStatus::Ready, std::nullopt, soundPath};
}

AssetPreviewMap loadAssetPreviews(
  const AssetPreviewProvider& provider, const std::vector<BrowserAsset>& assets)
{
  auto previews = AssetPreviewMap{};
  for (const auto& asset : assets)
  {
    const auto preview = provider.preview(asset);
    if (preview.status != AssetPreviewStatus::Unsupported)
    {
      previews.emplace(asset.path, preview);
    }
  }
  return previews;
}

} // namespace tb::ui
