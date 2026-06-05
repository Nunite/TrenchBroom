#pragma once

#include "ui/python/PythonExecutionContext.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace tb::ui
{
class PythonPluginSession;

class PythonRuntime
{
private:
  PythonRuntime();

public:
  static PythonRuntime& instance();

  bool ensureInitialized();
  bool runScript(
    const PythonExecutionContext& context, const std::filesystem::path& path);
  bool runScript(PythonPluginSession& session);
  void runCallback(PythonPluginSession& session, void* callback);
  void emitEvent(const std::string& eventName, MapWindow& mapWindow);
  void emitEvent(
    const std::string& eventName, MapWindow& mapWindow, bool initializeIfNeeded);
  void cleanupPlugin(const std::string& pluginId);
  void cleanupPluginSession(PythonPluginSession& session);
  void cleanupDocument(MapWindow& mapWindow);

  const std::string& lastError() const;
  std::string formatCurrentException() const;

private:
  std::string m_lastError;

  bool runScript(
    const PythonExecutionContext& context,
    const std::filesystem::path& path,
    PythonPluginSession* session);
  bool installV2Module();
  bool prependSysPath(const std::filesystem::path& path);
};

PythonExecutionContext* currentPythonExecutionContext();
PythonPluginSession* currentPythonPluginSession();

} // namespace tb::ui
