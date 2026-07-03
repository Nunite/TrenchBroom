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
#include "fs/DiskIO.h"
#include "fs/File.h"
#include "fs/Reader.h"

#include "kd/path_utils.h"

namespace tb::ui
{

bool isPrefabAssetPath(const std::filesystem::path& path)
{
  return kdl::path_to_lower(path.extension()) == ".tbprefab";
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
