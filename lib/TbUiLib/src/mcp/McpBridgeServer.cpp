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

#include "ui/mcp/McpBridgeServer.h"

#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>

#include "mcp/McpError.h"
#include "mcp/McpToolCatalog.h"
#include "mdl/Brush.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/GameInfo.h"
#include "mdl/Grid.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Selection.h"
#include "mdl/Node.h"
#include "mdl/PatchNode.h"
#include "mdl/Selection.h"
#include "mdl/WorldNode.h"
#include "ui/Action.h"
#include "ui/ActionExecutionContext.h"
#include "ui/ActionManager.h"
#include "ui/AppController.h"
#include "ui/GetVersion.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"

namespace tb::ui
{
namespace mcp = tb::mcp;

namespace
{

QJsonArray vecToJson(const vm::vec3d& value)
{
  return QJsonArray{
    value.x(),
    value.y(),
    value.z(),
  };
}

QJsonObject boundsToJson(const vm::bbox3d& bounds)
{
  return QJsonObject{
    {"min", vecToJson(bounds.min)},
    {"max", vecToJson(bounds.max)},
  };
}

QString pathToQString(const std::filesystem::path& path)
{
  return path.empty() ? QString{} : pathAsQString(path);
}

QString nodePathId(const mdl::Node& node, const mdl::WorldNode& worldNode)
{
  if (&node == &worldNode)
  {
    return "node:world";
  }

  auto parts = QStringList{};
  for (const auto index : node.pathFrom(worldNode).indices)
  {
    parts.push_back(QString::number(index));
  }
  return QString{"node:%1"}.arg(parts.join('/'));
}

std::optional<mdl::NodePath> parseNodePathId(const QString& id)
{
  if (id == "node:world")
  {
    return mdl::NodePath{};
  }

  static const auto Prefix = QString{"node:"};
  if (!id.startsWith(Prefix))
  {
    return std::nullopt;
  }

  auto path = mdl::NodePath{};
  for (const auto& part : id.mid(Prefix.size()).split('/', Qt::SkipEmptyParts))
  {
    auto ok = false;
    const auto index = part.toULongLong(&ok);
    if (!ok)
    {
      return std::nullopt;
    }
    path.indices.push_back(static_cast<std::size_t>(index));
  }
  return path;
}

mdl::Node* resolveNodeId(mdl::WorldNode& worldNode, const QString& id)
{
  const auto path = parseNodePathId(id);
  if (!path)
  {
    return nullptr;
  }
  return worldNode.resolvePath(*path);
}

QString nodeTypeName(const mdl::Node& node)
{
  if (dynamic_cast<const mdl::WorldNode*>(&node) != nullptr)
  {
    return "world";
  }
  if (dynamic_cast<const mdl::LayerNode*>(&node) != nullptr)
  {
    return "layer";
  }
  if (dynamic_cast<const mdl::GroupNode*>(&node) != nullptr)
  {
    return "group";
  }
  if (dynamic_cast<const mdl::EntityNode*>(&node) != nullptr)
  {
    return "entity";
  }
  if (dynamic_cast<const mdl::BrushNode*>(&node) != nullptr)
  {
    return "brush";
  }
  if (dynamic_cast<const mdl::PatchNode*>(&node) != nullptr)
  {
    return "patch";
  }
  return "node";
}

QJsonObject nodeSummaryJson(const mdl::Node& node, const mdl::WorldNode& worldNode)
{
  auto result = QJsonObject{
    {"id", nodePathId(node, worldNode)},
    {"type", nodeTypeName(node)},
    {"name", QString::fromStdString(node.name())},
    {"selected", node.selected()},
    {"childCount", static_cast<int>(node.childCount())},
    {"descendantCount", static_cast<int>(node.descendantCount())},
    {"logicalBounds", boundsToJson(node.logicalBounds())},
  };

  if (const auto* nodeAsWorld = dynamic_cast<const mdl::WorldNode*>(&node))
  {
    result.insert("classname", QString::fromStdString(nodeAsWorld->entity().classname()));
  }
  else if (const auto* entityNode = dynamic_cast<const mdl::EntityNode*>(&node))
  {
    result.insert("classname", QString::fromStdString(entityNode->entity().classname()));
  }
  else if (const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(&node))
  {
    result.insert("faceCount", static_cast<int>(brushNode->brush().faceCount()));
  }

  return result;
}

void collectMapCounts(const mdl::Node& node, int& entities, int& brushes, int& patches)
{
  if (dynamic_cast<const mdl::EntityNode*>(&node) != nullptr)
  {
    ++entities;
  }
  else if (dynamic_cast<const mdl::BrushNode*>(&node) != nullptr)
  {
    ++brushes;
  }
  else if (dynamic_cast<const mdl::PatchNode*>(&node) != nullptr)
  {
    ++patches;
  }

  for (const auto* child : node.children())
  {
    collectMapCounts(*child, entities, brushes, patches);
  }
}

QJsonObject documentJson(const MapWindow& mapWindow, const int index)
{
  const auto& map = mapWindow.document().map();
  return QJsonObject{
    {"index", index},
    {"fileName", QString::fromStdString(map.filename())},
    {"path", pathToQString(map.path())},
    {"persistent", map.persistent()},
    {"modified", map.modified()},
    {"game", QString::fromStdString(map.gameInfo().gameConfig.name)},
    {"mapFormat", QString::fromStdString(mdl::formatName(map.worldNode().mapFormat()))},
  };
}

QJsonObject activeDocumentJson(AppController& appController)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return {};
  }

