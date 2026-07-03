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

#include "ui/PrefabAsset.h"

#include <QColor>

#include "Error.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "fs/DiskIO.h"
#include "fs/File.h"
#include "fs/PathInfo.h"
#include "fs/Reader.h"
#include "ui/SystemPaths.h"

#include "kd/path_utils.h"

#include <fmt/format.h>
#include <fmt/std.h>

#include <algorithm>
#include <cmath>

namespace tb::ui
{
namespace
{

constexpr auto ThumbnailCropPaddingRatio = 0.12;
constexpr auto ThumbnailBackgroundTolerance = 10;

bool differsFromBackground(const QColor& color, const QColor& background)
{
  return std::abs(color.red() - background.red()) > ThumbnailBackgroundTolerance
         || std::abs(color.green() - background.green()) > ThumbnailBackgroundTolerance
         || std::abs(color.blue() - background.blue()) > ThumbnailBackgroundTolerance
         || std::abs(color.alpha() - background.alpha()) > ThumbnailBackgroundTolerance;
}

} // namespace

bool isPrefabAssetPath(const std::filesystem::path& path)
{
  return kdl::path_to_lower(path.extension()) == ".tbprefab";
}

std::filesystem::path defaultPrefabDirectory()
{
  return SystemPaths::userDataDirectory() / "prefabs";
}

std::filesystem::path configuredPrefabDirectory()
{
  const auto& directory = pref(Preferences::PrefabDirectory);
  if (directory.empty() || fs::Disk::pathInfo(directory) != fs::PathInfo::Directory)
  {
    return defaultPrefabDirectory();
  }
  return directory;
}

std::filesystem::path prefabPathForName(
  const std::filesystem::path& directory, const std::string& name)
{
  auto path = directory / std::filesystem::path{name}.filename();
  if (path.extension().empty())
  {
    path.replace_extension(".tbprefab");
  }
  return path;
}

std::filesystem::path prefabThumbnailPath(const std::filesystem::path& prefabPath)
{
  auto path = prefabPath;
  path.replace_extension(".png");
  return path;
}

QImage cropPrefabThumbnailImage(const QImage& image)
{
  if (image.isNull() || image.width() < 2 || image.height() < 2)
  {
    return image;
  }

  const auto background = image.pixelColor(0, 0);
  auto left = image.width();
  auto top = image.height();
  auto right = -1;
  auto bottom = -1;

  for (auto y = 0; y < image.height(); ++y)
  {
    for (auto x = 0; x < image.width(); ++x)
    {
      if (differsFromBackground(image.pixelColor(x, y), background))
      {
        left = std::min(left, x);
        top = std::min(top, y);
        right = std::max(right, x);
        bottom = std::max(bottom, y);
      }
    }
  }

  if (right < left || bottom < top)
  {
    return image;
  }

  const auto padding = int(
    std::round(std::max(right - left + 1, bottom - top + 1) * ThumbnailCropPaddingRatio));
  const auto cropLeft = std::max(0, left - padding);
  const auto cropTop = std::max(0, top - padding);
  const auto cropRight = std::min(image.width() - 1, right + padding);
  const auto cropBottom = std::min(image.height() - 1, bottom + padding);

  return image.copy(
    cropLeft, cropTop, cropRight - cropLeft + 1, cropBottom - cropTop + 1);
}

Result<void> checkPrefabNameAvailable(
  const std::filesystem::path& directory, const std::string& name)
{
  const auto path = prefabPathForName(directory, name);
  if (!isPrefabAssetPath(path))
  {
    return Error{"Prefab name must use the .tbprefab extension"};
  }
  if (fs::Disk::pathInfo(path) != fs::PathInfo::Unknown)
  {
    return Error{fmt::format("Prefab already exists: {}", path.filename().string())};
  }
  return kdl::void_success;
}

Result<std::string> readPrefabAsset(const std::filesystem::path& path)
{
  if (!isPrefabAssetPath(path))
  {
    return Error{"Not a prefab asset"};
  }

  return fs::Disk::openFile(path) | kdl::transform([](auto file) {
           auto reader = file->reader().buffer();
           return std::string{reader.stringView()};
         });
}

Result<void> writePrefabAsset(const std::filesystem::path& path, const std::string& text)
{
  if (!isPrefabAssetPath(path))
  {
    return Error{"Not a prefab asset"};
  }

  return fs::Disk::createDirectory(path.parent_path())
         | kdl::and_then([&](auto) -> Result<void> {
             return fs::Disk::withOutputStream(
               path,
               std::ios::out | std::ios::binary | std::ios::trunc,
               [&](auto& stream) -> Result<void> {
                 stream << text;
                 if (!stream.good())
                 {
                   return Error{"Failed to write prefab asset"};
                 }
                 return Result<void>{};
               });
           });
}

Result<void> renamePrefabAsset(const std::filesystem::path& path, const std::string& name)
{
  if (!isPrefabAssetPath(path))
  {
    return Error{"Not a prefab asset"};
  }
  if (fs::Disk::pathInfo(path) != fs::PathInfo::File)
  {
    return Error{"Prefab asset does not exist"};
  }

  const auto newPath = prefabPathForName(path.parent_path(), name);
  if (newPath == path)
  {
    return kdl::void_success;
  }
  if (const auto result = checkPrefabNameAvailable(path.parent_path(), name);
      result.is_error())
  {
    return result;
  }

  return fs::Disk::moveFile(path, newPath) | kdl::and_then([&]() -> Result<void> {
           const auto oldThumbnailPath = prefabThumbnailPath(path);
           if (fs::Disk::pathInfo(oldThumbnailPath) != fs::PathInfo::File)
           {
             return kdl::void_success;
           }
           return fs::Disk::moveFile(oldThumbnailPath, prefabThumbnailPath(newPath));
         });
}

Result<void> deletePrefabAsset(const std::filesystem::path& path)
{
  if (!isPrefabAssetPath(path))
  {
    return Error{"Not a prefab asset"};
  }

  return fs::Disk::deleteFile(path) | kdl::and_then([&](auto) -> Result<void> {
           return fs::Disk::deleteFile(prefabThumbnailPath(path))
                  | kdl::transform([](auto) {});
         });
}

} // namespace tb::ui
