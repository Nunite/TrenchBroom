#include "ui/python/PythonPluginManager.h"

#include "prefs/Preferences.h"
#include "ui/MapWindow.h"
#include "ui/python/PythonPluginSession.h"
#include "ui/python/PythonRuntime.h"

#include <algorithm>
#include <filesystem>

namespace tb::ui
{
namespace
{
constexpr auto ManifestFileName = "trenchbroom-plugin.json";

bool hasPluginManifest(const std::filesystem::path& directory)
{
  auto error = std::error_code{};
  return std::filesystem::exists(directory / ManifestFileName, error) && !error;
}

std::vector<std::filesystem::path> discoverPluginDirectories(
  const std::filesystem::path& directory)
{
  auto result = std::vector<std::filesystem::path>{};
  if (hasPluginManifest(directory))
  {
    result.push_back(directory);
    return result;
  }

  auto error = std::error_code{};
  if (!std::filesystem::is_directory(directory, error) || error)
  {
    return result;
  }

  for (const auto& entry : std::filesystem::directory_iterator{directory, error})
  {
    if (entry.is_directory(error) && !error && hasPluginManifest(entry.path()))
    {
      result.push_back(entry.path());
    }
    error.clear();
  }

  std::ranges::sort(result);
  return result;
}
} // namespace

void PythonPluginManager::reload(const std::vector<std::filesystem::path>& directories)
{
  m_plugins.clear();
  m_errors.clear();

  for (const auto& directory : directories)
  {
    auto pluginDirectories = discoverPluginDirectories(directory);
    if (pluginDirectories.empty())
    {
      pluginDirectories.push_back(directory);
    }

    for (const auto& pluginDirectory : pluginDirectories)
    {
      auto result = loadPythonPluginManifest(pluginDirectory);
      if (result.manifest)
      {
        if (result.manifest->pluginType != PythonPluginType::Ui)
        {
          m_errors.push_back(PythonPluginManifestError{
            pluginDirectory / ManifestFileName,
            "Script-only Python manifest; use Run Python Script to execute it"});
          continue;
        }

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
  auto unloadedAnyPlugin = false;
  for (auto& plugin : m_plugins)
  {
    if (plugin.status == PythonPluginStatus::Loaded)
    {
      if (plugin.session)
      {
        PythonRuntime::instance().cleanupPluginSession(*plugin.session);
        plugin.session.reset();
      }
      plugin.status = PythonPluginStatus::NotLoaded;
      plugin.error.clear();
      unloadedAnyPlugin = true;
    }
  }
  if (unloadedAnyPlugin)
  {
    PythonRuntime::instance().cleanupDocument(mapWindow);
  }
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

  plugin.session =
    std::make_shared<PythonPluginSession>(plugin.manifest, std::move(context));

  if (PythonRuntime::instance().runScript(*plugin.session))
  {
    plugin.status = PythonPluginStatus::Loaded;
    plugin.error.clear();
    plugin.session->clearError();
    return true;
  }

  plugin.status = PythonPluginStatus::Failed;
  plugin.error = PythonRuntime::instance().lastError();
  if (plugin.error.empty())
  {
    plugin.error = "Plugin entry script failed";
  }
  plugin.session->setError(plugin.error);
  return false;
}

} // namespace tb::ui
