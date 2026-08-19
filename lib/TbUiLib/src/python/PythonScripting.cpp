/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ui/python/PythonScripting.h"

#include "ui/AppController.h"
#include "ui/MapWindow.h"
#include "ui/python/PythonRuntime.h"

namespace tb::ui
{
PythonScripting::PythonScripting() = default;

PythonScripting& PythonScripting::instance()
{
  static auto instance = PythonScripting{};
  return instance;
}

bool PythonScripting::runScript(MapWindow& frame, const std::filesystem::path& path)
{
  auto context = PythonExecutionContext{};
  context.mapWindow = &frame;
  context.document = &frame.document();
  context.appController = &frame.appController();
  context.currentMapView = frame.currentMapViewBase();
  context.logger = &frame.pythonLogger();
  context.scriptPath = path;
  return PythonRuntime::instance().runScript(context, path);
}

bool PythonScripting::runConsoleCommand(MapWindow& frame, const std::string_view source)
{
  auto context = PythonExecutionContext{};
  context.mapWindow = &frame;
  context.document = &frame.document();
  context.appController = &frame.appController();
  context.currentMapView = frame.currentMapViewBase();
  context.logger = &frame.pythonLogger();
  return PythonRuntime::instance().runConsoleCommand(context, source);
}

PythonCompletionRoot PythonScripting::consoleCompletionRoot(
  MapWindow& frame, const std::string_view name) const
{
  return PythonRuntime::instance().consoleCompletionRoot(frame, name);
}

} // namespace tb::ui
