#include "ui/python/PythonPluginManager.h"

#include "Preferences.h"
#include "ui/MapWindow.h"
#include "ui/python/PythonRuntime.h"

#include <algorithm>

namespace tb::ui
{

void PythonPluginManager::reload(const std::vector<std::filesystem::path>& directories)
{
  m_plugins.clear();
  m_errors.clear();

  for (const auto& directory : directories)
  {
    auto result = loadPythonPluginManifest(directory);
    if (result.manifest)
    {
      auto state = PythonPluginState{};
      state.manifest = std::move(*result.manifest);
      m_plugins.push_back(std::move(state));
    }
    else if (result.error)
    {
      m_errors.push_back(std::move(*result.error));
    }
  }
}

bool PythonPluginManager::loadPlugins(MapWindow& mapWindow)
{
  auto ok = true;
  for (auto& plugin : m_plugins)
  {
    ok = loadPlugin(mapWindow, plugin) && ok;
  }
  return ok;
}

void PythonPluginManager::unloadPlugins(MapWindow& mapWindow)
{
  for (auto& plugin : m_plugins)
  {
    if (plugin.status == PythonPluginStatus::Loaded)
    {
      PythonRuntime::instance().cleanupPlugin(plugin.manifest.id);
      plugin.status = PythonPluginStatus::NotLoaded;
      plugin.error.clear();
    }
  }
  PythonRuntime::instance().cleanupDocument(mapWindow);
}

const std::vector<PythonPluginState>& PythonPluginManager::plugins() const
{
  return m_plugins;
}

const std::vector<PythonPluginManifestError>& PythonPluginManager::errors() const
{
  return m_errors;
}

bool PythonPluginManager::loadPlugin(MapWindow& mapWindow, PythonPluginState& plugin)
{
  auto context = PythonExecutionContext{};
  context.mapWindow = &mapWindow;
  context.document = &mapWindow.document();
  context.appController = &mapWindow.appController();
  context.currentMapView = nullptr;
  context.logger = &mapWindow.pythonLogger();
  context.pluginId = plugin.manifest.id;
  context.pluginDirectory = plugin.manifest.directory;
  context.scriptPath = plugin.manifest.directory / plugin.manifest.entry;

  if (PythonRuntime::instance().runScript(context, context.scriptPath))
  {
    plugin.status = PythonPluginStatus::Loaded;
    plugin.error.clear();
    return true;
  }

  plugin.status = PythonPluginStatus::Failed;
  plugin.error = PythonRuntime::instance().lastError();
  if (plugin.error.empty())
  {
    plugin.error = "Plugin entry script failed";
  }
  return false;
}

} // namespace tb::ui
