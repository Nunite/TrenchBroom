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

#include <QJsonObject>
#include <QString>

#include "mdl/Map.h"
#include "mdl/Transaction.h"
#include "ui/mcp/McpBridgeServer.h"

#include <functional>
#include <utility>

namespace tb::ui
{

inline bool executeTransaction(
  mdl::Map& map, const QString& transactionName, const std::function<bool()>& operation)
{
  auto transaction = mdl::Transaction{map, transactionName.toStdString()};
  if (!operation())
  {
    transaction.cancel();
    return false;
  }
  return transaction.commit();
}

inline QJsonObject preMutationFailureDetails(
  QJsonObject details, const QString& recoveryAction)
{
  details.insert("mutatedDocument", false);
  details.insert("retrySafe", true);
  details.insert("recoveryAction", recoveryAction);
  return details;
}

inline McpBridgeToolResult preMutationInvalidParamsFailure(
  const QString& message, const QString& recoveryAction, QJsonObject details = {})
{
  return McpBridgeToolResult::failure(
    mcp::McpErrorCode::InvalidParams,
    message,
    preMutationFailureDetails(std::move(details), recoveryAction));
}

} // namespace tb::ui
