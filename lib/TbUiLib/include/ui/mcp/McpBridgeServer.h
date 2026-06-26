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

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>

#include "mcp/McpBridgeConfig.h"
#include "mcp/McpBridgeMessages.h"
#include "mcp/McpError.h"
#include "ui/mcp/McpObjectRegistry.h"

#include <functional>
#include <map>
#include <memory>
#include <vector>

class QLocalServer;
class QLocalSocket;

namespace tb::mdl
{
class Map;
}

namespace tb::ui
{
namespace mcp = tb::mcp;

class AppController;

struct McpBridgeToolResult
{
  bool ok = true;
  QJsonObject result;
  mcp::McpError error;

  static McpBridgeToolResult success(QJsonObject result = {});
  static McpBridgeToolResult failure(mcp::McpErrorCode code, QString message);
};

struct McpOperationRecord
{
  QString operationId;
  QString toolName;
  QString transactionName;
  QStringList changedObjectIds;
  QString createdAt;
  qint64 createdAtMs = 0;
  QByteArray summaryJson;
  QByteArray detailJson;
  bool undone = false;

  McpOperationRecord();
  void setChangedObjectIds(const QJsonArray& ids);
  QJsonArray changedObjectIdsJson() const;
  void setSummary(const QJsonObject& value);
  QJsonObject summary() const;
  void setDetail(const QJsonObject& value);
  QJsonObject detail() const;
};

struct McpBrushMetadataRecord
{
  QString objectId;
  QJsonObject metadata;
  bool stale = false;
};

class McpBridgeServer : public QObject
{
  Q_OBJECT
public:
  using ToolHandler =
    std::function<McpBridgeToolResult(const QString&, const QJsonObject&)>;
  using ActiveMapProvider = std::function<mdl::Map*()>;

private:
  mcp::McpBridgeConfig m_config;
  ToolHandler m_toolHandler;
  ActiveMapProvider m_activeMapProvider;
  QJsonObject m_overlayState;
  mutable int m_nextOperationIndex = 1;
  mutable std::vector<McpOperationRecord> m_operationHistory;
  mutable std::map<QString, McpBrushMetadataRecord> m_brushMetadata;
  mutable McpObjectRegistry m_objectRegistry;
  mutable bool m_dispatchInProgress = false;
  std::unique_ptr<QLocalServer> m_server;

public:
  explicit McpBridgeServer(AppController& appController, QObject* parent = nullptr);
  explicit McpBridgeServer(ToolHandler toolHandler, QObject* parent = nullptr);
  McpBridgeServer(
    ToolHandler toolHandler, ActiveMapProvider activeMapProvider, QObject* parent = nullptr);
  ~McpBridgeServer() override;

  bool start(const mcp::McpBridgeConfig& config, QString* error = nullptr);
  void stop();

  bool isListening() const;
  QString pipeName() const;
  mcp::McpMode mode() const;
  const QJsonObject& overlayState() const;

  mcp::McpBridgeResponse dispatchRequest(const mcp::McpBridgeRequest& request) const;
  std::optional<QJsonObject> readResource(const QString& uri) const;

private:
  void handleNewConnection();
  void handleSocketReadyRead(QLocalSocket& socket);
  void writeResponse(QLocalSocket& socket, const mcp::McpBridgeResponse& response) const;
};

} // namespace tb::ui
