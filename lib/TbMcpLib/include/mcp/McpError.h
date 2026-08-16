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

#pragma once

#include <QJsonObject>
#include <QString>

#include <optional>

namespace tb::mcp
{

enum class McpErrorCode
{
  Forbidden,
  ToolNotFound,
  InvalidRequest,
  InvalidParams,
  NoActiveDocument,
  InternalError,
};

struct McpError
{
  McpErrorCode code = McpErrorCode::InternalError;
  QString message;
  QJsonObject details{};
};

QString errorCodeName(McpErrorCode code);
std::optional<McpErrorCode> parseErrorCode(const QString& value);

QJsonObject toJson(const McpError& error);
std::optional<McpError> errorFromJson(const QJsonObject& json, QString* error = nullptr);

} // namespace tb::mcp
