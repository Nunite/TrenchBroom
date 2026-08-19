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

#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QtGlobal>

#include "mcp/McpBridgeConfig.h"
#include "ui/mcp/McpBridgeServer.h"

#include <memory>

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace tb::ui
{
namespace mcp = tb::mcp;

struct McpHttpServerLimits
{
  qsizetype maxHeaderBytes = 64 * 1024;
  qsizetype maxBodyBytes = 4 * 1024 * 1024;
  int incompleteRequestTimeoutMs = 10'000;
  int maxOrdinaryConnections = 32;
  int maxSseConnections = 8;
};

class McpHttpServer : public QObject
{
  Q_OBJECT
private:
  McpBridgeServer& m_bridgeServer;
  McpHttpServerLimits m_limits;
  mcp::McpBridgeConfig m_config;
  std::unique_ptr<QTcpServer> m_server;
  QSet<QTcpSocket*> m_connections;
  QSet<QTcpSocket*> m_sseConnections;
  QHash<QTcpSocket*, QTimer*> m_requestDeadlines;

public:
  explicit McpHttpServer(McpBridgeServer& bridgeServer, QObject* parent = nullptr);
  McpHttpServer(
    McpBridgeServer& bridgeServer, McpHttpServerLimits limits, QObject* parent = nullptr);
  ~McpHttpServer() override;

  bool start(const mcp::McpBridgeConfig& config, QString* error = nullptr);
  void stop();

  bool isListening() const;
  QString url() const;
  quint16 port() const;

private:
  int ordinaryConnectionCount() const;
  void startRequestDeadline(QTcpSocket& socket);
  void completeRequest(QTcpSocket& socket);
  void removeConnection(QTcpSocket& socket);
  void handleNewConnection();
  void handleSocketReadyRead(QTcpSocket& socket);
  void writeSseStream(QTcpSocket& socket, const QByteArray& allowedOrigin) const;
  void writeHttpResponse(
    QTcpSocket& socket,
    int statusCode,
    const QByteArray& reason,
    const QByteArray& contentType,
    const QByteArray& body,
    const QByteArray& allowedOrigin = {},
    const QByteArray& extraHeaders = {}) const;
};

} // namespace tb::ui
