#include "ui/python/PythonPluginManifest.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "ui/QPathUtils.h"

#include <algorithm>
#include <sstream>
#include <string>

namespace tb::ui
{
namespace
{
constexpr auto ManifestFileName = "trenchbroom-plugin.json";

std::optional<std::string> requiredString(
  const QJsonObject& object, const char* key, std::string& error)
{
  const auto value = object.value(QString::fromUtf8(key));
  if (!value.isString() || value.toString().isEmpty())
  {
    error = std::string{"Missing or invalid manifest field: "} + key;
    return std::nullopt;
  }
  return value.toString().toStdString();
}

std::string optionalString(const QJsonObject& object, const char* key)
{
  const auto value = object.value(QString::fromUtf8(key));
  return value.isString() ? value.toString().toStdString() : std::string{};
}

PythonPluginManifestResult errorResult(
  const std::filesystem::path& path, std::string message)
{
  return {std::nullopt, PythonPluginManifestError{path, std::move(message)}};
}

PythonPluginManifestResult parsePluginType(
  const std::filesystem::path& path, const QJsonObject& object, PythonPluginType& type)
{
  const auto value = object.value(QStringLiteral("pluginType"));
  if (value.isUndefined())
  {
    type = PythonPluginType::Script;
    return {};
  }
  if (!value.isString())
  {
    return errorResult(path, "Invalid manifest field: pluginType");
  }

  const auto pluginType = value.toString().toLower();
  if (pluginType == QStringLiteral("ui"))
  {
    type = PythonPluginType::Ui;
    return {};
  }
  if (pluginType == QStringLiteral("script"))
  {
    type = PythonPluginType::Script;
    return {};
  }

  return errorResult(path, "Unsupported Python plugin pluginType");
}
} // namespace

PythonPluginManifestResult loadPythonPluginManifest(
  const std::filesystem::path& directory)
{
  const auto path = directory / ManifestFileName;
  auto file = QFile{pathAsQString(path)};
  if (!file.exists())
  {
    return errorResult(path, "Plugin manifest not found");
  }
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    return errorResult(path, "Could not open plugin manifest");
  }

  auto parseError = QJsonParseError{};
  const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
  {
    return errorResult(path, parseError.errorString().toStdString());
  }

  const auto object = document.object();
  auto error = std::string{};
  auto id = requiredString(object, "id", error);
  if (!id)
  {
    return errorResult(path, error);
  }
  auto name = requiredString(object, "name", error);
  if (!name)
  {
    return errorResult(path, error);
  }
  auto version = requiredString(object, "version", error);
  if (!version)
  {
    return errorResult(path, error);
  }
  auto entry = requiredString(object, "entry", error);
  if (!entry)
  {
    return errorResult(path, error);
  }

  const auto apiVersionValue = object.value(QStringLiteral("apiVersion"));
  const auto apiVersion = apiVersionValue.isDouble() ? apiVersionValue.toInt() : 2;
  if (apiVersion != 2)
  {
    return errorResult(path, "Unsupported Python plugin apiVersion");
  }

  auto pluginType = PythonPluginType::Script;
  if (auto result = parsePluginType(path, object, pluginType); result.error)
  {
    return result;
  }

  auto manifest = PythonPluginManifest{};
  manifest.id = std::move(*id);
  manifest.name = std::move(*name);
  manifest.version = std::move(*version);
  manifest.apiVersion = apiVersion;
  manifest.pluginType = pluginType;
  manifest.entry = std::filesystem::path{std::move(*entry)};
  manifest.description = optionalString(object, "description");
  manifest.author = optionalString(object, "author");
  manifest.directory = directory;

  if (!std::filesystem::exists(manifest.directory / manifest.entry))
  {
    return errorResult(path, "Plugin entry script not found");
  }

  return {std::move(manifest), std::nullopt};
}

std::string pythonPluginTypeName(const PythonPluginType type)
{
  switch (type)
  {
  case PythonPluginType::Script:
    return "script";
  case PythonPluginType::Ui:
    return "ui";
  }
  return "script";
}

std::vector<std::filesystem::path> splitPythonPluginDirectories(const std::string& paths)
{
  auto result = std::vector<std::filesystem::path>{};
  auto stream = std::stringstream{paths};
  auto item = std::string{};
  while (std::getline(stream, item, '|'))
  {
    if (!item.empty())
    {
      result.emplace_back(item);
    }
  }
  return result;
}

std::string joinPythonPluginDirectories(const std::vector<std::filesystem::path>& paths)
{
  auto result = std::string{};
  for (const auto& path : paths)
  {
    if (!result.empty())
    {
      result += '|';
    }
    const auto pathString = path.u8string();
    result.append(reinterpret_cast<const char*>(pathString.c_str()), pathString.size());
  }
  return result;
}

} // namespace tb::ui
