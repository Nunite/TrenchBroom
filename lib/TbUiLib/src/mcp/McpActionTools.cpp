/*
 Copyright (C) 2026 XiangXtreme

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
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"

namespace tb::ui
{
namespace mcp = tb::mcp;
namespace
{
struct ActiveActionContext
{
  MapWindow* mapWindow = nullptr;
  MapViewBase* mapView = nullptr;
};

ActiveActionContext activeActionContext(AppController& appController)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  auto* mapView = mapWindow ? mapWindow->currentMapViewBase() : nullptr;
  return {mapWindow, mapView};
}
} // namespace

QJsonObject actionsListJson(AppController& appController)
{
  const auto activeContext = activeActionContext(appController);
  const auto hasActiveView = activeContext.mapWindow && activeContext.mapView;
  auto context = ActionExecutionContext{
    appController,
    hasActiveView ? activeContext.mapWindow : nullptr,
    hasActiveView ? activeContext.mapView : nullptr};

  auto actions = QJsonArray{};
  for (const auto& [path, action] : appController.actionManager().actionsMap())
  {
    const auto enabled = action.enabled(context);
    auto actionJson = QJsonObject{
      {"id", pathAsGenericQString(path)},
      {"label", QString::fromStdString(action.label())},
      {"enabled", enabled},
      {"menuAction", action.isMenuAction()},
      {"checkable", action.checkable()},
    };

    if (action.checkable())
    {
      actionJson.insert("checked", enabled ? action.checked(context) : false);
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

  const auto activeContext = activeActionContext(appController);
  if (activeContext.mapWindow && !activeContext.mapView)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden, "No active map view");
  }

  auto context =
    ActionExecutionContext{appController, activeContext.mapWindow, activeContext.mapView};
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
