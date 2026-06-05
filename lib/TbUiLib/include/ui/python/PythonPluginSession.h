#pragma once

#include <QPointer>

#include "ui/python/PythonExecutionContext.h"
#include "ui/python/PythonPluginManifest.h"

#include <string>
#include <vector>

class QWidget;

namespace tb::ui
{

class PythonPluginSession
{
private:
  PythonPluginManifest m_manifest;
  PythonExecutionContext m_context;
  std::vector<int> m_callbackTokens;
  std::vector<QPointer<QWidget>> m_pluginPanels;
  std::string m_error;

public:
  explicit PythonPluginSession(
    PythonPluginManifest manifest, PythonExecutionContext context);

  const PythonPluginManifest& manifest() const;
  const std::string& pluginId() const;
  PythonExecutionContext& context();
  const PythonExecutionContext& context() const;

  void addCallbackToken(int token);
  std::vector<int> takeCallbackTokens();

  void addPluginPanel(QWidget* panel);
  void closePluginPanels();

  const std::string& error() const;
  void setError(std::string error);
  void clearError();
};

} // namespace tb::ui
