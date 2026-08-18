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

#pragma once

#include "base/Macros.h"
#include "ui/python/PythonCompletionEngine.h"

#include <filesystem>
#include <string_view>

namespace tb::ui
{
class MapWindow;

class PythonScripting
{
private:
  PythonScripting();

public:
  deleteCopyAndMove(PythonScripting);

  static PythonScripting& instance();

  bool runScript(MapWindow& frame, const std::filesystem::path& path);
  bool runConsoleCommand(MapWindow& frame, std::string_view source);
  PythonCompletionRoot consoleCompletionRoot(
    MapWindow& frame, std::string_view name) const;
};

} // namespace tb::ui
