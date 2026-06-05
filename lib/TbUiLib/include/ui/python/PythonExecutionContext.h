#pragma once

#include <filesystem>
#include <string>

namespace tb::ui
{
class AppController;
class MapDocument;
class MapViewBase;
class MapWindow;
} // namespace tb::ui

namespace tb
{
class Logger;
} // namespace tb

namespace tb::ui
{

struct PythonExecutionContext
{
  MapWindow* mapWindow = nullptr;
  MapDocument* document = nullptr;
  AppController* appController = nullptr;
  MapViewBase* currentMapView = nullptr;
  Logger* logger = nullptr;
  std::filesystem::path scriptPath;
  std::filesystem::path pluginDirectory;
  std::string pluginId;
};

} // namespace tb::ui
