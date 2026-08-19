/*
 Copyright (C) 2026 XiangXtreme

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

#include "McpToolRegistry.h"

#include "mcp/McpError.h"

#include <utility>

namespace tb::ui
{
namespace mcp = tb::mcp;

bool McpToolRegistry::registerHandler(
  QString toolName, McpBridgeServer::ToolHandler handler)
{
  const auto [it, inserted] = m_handlers.emplace(std::move(toolName), std::move(handler));
  Q_UNUSED(it);
  if (!inserted)
  {
    ++m_duplicateRegistrationCount;
  }
  return inserted;
}

McpBridgeToolResult McpToolRegistry::dispatch(
  const QString& toolName, const QJsonObject& params) const
{
  const auto it = m_handlers.find(toolName);
  if (it == m_handlers.end())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::ToolNotFound,
      QString{"MCP tool has no registered handler: %1"}.arg(toolName));
  }
  return it->second(toolName, params);
}

QStringList McpToolRegistry::toolNames() const
{
  auto result = QStringList{};
  result.reserve(static_cast<qsizetype>(m_handlers.size()));
  for (const auto& [name, handler] : m_handlers)
  {
    Q_UNUSED(handler);
    result.push_back(name);
  }
  return result;
}

int McpToolRegistry::duplicateRegistrationCount() const
{
  return m_duplicateRegistrationCount;
}

} // namespace tb::ui
