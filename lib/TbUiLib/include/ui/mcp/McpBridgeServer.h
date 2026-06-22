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

#include <QObject>

#include "mcp/McpBridgeConfig.h"
#include "mcp/McpBridgeMessages.h"

#include <functional>
#include <memory>

class QLocalServer;
class QLocalSocket;

namespace tb::ui
{
namespace mcp = tb::mcp;

class AppController;

class McpBridgeServer : public QObject
{
  Q_OBJECT
public:
  using StatusProvider = std::function<QJsonObject()>;

private:
  mcp::McpBridgeConfig m_config;
  StatusProvider m_statusProvider;
  std::unique_ptr<QLocalServer> m_server;

public:
  explicit McpBridgeServer(AppController& appController, QObject* parent = nullptr);
  explicit McpBridgeServer(StatusProvider statusProvider, QObject* parent = nullptr);
  ~McpBridgeServer() override;

  bool start(const mcp::McpBridgeConfig& config, QString* error = nullptr);
  void stop();

  bool isListening() const;
  QString pipeName() const;
  mcp::McpMode mode() const;

  mcp::McpBridgeResponse dispatchRequest(const mcp::McpBridgeRequest& request) const;

private:
  void handleNewConnection();
  void handleSocketReadyRead(QLocalSocket& socket);
  void writeResponse(QLocalSocket& socket, const mcp::McpBridgeResponse& response) const;
};

} // namespace tb::ui