  return documentJson(*mapWindow, 0);
}

QJsonObject mapSnapshotJson(AppController& appController)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return {};
  }

  const auto& map = mapWindow->document().map();
  const auto& worldNode = map.worldNode();
  const auto& grid = map.grid();

  auto entities = 0;
  auto brushes = 0;
  auto patches = 0;
  collectMapCounts(worldNode, entities, brushes, patches);

  auto worldspawn = QJsonObject{};
  for (const auto& property : worldNode.entity().properties())
  {
    worldspawn.insert(
      QString::fromStdString(property.key()), QString::fromStdString(property.value()));
  }

  return QJsonObject{
    {"document", documentJson(*mapWindow, 0)},
    {"world", nodeSummaryJson(worldNode, worldNode)},
    {"worldspawn", worldspawn},
    {"entityCount", entities},
    {"brushCount", brushes},
    {"patchCount", patches},
    {"nodeCount", static_cast<int>(worldNode.descendantCount() + 1)},
    {"bounds", boundsToJson(worldNode.logicalBounds())},
    {"grid",
     QJsonObject{
       {"size", grid.size()},
       {"actualSize", grid.actualSize()},
       {"snap", grid.snap()},
       {"visible", grid.visible()},
     }},
  };
}

bool textMatches(const QString& text, const QString& query)
{
  return text.contains(query, Qt::CaseInsensitive);
}

bool nodeMatchesQuery(
  const mdl::Node& node, const mdl::WorldNode& worldNode, const QString& query)
{
  if (
    textMatches(nodePathId(node, worldNode), query)
    || textMatches(nodeTypeName(node), query)
    || textMatches(QString::fromStdString(node.name()), query))
  {
    return true;
  }

  if (const auto* entityNode = dynamic_cast<const mdl::EntityNodeBase*>(&node))
  {
    if (textMatches(QString::fromStdString(entityNode->entity().classname()), query))
    {
      return true;
    }

    for (const auto& property : entityNode->entity().properties())
    {
      if (
        textMatches(QString::fromStdString(property.key()), query)
        || textMatches(QString::fromStdString(property.value()), query))
      {
        return true;
      }
    }
  }

  return false;
}

void collectSearchResults(
  const mdl::Node& node,
  const mdl::WorldNode& worldNode,
  const QString& query,
  QJsonArray& results)
{
  if (nodeMatchesQuery(node, worldNode, query))
  {
    results.push_back(nodeSummaryJson(node, worldNode));
  }

  for (const auto* child : node.children())
  {
    collectSearchResults(*child, worldNode, query, results);
  }
}

