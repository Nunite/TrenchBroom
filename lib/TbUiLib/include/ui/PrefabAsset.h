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

#pragma once

#include <QImage>

#include "base/Result.h"

#include <filesystem>
#include <string>
#include <vector>

namespace tb::ui
{

bool isPrefabAssetPath(const std::filesystem::path& path);

std::filesystem::path defaultPrefabDirectory();

std::filesystem::path configuredPrefabDirectory();

std::filesystem::path prefabPathForName(
  const std::filesystem::path& directory, const std::string& name);

std::filesystem::path prefabThumbnailPath(const std::filesystem::path& prefabPath);

std::string appendPrefabMaterialCollections(
  const std::string& prefabText,
  const std::vector<std::filesystem::path>& materialCollections);

std::vector<std::filesystem::path> prefabMaterialCollections(
  const std::string& prefabText);

std::string appendPrefabWadPaths(
  const std::string& prefabText, const std::vector<std::string>& wadPaths);

std::vector<std::string> prefabWadPaths(const std::string& prefabText);

QImage cropPrefabThumbnailImage(const QImage& image);

Result<void> checkPrefabNameAvailable(
  const std::filesystem::path& directory, const std::string& name);

Result<std::string> readPrefabAsset(const std::filesystem::path& path);

Result<void> writePrefabAsset(const std::filesystem::path& path, const std::string& text);

Result<void> renamePrefabAsset(
  const std::filesystem::path& path, const std::string& name);

Result<void> deletePrefabAsset(const std::filesystem::path& path);

} // namespace tb::ui
