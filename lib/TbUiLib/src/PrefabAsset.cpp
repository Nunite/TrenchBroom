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

namespace tb::ui
{

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
    return Error{fmt::format("Prefab already exists: {}", path.filename())};
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

} // namespace tb::ui
