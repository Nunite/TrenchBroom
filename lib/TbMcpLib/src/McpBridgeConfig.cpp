/*
 Copyright (C) 2026

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "mcp/McpBridgeConfig.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace tb::mcp
{
namespace
{

QString currentUserName()
{
  auto userName = qEnvironmentVariable("USERNAME");
  if (userName.isEmpty())
  {
    userName = qEnvironmentVariable("USER");
  }
  if (userName.isEmpty())
  {
    userName = QDir::home().dirName();
  }
  if (userName.isEmpty())
  {
    userName = "user";
  }

  userName.replace(QRegularExpression{"[^A-Za-z0-9_.-]"}, "_");
  return userName;
}

bool ensureParentDirectory(const QString& filePath, QString* error)
{
  const auto parentDir = QFileInfo{filePath}.absoluteDir();
  if (parentDir.exists())
  {
    return true;
  }

  if (!QDir{}.mkpath(parentDir.absolutePath()))
  {
    if (error)
    {
      *error = QString{"Could not create MCP config directory: %1"}.arg(
        parentDir.absolutePath());
    }
    return false;
  }

  return true;
}

} // namespace

QString defaultConfigDirectory()
{
  const auto appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const auto root = appData.isEmpty() ? QDir::homePath() + "/.trenchbroom" : appData;
  return QDir{root}.filePath("MCP");
}

QString defaultConfigPath()
{
  return QDir{defaultConfigDirectory()}.filePath("config.json");
}

QString generateBridgeToken()
{
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

McpBridgeConfig defaultBridgeConfig()
{
  return McpBridgeConfig{
    QString{"trenchbroom-mcp-%1"}.arg(currentUserName()),
    generateBridgeToken(),
    McpMode::Off,
  };
}

QJsonObject toJson(const McpBridgeConfig& config)
{
  return QJsonObject{
    {"pipeName", config.pipeName},
    {"token", config.token},
    {"mode", modeName(config.mode)},
  };
}

std::optional<McpBridgeConfig> bridgeConfigFromJson(
  const QJsonObject& json, QString* error)
{
  const auto pipeName = json.value("pipeName");
  if (!pipeName.isString() || pipeName.toString().trimmed().isEmpty())
  {
    if (error)
    {
      *error = "MCP pipeName is missing or empty";
    }
    return std::nullopt;
  }

  const auto token = json.value("token");
  if (!token.isString() || token.toString().trimmed().isEmpty())
  {
    if (error)
    {
      *error = "MCP token is missing or empty";
    }
    return std::nullopt;
  }

  const auto modeValue = json.value("mode");
  if (!modeValue.isString())
  {
    if (error)
    {
      *error = "MCP mode is missing or not a string";
    }
    return std::nullopt;
  }

  const auto mode = parseMode(modeValue.toString());
  if (!mode)
  {
    if (error)
    {
      *error = "MCP mode is unknown";
    }
    return std::nullopt;
  }

  return McpBridgeConfig{
    pipeName.toString(),
    token.toString(),
    *mode,
  };
}

std::optional<McpBridgeConfig> readBridgeConfig(const QString& filePath, QString* error)
{
  auto file = QFile{filePath};
  if (!file.open(QIODevice::ReadOnly))
  {
    if (error)
    {
      *error = QString{"Could not open MCP config: %1"}.arg(file.errorString());
    }
    return std::nullopt;
  }

  auto parseError = QJsonParseError{};
  const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
  {
    if (error)
    {
      *error = QString{"Could not parse MCP config: %1"}.arg(parseError.errorString());
    }
    return std::nullopt;
  }

  return bridgeConfigFromJson(document.object(), error);
}

bool writeBridgeConfig(
  const McpBridgeConfig& config, const QString& filePath, QString* error)
{
  if (!ensureParentDirectory(filePath, error))
  {
    return false;
  }

  auto file = QSaveFile{filePath};
  if (!file.open(QIODevice::WriteOnly))
  {
    if (error)
    {
      *error = QString{"Could not write MCP config: %1"}.arg(file.errorString());
    }
    return false;
  }

  file.write(QJsonDocument{toJson(config)}.toJson(QJsonDocument::Indented));
  if (!file.commit())
  {
    if (error)
    {
      *error = QString{"Could not commit MCP config: %1"}.arg(file.errorString());
    }
    return false;
  }

  return true;
}

std::optional<McpBridgeConfig> readOrCreateBridgeConfig(
  const QString& filePath, QString* error)
{
  if (QFileInfo::exists(filePath))
  {
    return readBridgeConfig(filePath, error);
  }

  auto config = defaultBridgeConfig();
  if (!writeBridgeConfig(config, filePath, error))
  {
    return std::nullopt;
  }
  return config;
}

} // namespace tb::mcp
