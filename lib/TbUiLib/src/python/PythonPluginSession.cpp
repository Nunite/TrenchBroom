#include "ui/python/PythonPluginSession.h"

#include <QWidget>

#include <utility>

namespace tb::ui
{

PythonPluginSession::PythonPluginSession(
  PythonPluginManifest manifest, PythonExecutionContext context)
  : m_manifest{std::move(manifest)}
  , m_context{std::move(context)}
{
  m_context.pluginId = m_manifest.id;
  m_context.pluginDirectory = m_manifest.directory;
  m_context.scriptPath = m_manifest.directory / m_manifest.entry;
}

const PythonPluginManifest& PythonPluginSession::manifest() const
{
  return m_manifest;
}

const std::string& PythonPluginSession::pluginId() const
{
  return m_manifest.id;
}

PythonExecutionContext& PythonPluginSession::context()
{
  return m_context;
}

const PythonExecutionContext& PythonPluginSession::context() const
{
  return m_context;
}

void PythonPluginSession::addCallbackToken(const int token)
{
  m_callbackTokens.push_back(token);
}

std::vector<int> PythonPluginSession::takeCallbackTokens()
{
  auto result = std::move(m_callbackTokens);
  m_callbackTokens.clear();
  return result;
}

void PythonPluginSession::addPluginPanel(QWidget* panel)
{
  if (panel != nullptr)
  {
    m_pluginPanels.push_back(QPointer<QWidget>{panel});
  }
}

void PythonPluginSession::closePluginPanels()
{
  for (const auto& panel : m_pluginPanels)
  {
    if (panel != nullptr)
    {
      panel->close();
      panel->deleteLater();
    }
  }
  m_pluginPanels.clear();
}

const std::string& PythonPluginSession::error() const
{
  return m_error;
}

void PythonPluginSession::setError(std::string error)
{
  m_error = std::move(error);
}

void PythonPluginSession::clearError()
{
  m_error.clear();
}

} // namespace tb::ui
