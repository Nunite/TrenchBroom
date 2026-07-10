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
#include <QDateTime>
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
  static McpBridgeToolResult failure(
    mcp::McpErrorCode code, QString message, QJsonObject details);
};

struct McpOperationRecord
{
  QString operationId;
  QString toolName;
  QString transactionName;
  QString operationKind;
  QString documentPath;
  QString documentFingerprint;
  QStringList changedObjectIds;
  QStringList deletedObjectIds;
  QString createdAt;
  qint64 createdAtMs = 0;
  QByteArray summaryJson;
  QByteArray detailJson;
  bool undone = false;
  bool undoable = true;
  QString parentOperationId;
  QStringList childOperationIds;

  McpOperationRecord();
  void setChangedObjectIds(const QJsonArray& ids);
  void setDeletedObjectIds(const QJsonArray& ids);
  QJsonArray changedObjectIdsJson() const;
  QJsonArray deletedObjectIdsJson() const;
  void setSummary(const QJsonObject& value);
  QJsonObject summary() const;
  void setDetail(const QJsonObject& value);
  QJsonObject detail() const;
};

struct McpBrushMetadataRecord
{
  QString objectId;
  QString documentFingerprint;
  QJsonObject metadata;
  bool stale = false;
};

struct McpModuleRecord
{
  QString moduleId;
  QString documentFingerprint;
  QStringList objectIds;
  QStringList operationIds;
  QJsonObject metadata;
};

struct McpIrPreviewCacheRecord
{
  QString previewId;
  QString sourcePath;
  QString irHash;
  QString documentFingerprint;
  QString activeDocumentPath;
  qint64 createdAtMs = 0;
  qint64 expiresAtMs = 0;
  QJsonObject preview;
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
  mutable std::map<QString, McpModuleRecord> m_modules;
  mutable std::map<QString, McpIrPreviewCacheRecord> m_irPreviewCache;
  mutable std::map<QString, QJsonObject> m_reviewResources;
  mutable int m_nextIrPreviewIndex = 1;
  mutable McpObjectRegistry m_objectRegistry;
  mutable bool m_dispatchInProgress = false;
  QString m_bridgeInstanceId;
  QDateTime m_bridgeStartedAtUtc;
  std::unique_ptr<QLocalServer> m_server = nullptr;

public:
  explicit McpBridgeServer(AppController& appController, QObject* parent = nullptr);
  explicit McpBridgeServer(ToolHandler toolHandler, QObject* parent = nullptr);
  McpBridgeServer(
    ToolHandler toolHandler,
    ActiveMapProvider activeMapProvider,
    QObject* parent = nullptr);
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
  void clearSessionState();
  void handleNewConnection();
  void handleSocketReadyRead(QLocalSocket& socket);
  void writeResponse(QLocalSocket& socket, const mcp::McpBridgeResponse& response) const;
};

} // namespace tb::ui