QJsonObject mapSearchJson(AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  const auto query = params.value("query").toString().trimmed();

  if (!mapWindow || query.isEmpty())
  {
    return QJsonObject{
      {"query", query},
      {"results", QJsonArray{}},
      {"count", 0},
    };
  }

  const auto& worldNode = mapWindow->document().map().worldNode();
  auto results = QJsonArray{};
  collectSearchResults(worldNode, worldNode, query, results);

  return QJsonObject{
    {"query", query},
    {"results", results},
    {"count", results.size()},
  };
}

QJsonObject selectionJson(AppController& appController)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return {};
  }

  const auto& map = mapWindow->document().map();
  const auto& worldNode = map.worldNode();
  const auto& selection = map.selection();

  auto nodes = QJsonArray{};
  for (const auto* node : selection.nodes)
  {
    nodes.push_back(nodeSummaryJson(*node, worldNode));
  }

  return QJsonObject{
    {"hasSelection", selection.hasAny()},
    {"nodes", nodes},
    {"nodeCount", static_cast<int>(selection.nodes.size())},
    {"groupCount", static_cast<int>(selection.groups.size())},
    {"entityCount", static_cast<int>(selection.entities.size())},
    {"brushCount", static_cast<int>(selection.brushes.size())},
    {"patchCount", static_cast<int>(selection.patches.size())},
    {"brushFaceCount", static_cast<int>(selection.brushFaces.size())},
  };
}

McpBridgeToolResult selectionSetResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::NoActiveDocument, "No active document");
  }

  const auto objectIdsValue = params.value("objectIds");
  if (!objectIdsValue.isArray())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams, "selection_set requires objectIds array");
  }

  auto& map = mapWindow->document().map();
  auto& worldNode = map.worldNode();
  auto nodes = std::vector<mdl::Node*>{};

  for (const auto& objectIdValue : objectIdsValue.toArray())
  {
    if (!objectIdValue.isString())
    {
      return McpBridgeToolResult::failure(
        mcp::McpErrorCode::InvalidParams, "objectIds must contain only strings");
    }

    const auto objectId = objectIdValue.toString();
    auto* node = resolveNodeId(worldNode, objectId);
    if (!node)
    {
      return McpBridgeToolResult::failure(
        mcp::McpErrorCode::InvalidParams,
        QString{"Unknown MCP object id: %1"}.arg(objectId));
    }
    if (!map.editorContext().selectable(*node))
    {
      return McpBridgeToolResult::failure(
        mcp::McpErrorCode::InvalidParams,
        QString{"MCP object id is not selectable: %1"}.arg(objectId));
    }
    nodes.push_back(node);
  }

  mdl::deselectAll(map);
  if (!nodes.empty())
  {
    mdl::selectNodes(map, nodes);
  }

  auto selectedIds = QJsonArray{};
  for (const auto* node : nodes)
  {
    selectedIds.push_back(nodePathId(*node, worldNode));
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"selectedObjectIds", selectedIds},
    {"selectedCount", selectedIds.size()},
  });
}

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

QJsonObject documentsListJson(AppController& appController)
{
  auto documents = QJsonArray{};
  auto index = 0;
  for (const auto* mapWindow : appController.mapWindowManager().mapWindows())
  {
    documents.push_back(documentJson(*mapWindow, index));
    ++index;
  }

  return QJsonObject{
    {"documents", documents},
    {"count", documents.size()},
  };
}

QJsonObject makeStatus(AppController& appController, const mcp::McpBridgeConfig& config)
{
  auto result = QJsonObject{
    {"application", "TrenchBroom"},
    {"version", getBuildVersion()},
    {"mode", mcp::modeName(config.mode)},
    {"pipeName", config.pipeName},
    {"documentCount",
     static_cast<int>(appController.mapWindowManager().mapWindows().size())},
    {"activeDocument", false},
  };

  if (auto* mapWindow = appController.mapWindowManager().topMapWindow())
  {
    const auto& map = mapWindow->document().map();
    result.insert("activeDocument", true);
    result.insert("activeDocumentFileName", QString::fromStdString(map.filename()));
    result.insert("activeDocumentPath", pathAsQString(map.path()));
    result.insert("activeDocumentModified", map.modified());
  }

  return result;
}

