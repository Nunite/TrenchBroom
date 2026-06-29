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

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QUuid>

#include "McpBridgeServerTools.h"
#include "mcp/McpToolCatalog.h"
#include "mdl/Map.h"
#include "ui/QPathUtils.h"
#include "ui/mcp/McpBridgeServer.h"

#include <algorithm>

namespace tb::ui
{
namespace
{

mcp::McpBridgeResponse makeFailure(
  const mcp::McpBridgeRequest& request,
  const mcp::McpErrorCode code,
  const QString& message)
{
  return mcp::McpBridgeResponse::failure(request.id, mcp::McpError{code, message});
}

mcp::McpBridgeResponse makeFailure(
  const mcp::McpBridgeRequest& request,
  const mcp::McpErrorCode code,
  const QString& message,
  QJsonObject details)
{
  return mcp::McpBridgeResponse::failure(
    request.id, mcp::McpError{code, message, std::move(details)});
}

void syncOperationHistoryWithExternalResult(
  std::vector<McpOperationRecord>& history, const QJsonObject& result)
{
  const auto operationId = result.value("operationId").toString();
  if (operationId.isEmpty())
  {
    return;
  }

  const auto it = std::ranges::find_if(
    history, [&](const auto& operation) { return operation.operationId == operationId; });
  if (it == history.end())
  {
    auto operation = McpOperationRecord{};
    operation.operationId = operationId;
    operation.toolName = result.value("toolName").toString();
    operation.transactionName = result.value("transactionName").toString();
    operation.operationKind = result.value("operationKind").toString("mutation");
    operation.setChangedObjectIds(result.value("changedObjectIds").toArray());
    operation.setDeletedObjectIds(result.value("deletedObjectIds").toArray());
    operation.setSummary(result);
    history.push_back(std::move(operation));
    return;
  }

  const auto changedObjectIds = result.value("changedObjectIds").toArray();
  if (!changedObjectIds.isEmpty())
  {
    it->setChangedObjectIds(changedObjectIds);
  }
  const auto deletedObjectIds = result.value("deletedObjectIds").toArray();
  if (!deletedObjectIds.isEmpty())
  {
    it->setDeletedObjectIds(deletedObjectIds);
  }
  it->setSummary(result);
}

} // namespace

McpOperationRecord::McpOperationRecord()
{
  const auto now = QDateTime::currentDateTimeUtc();
  createdAt = now.toString(Qt::ISODateWithMs);
  createdAtMs = now.toMSecsSinceEpoch();
  operationKind = "mutation";
}

void McpOperationRecord::setChangedObjectIds(const QJsonArray& ids)
{
  changedObjectIds.clear();
  changedObjectIds.reserve(ids.size());
  for (const auto& value : ids)
  {
    if (value.isString())
    {
      changedObjectIds.push_back(value.toString());
    }
  }
}

void McpOperationRecord::setDeletedObjectIds(const QJsonArray& ids)
{
  deletedObjectIds.clear();
  deletedObjectIds.reserve(ids.size());
  for (const auto& value : ids)
  {
    if (value.isString())
    {
      deletedObjectIds.push_back(value.toString());
    }
  }
}

QJsonArray McpOperationRecord::changedObjectIdsJson() const
{
  auto result = QJsonArray{};
  for (const auto& id : changedObjectIds)
  {
    result.push_back(id);
  }
  return result;
}

QJsonArray McpOperationRecord::deletedObjectIdsJson() const
{
  auto result = QJsonArray{};
  for (const auto& id : deletedObjectIds)
  {
    result.push_back(id);
  }
  return result;
}

void McpOperationRecord::setSummary(const QJsonObject& value)
{
  summaryJson = QJsonDocument{value}.toJson(QJsonDocument::Compact);
}

QJsonObject McpOperationRecord::summary() const
{
  const auto document = QJsonDocument::fromJson(summaryJson);
  return document.isObject() ? document.object() : QJsonObject{};
}

void McpOperationRecord::setDetail(const QJsonObject& value)
{
  detailJson = QJsonDocument{value}.toJson(QJsonDocument::Compact);
}

QJsonObject McpOperationRecord::detail() const
{
  const auto document = QJsonDocument::fromJson(detailJson);
  return document.isObject() ? document.object() : QJsonObject{};
}

bool McpBridgeServer::start(const mcp::McpBridgeConfig& config, QString* error)
{
  stop();
  m_config = config;
  m_bridgeInstanceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_bridgeStartedAtUtc = QDateTime::currentDateTimeUtc();

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
  m_overlayState = QJsonObject{};
  m_operationHistory.clear();
  m_brushMetadata.clear();
  m_modules.clear();
  m_objectRegistry.clear();
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

const QJsonObject& McpBridgeServer::overlayState() const
{
  return m_overlayState;
}

namespace
{

QJsonObject resourceObject(
  const McpOperationRecord& operation, const std::optional<QJsonObject>& liveState)
{
  auto result = QJsonObject{
    {"operationId", operation.operationId},
    {"toolName", operation.toolName},
    {"transactionName", operation.transactionName},
    {"operationKind", operation.operationKind},
    {"createdAt", operation.createdAt},
    {"createdAtMs", operation.createdAtMs},
    {"changedObjectCount", operation.changedObjectIds.size()},
    {"deletedObjectCount", operation.deletedObjectIds.size()},
    {"undone", operation.undone},
    {"summary", operation.summary()},
    {"detail", operation.detail()},
  };
  result.insert(
    "idsDetail",
    "compact; use operation_inspect(detail=ids) or operation_inspect(detail=full) "
    "for changedObjectIds/deletedObjectIds");
  if (liveState)
  {
    for (auto it = liveState->begin(); it != liveState->end(); ++it)
    {
      result.insert(it.key(), it.value());
    }
  }
  return result;
}

} // namespace

std::optional<QJsonObject> McpBridgeServer::readResource(const QString& uri) const
{
  static const auto Prefix = QString{"tbmcp://operation/"};
  if (!uri.startsWith(Prefix))
  {
    return std::nullopt;
  }

  const auto operationId = uri.mid(Prefix.size());
  const auto it = std::ranges::find_if(m_operationHistory, [&](const auto& operation) {
    return operation.operationId == operationId;
  });
  if (it == m_operationHistory.end())
  {
    return std::nullopt;
  }

  if (m_activeMapProvider)
  {
    if (auto* map = m_activeMapProvider())
    {
      return m_objectRegistry.externalizeResult(
        *map,
        resourceObject(
          *it, m_objectRegistry.liveStateJson(*map, it->changedObjectIds, it->undone)));
    }
  }

  return resourceObject(*it, std::nullopt);
}

mcp::McpBridgeResponse McpBridgeServer::dispatchRequest(
  const mcp::McpBridgeRequest& request) const
{
  if (m_dispatchInProgress)
  {
    return makeFailure(
      request,
      mcp::McpErrorCode::Forbidden,
      "MCP bridge is already handling another request; retry after it finishes");
  }

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

  if (!tool->implemented)
  {
    return makeFailure(
      request,
      mcp::McpErrorCode::ToolNotFound,
      QString{"MCP tool is registered but not implemented yet: %1"}.arg(request.tool));
  }

  if (!mcp::canCallTool(*tool, m_config.mode))
  {
    return makeFailure(
      request,
      mcp::McpErrorCode::Forbidden,
      QString{"MCP tool is not available in mode %1"}.arg(mcp::modeName(m_config.mode)));
  }

  struct DispatchGuard
  {
    bool& dispatchInProgress;

    explicit DispatchGuard(bool& i_dispatchInProgress)
      : dispatchInProgress{i_dispatchInProgress}
    {
      dispatchInProgress = true;
    }

    ~DispatchGuard() { dispatchInProgress = false; }
  };

  const auto dispatchGuard = DispatchGuard{m_dispatchInProgress};

  auto params = request.params;
  auto* map = m_activeMapProvider ? m_activeMapProvider() : nullptr;
  if (tool->mutatesDocument)
  {
    const auto expectedDocumentPath =
      params.value("expectedDocumentPath").toString().trimmed();
    if (!expectedDocumentPath.isEmpty())
    {
      const auto actualDocumentPath =
        map != nullptr && !map->path().empty() ? pathAsQString(map->path()) : QString{};
      if (actualDocumentPath != expectedDocumentPath)
      {
        return makeFailure(
          request,
          mcp::McpErrorCode::Forbidden,
          QString{"Active document does not match expectedDocumentPath. Expected '%1', "
                  "actual '%2'."}
            .arg(expectedDocumentPath, actualDocumentPath),
          QJsonObject{
            {"expectedDocumentPath", expectedDocumentPath},
            {"actualDocumentPath", actualDocumentPath},
            {"processId", static_cast<int>(QCoreApplication::applicationPid())},
            {"bridgeInstanceId", m_bridgeInstanceId},
            {"bridgeStartedAt", m_bridgeStartedAtUtc.toString(Qt::ISODateWithMs)},
            {"httpPort", static_cast<int>(m_config.httpPort)},
          });
      }
    }
  }
  if (map != nullptr)
  {
    auto error = QString{};
    const auto internalParams = m_objectRegistry.internalizeParams(*map, params, error);
    if (!internalParams)
    {
      return makeFailure(request, mcp::McpErrorCode::InvalidParams, error);
    }
    params = *internalParams;
  }

  const auto result = m_toolHandler(request.tool, params);
  if (result.ok)
  {
    auto* resultMap = m_activeMapProvider ? m_activeMapProvider() : nullptr;
    if (resultMap != nullptr)
    {
      auto externalResult = m_objectRegistry.externalizeResult(*resultMap, result.result);
      syncOperationHistoryWithExternalResult(m_operationHistory, externalResult);
      return mcp::McpBridgeResponse::success(request.id, std::move(externalResult));
    }
    return mcp::McpBridgeResponse::success(request.id, result.result);
  }
  return mcp::McpBridgeResponse::failure(request.id, result.error);
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
