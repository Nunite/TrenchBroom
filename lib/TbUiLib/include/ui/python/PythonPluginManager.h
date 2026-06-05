#pragma once

#include "ui/python/PythonPluginManifest.h"
#include "ui/python/PythonPluginSession.h"

#include <filesystem>
#include <memory>
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
  std::unique_ptr<PythonPluginSession> session;

  PythonPluginState() = default;
  PythonPluginState(PythonPluginState&&) noexcept = default;
  PythonPluginState& operator=(PythonPluginState&&) noexcept = default;
  ~PythonPluginState() = default;

  PythonPluginState(const PythonPluginState&) = delete;
  PythonPluginState& operator=(const PythonPluginState&) = delete;
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
