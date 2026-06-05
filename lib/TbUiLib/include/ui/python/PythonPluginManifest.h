#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace tb::ui
{

enum class PythonPluginType
{
  Script,
  Ui,
};

struct PythonPluginManifest
{
  std::string id;
  std::string name;
  std::string version;
  int apiVersion = 2;
  PythonPluginType pluginType = PythonPluginType::Script;
  std::filesystem::path entry;
  std::string description;
  std::string author;
  std::filesystem::path directory;
};

struct PythonPluginManifestError
{
  std::filesystem::path path;
  std::string message;
};

struct PythonPluginManifestResult
{
  std::optional<PythonPluginManifest> manifest;
  std::optional<PythonPluginManifestError> error;
};

PythonPluginManifestResult loadPythonPluginManifest(const std::filesystem::path& path);
std::string pythonPluginTypeName(PythonPluginType type);
std::vector<std::filesystem::path> splitPythonPluginDirectories(const std::string& paths);
std::string joinPythonPluginDirectories(const std::vector<std::filesystem::path>& paths);

} // namespace tb::ui
