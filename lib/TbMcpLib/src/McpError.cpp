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

#include "mcp/McpError.h"

#include <QJsonValue>

namespace tb::mcp
{

QString errorCodeName(const McpErrorCode code)
{
  switch (code)
  {
  case McpErrorCode::Unauthorized:
    return "Unauthorized";
  case McpErrorCode::Forbidden:
    return "Forbidden";
  case McpErrorCode::ToolNotFound:
    return "ToolNotFound";
  case McpErrorCode::InvalidRequest:
    return "InvalidRequest";
  case McpErrorCode::InvalidParams:
    return "InvalidParams";
  case McpErrorCode::NoActiveDocument:
    return "NoActiveDocument";
  case McpErrorCode::InternalError:
    return "InternalError";
  }

  return "InternalError";
}

std::optional<McpErrorCode> parseErrorCode(const QString& value)
{
  if (value == "Unauthorized")
  {
    return McpErrorCode::Unauthorized;
  }
  if (value == "Forbidden")
  {
    return McpErrorCode::Forbidden;
  }
  if (value == "ToolNotFound")
  {
    return McpErrorCode::ToolNotFound;
  }
  if (value == "InvalidRequest")
  {
    return McpErrorCode::InvalidRequest;
  }
  if (value == "InvalidParams")
  {
    return McpErrorCode::InvalidParams;
  }
  if (value == "NoActiveDocument")
  {
    return McpErrorCode::NoActiveDocument;
  }
  if (value == "InternalError")
  {
    return McpErrorCode::InternalError;
  }

  return std::nullopt;
}

QJsonObject toJson(const McpError& error)
{
  auto json = QJsonObject{
    {"code", errorCodeName(error.code)},
    {"message", error.message},
  };
  if (!error.details.isEmpty())
  {
    json.insert("details", error.details);
  }
  return json;
}

std::optional<McpError> errorFromJson(const QJsonObject& json, QString* error)
{
  const auto codeValue = json.value("code");
  if (!codeValue.isString())
  {
    if (error)
    {
      *error = "MCP error code is missing or not a string";
    }
    return std::nullopt;
  }

  const auto code = parseErrorCode(codeValue.toString());
  if (!code)
  {
    if (error)
    {
      *error = "MCP error code is unknown";
    }
    return std::nullopt;
  }

  const auto messageValue = json.value("message");
  if (!messageValue.isString())
  {
    if (error)
    {
      *error = "MCP error message is missing or not a string";
    }
    return std::nullopt;
  }

  auto details = QJsonObject{};
  const auto detailsValue = json.value("details");
  if (detailsValue.isObject())
  {
    details = detailsValue.toObject();
  }

  return McpError{*code, messageValue.toString(), details};
}

} // namespace tb::mcp
