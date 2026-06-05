#pragma once

#include "ui/python/PythonPluginManifest.h"

#include <filesystem>
#include <string>
#include <vector>

namespace tb::ui
{
class MapWindow;

enum class PythonPluginStatus
{
  NotLoaded,
  Loaded,
  Failed,
};

struct PythonPluginState
{
  PythonPluginManifest manifest;
  PythonPluginStatus status = PythonPluginStatus::NotLoaded;
  std::string error;
};

class PythonPluginManager
{
private:
  std::vector<PythonPluginState> m_plugins;
  std::vector<PythonPluginManifestError> m_errors;

public:
  void reload(const std::vector<std::filesystem::path>& directories);
  bool loadPlugins(MapWindow& mapWindow);
  void unloadPlugins(MapWindow& mapWindow);

  const std::vector<PythonPluginState>& plugins() const;
  const std::vector<PythonPluginManifestError>& errors() const;

private:
  bool loadPlugin(MapWindow& mapWindow, PythonPluginState& plugin);
};

} // namespace tb::ui
