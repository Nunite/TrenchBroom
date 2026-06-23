/*
 Copyright (C) 2026

 This file is part of TrenchBroom.

 TrenchBroom is free software:
 * you can redistribute it and/or modify
 it under the terms of the GNU General Public
 * License as published by
 the Free Software Foundation, either version 3 of the License,
 * or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that
 * it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of

 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public
 * License for more details.

 You should have received a copy of the GNU General Public
 * License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include <QJsonArray>
#include <QJsonObject>

#include "McpBridgeServerTools.h"
#include "mcp/McpError.h"
#include "ui/Action.h"
#include "ui/ActionExecutionContext.h"
#include "ui/ActionManager.h"
#include "ui/AppController.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"

namespace tb::ui
{
namespace mcp = tb::mcp;
QJsonObject actionsListJson(AppController& appController)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  auto context = ActionExecutionContext{appController, mapWindow, nullptr};

  auto actions = QJsonArray{};
  for (const auto& [path, action] : appController.actionManager().actionsMap())
  {
    const auto enabled = action.enabled(context);
    auto actionJson = QJsonObject{
      {"id", pathAsGenericQString(path)},
      {"label", action.label()},
      {"enabled", enabled},
      {"menuAction", action.isMenuAction()},
      {"checkable", action.checkable()},
    };

    if (action.checkable())
    {
      actionJson.insert("checked", action.checked(context));
    }

    actions.push_back(actionJson);
  }

  return QJsonObject{
    {"actions", actions},
    {"count", actions.size()},
  };
}

McpBridgeToolResult actionExecuteResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  const auto actionId = params.value("actionId").toString().trimmed();

  if (actionId.isEmpty())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams, "action_execute requires actionId");
  }

  const auto actionPath = pathFromQString(actionId);
  const auto& actionsMap = appController.actionManager().actionsMap();
  const auto actionIt = actionsMap.find(actionPath);
  if (actionIt == std::end(actionsMap))
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams, QString{"Unknown action id: %1"}.arg(actionId));
  }

  auto context = ActionExecutionContext{appController, mapWindow, nullptr};
  const auto& action = actionIt->second;
  if (!action.enabled(context))
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden, QString{"Action is disabled: %1"}.arg(actionId));
  }

  action.execute(context);
  return McpBridgeToolResult::success(QJsonObject{
    {"actionId", actionId},
    {"executed", true},
  });
}


} // namespace tb::ui