QJsonObject doctorJson(AppController& appController, const mcp::McpBridgeConfig& config)
{
  const auto implementedTools = mcp::toolsListJson(config.mode);
  return QJsonObject{
    {"configPath", mcp::defaultConfigPath()},
    {"pipeName", config.pipeName},
    {"mode", mcp::modeName(config.mode)},
    {"tokenPresent", !config.token.isEmpty()},
    {"listening", config.mode != mcp::McpMode::Off},
    {"documentCount",
     static_cast<int>(appController.mapWindowManager().mapWindows().size())},
    {"activeDocument", appController.mapWindowManager().topMapWindow() != nullptr},
    {"implementedToolCount", implementedTools.size()},
    {"implementedTools", implementedTools},
  };
}

mcp::McpBridgeResponse makeFailure(
  const mcp::McpBridgeRequest& request,
  const mcp::McpErrorCode code,
  const QString& message)
{
  return mcp::McpBridgeResponse::failure(request.id, mcp::McpError{code, message});
}

} // namespace

McpBridgeToolResult McpBridgeToolResult::success(QJsonObject result)
{
  return McpBridgeToolResult{true, std::move(result), {}};
}

McpBridgeToolResult McpBridgeToolResult::failure(
  const mcp::McpErrorCode code, QString message)
{
  return McpBridgeToolResult{false, {}, mcp::McpError{code, std::move(message)}};
}

McpBridgeServer::McpBridgeServer(AppController& appController, QObject* parent)
  : McpBridgeServer{
      [&appController, this](const auto& toolName, const auto& params) {
        if (toolName == "tb_status")
        {
          return McpBridgeToolResult::success(makeStatus(appController, m_config));
        }
        if (toolName == "tb_doctor")
        {
          auto doctor = doctorJson(appController, m_config);
          doctor.insert("overlay", m_overlayState);
          return McpBridgeToolResult::success(std::move(doctor));
        }
        if (toolName == "documents_list")
        {
          return McpBridgeToolResult::success(documentsListJson(appController));
        }
        if (toolName == "document_snapshot")
        {
          return McpBridgeToolResult::success(activeDocumentJson(appController));
        }
        if (toolName == "map_snapshot")
        {
          return McpBridgeToolResult::success(mapSnapshotJson(appController));
        }
        if (toolName == "map_search")
        {
          return McpBridgeToolResult::success(mapSearchJson(appController, params));
        }
        if (toolName == "selection_get")
        {
          return McpBridgeToolResult::success(selectionJson(appController));
        }
        if (toolName == "selection_set")
        {
          return selectionSetResult(appController, params);
        }
        if (toolName == "actions_list")
        {
          return McpBridgeToolResult::success(actionsListJson(appController));
        }
        if (toolName == "action_execute")
        {
          return actionExecuteResult(appController, params);
        }
        if (toolName == "overlay_set")
        {
          m_overlayState = params;
          return McpBridgeToolResult::success(QJsonObject{
            {"overlay", m_overlayState},
            {"active", true},
          });
        }
        if (toolName == "overlay_clear")
        {
          m_overlayState = QJsonObject{};
          return McpBridgeToolResult::success(QJsonObject{
            {"overlay", m_overlayState},
            {"active", false},
          });
        }
        return McpBridgeToolResult::failure(
          mcp::McpErrorCode::ToolNotFound,
          QString{"MCP tool is registered but not wired yet: %1"}.arg(toolName));
      },
      parent}
{
}

McpBridgeServer::McpBridgeServer(ToolHandler toolHandler, QObject* parent)
  : QObject{parent}
  , m_toolHandler{std::move(toolHandler)}
{
}

McpBridgeServer::~McpBridgeServer()
{
  stop();
}

