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

#include <QJsonArray>
#include <QJsonObject>

#include "McpBridgeServerTools.h"
#include "mcp/McpError.h"
#include "mdl/Map.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"

#include <algorithm>
#include <vector>

namespace tb::ui
{
namespace mcp = tb::mcp;

namespace
{

QJsonObject operationRecordJson(const McpOperationRecord& operation)
{
  auto result = QJsonObject{};
  result.insert("operationId", operation.operationId);
  result.insert("toolName", operation.toolName);
  result.insert("transactionName", operation.transactionName);
  result.insert("changedObjectIds", operation.changedObjectIds);
  result.insert("undone", operation.undone);
  return result;
}

QJsonArray operationHistoryJson(const std::vector<McpOperationRecord>& history)
{
  auto result = QJsonArray{};
  for (const auto& operation : history)
  {
    result.push_back(operationRecordJson(operation));
  }
  return result;
}

} // namespace

McpBridgeToolResult historyListResult(const std::vector<McpOperationRecord>& history)
{
  return McpBridgeToolResult::success(QJsonObject{
    {"operations", operationHistoryJson(history)},
    {"count", static_cast<int>(history.size())},
  });
}

McpBridgeToolResult historyUndoResult(
  AppController& appController, std::vector<McpOperationRecord>& history)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto it = std::find_if(history.rbegin(), history.rend(), [](const auto& operation) {
    return !operation.undone;
  });
  if (it == history.rend())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden, "No MCP operation is available to undo");
  }

  auto& map = mapWindow->document().map();
  const auto* undoName = map.undoCommandName();
  if (!undoName || QString::fromStdString(*undoName) != it->transactionName)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden,
      "The latest MCP operation is no longer on top of the undo stack");
  }

  map.undoCommand();
  it->undone = true;
  return McpBridgeToolResult::success(QJsonObject{
    {"operation", operationRecordJson(*it)},
    {"undone", true},
  });
}

McpBridgeToolResult historyRedoResult(
  AppController& appController, std::vector<McpOperationRecord>& history)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto it = std::find_if(history.begin(), history.end(), [](const auto& operation) {
    return operation.undone;
  });
  if (it == history.end())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden, "No MCP operation is available to redo");
  }

  auto& map = mapWindow->document().map();
  const auto* redoName = map.redoCommandName();
  if (!redoName || QString::fromStdString(*redoName) != it->transactionName)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden,
      "The MCP operation is no longer on top of the redo stack");
  }

  map.redoCommand();
  it->undone = false;
  return McpBridgeToolResult::success(QJsonObject{
    {"operation", operationRecordJson(*it)},
    {"redone", true},
  });
}

} // namespace tb::ui
