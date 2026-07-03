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

#pragma once

#include "Result.h"

#include <filesystem>
#include <string>

namespace tb::ui
{

bool isPrefabAssetPath(const std::filesystem::path& path);

Result<std::string> readPrefabAsset(const std::filesystem::path& path);

Result<void> writePrefabAsset(const std::filesystem::path& path, const std::string& text);

} // namespace tb::ui