bool McpBridgeServer::start(const mcp::McpBridgeConfig& config, QString* error)
{
  stop();
  m_config = config;

  if (m_config.mode == mcp::McpMode::Off)
  {
    return true;
  }

  m_server = std::make_unique<QLocalServer>();
  connect(
    m_server.get(),
    &QLocalServer::newConnection,
    this,
    &McpBridgeServer::handleNewConnection);

  QLocalServer::removeServer(m_config.pipeName);
  if (!m_server->listen(m_config.pipeName))
  {
    if (error)
    {
      *error = m_server->errorString();
    }
    m_server.reset();
    return false;
  }

  return true;
}

void McpBridgeServer::stop()
{
  if (m_server)
  {
    m_server->close();
    QLocalServer::removeServer(m_config.pipeName);
    m_server.reset();
  }
}

bool McpBridgeServer::isListening() const
{
  return m_server != nullptr && m_server->isListening();
}

QString McpBridgeServer::pipeName() const
{
  return m_config.pipeName;
}

mcp::McpMode McpBridgeServer::mode() const
{
  return m_config.mode;
}

mcp::McpBridgeResponse McpBridgeServer::dispatchRequest(
  const mcp::McpBridgeRequest& request) const
{
  if (request.token != m_config.token)
  {
    return makeFailure(
      request, mcp::McpErrorCode::Unauthorized, "Invalid MCP bridge token");
  }

  const auto tool = mcp::findToolDefinition(request.tool);
  if (!tool)
  {
    return makeFailure(
      request,
      mcp::McpErrorCode::ToolNotFound,
      QString{"Unknown MCP tool: %1"}.arg(request.tool));
  }

  if (!mcp::canCallTool(*tool, m_config.mode))
  {
    return makeFailure(
      request,
      mcp::McpErrorCode::Forbidden,
      QString{"MCP tool is not available in mode %1"}.arg(mcp::modeName(m_config.mode)));
  }

  if (
    request.tool == "tb_status" || request.tool == "tb_doctor"
    || request.tool == "documents_list" || request.tool == "document_snapshot"
    || request.tool == "map_snapshot" || request.tool == "map_search"
    || request.tool == "selection_get" || request.tool == "selection_set"
    || request.tool == "actions_list" || request.tool == "action_execute"
    || request.tool == "overlay_set" || request.tool == "overlay_clear")
  {
    const auto result = m_toolHandler(request.tool, request.params);
    if (result.ok)
    {
      return mcp::McpBridgeResponse::success(request.id, result.result);
    }
    return mcp::McpBridgeResponse::failure(request.id, result.error);
  }

  return makeFailure(
    request,
    mcp::McpErrorCode::ToolNotFound,
    QString{"MCP tool is registered but not wired yet: %1"}.arg(request.tool));
}

void McpBridgeServer::handleNewConnection()
{
  while (auto* socket = m_server->nextPendingConnection())
  {
    socket->setParent(this);
    connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
      handleSocketReadyRead(*socket);
    });
    connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
  }
}

void McpBridgeServer::handleSocketReadyRead(QLocalSocket& socket)
{
  while (socket.canReadLine())
  {
    auto parseError = QJsonParseError{};
    const auto document =
      QJsonDocument::fromJson(socket.readLine().trimmed(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
      writeResponse(
        socket,
        mcp::McpBridgeResponse::failure(
          {},
          mcp::McpError{
            mcp::McpErrorCode::InvalidRequest, "Invalid MCP bridge JSON request"}));
      continue;
    }

    auto error = QString{};
    const auto request = mcp::bridgeRequestFromJson(document.object(), &error);
    if (!request)
    {
      writeResponse(
        socket,
        mcp::McpBridgeResponse::failure(
          {}, mcp::McpError{mcp::McpErrorCode::InvalidRequest, error}));
      continue;
    }

    writeResponse(socket, dispatchRequest(*request));
  }
}

void McpBridgeServer::writeResponse(
  QLocalSocket& socket, const mcp::McpBridgeResponse& response) const
{
  socket.write(QJsonDocument{mcp::toJson(response)}.toJson(QJsonDocument::Compact));
  socket.write("\n");
  socket.flush();
}

} // namespace tb::ui
