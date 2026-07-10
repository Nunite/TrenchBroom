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

void applyDocumentIdentityToOperation(
  McpOperationRecord& operation,
  mdl::Map& map,
  const McpObjectRegistry& objectRegistry,
  const QJsonObject& result)
{
  operation.documentPath = result.value("activeDocumentPath").toString();
  if (operation.documentPath.isEmpty() && !map.path().empty())
  {
    operation.documentPath = pathAsQString(map.path());
  }

  operation.documentFingerprint = result.value("documentFingerprint").toString();
  if (operation.documentFingerprint.isEmpty())
  {
    operation.documentFingerprint = objectRegistry.documentFingerprint(map);
  }
}

void syncOneOperationHistoryWithExternalResult(
  std::vector<McpOperationRecord>& history,
  const QString& operationId,
  mdl::Map& map,
  const McpObjectRegistry& objectRegistry,
  const QJsonObject& result)
{
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
    applyDocumentIdentityToOperation(operation, map, objectRegistry, result);
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
  applyDocumentIdentityToOperation(*it, map, objectRegistry, result);
}

void syncOperationHistoryWithExternalResult(
  std::vector<McpOperationRecord>& history,
  mdl::Map& map,
  const McpObjectRegistry& objectRegistry,
  const QJsonObject& result)
{
  syncOneOperationHistoryWithExternalResult(
    history, result.value("operationId").toString(), map, objectRegistry, result);

  if (!result.value("parentOperationId").toString().isEmpty())
  {
    return;
  }

  const auto operationIds = result.value("operationIds").toArray();
  for (const auto& operationId : operationIds)
  {
    syncOneOperationHistoryWithExternalResult(
      history, operationId.toString(), map, objectRegistry, result);
  }
}

QJsonObject compactReviewResource(const QJsonObject& result)
{
  auto compact = QJsonObject{};
  const auto copy = [&](const QString& key) {
    if (result.contains(key))
    {
      compact.insert(key, result.value(key));
    }
  };
  for (const auto& key :
       {"tool",
        "renderer",
        "style",
        "edgeMode",
        "reviewId",
        "resourceUri",
        "preferredCapturePath",
        "absolutePreferredCapturePath",
        "outputDir",
        "absoluteOutputDir",
        "manifestPath",
        "absoluteManifestPath",
        "targetObjectCount",
        "targetObjectIdsCount",
        "targetObjectIdsSample",
        "idsMode",
        "targetBrushCount",
        "unsupportedObjectCount",
        "targetBounds",
        "captureCount",
        "qualityValid",
        "semanticAcceptance",
        "warnings",
        "faceCount",
        "edgeCount",
        "labelCount",
        "entityLabelCount",
        "orderLabelCount",
        "partLabelCount",
        "simplified",
        "verticalExaggeration"})
  {
    copy(key);
  }

  const auto contactSheet = result.value("contactSheet").toObject();
  if (!contactSheet.isEmpty())
  {
    compact.insert(
      "contactSheet",
      QJsonObject{
        {"path", contactSheet.value("path")},
        {"absolutePath", contactSheet.value("absolutePath")},
        {"width", contactSheet.value("width")},
        {"height", contactSheet.value("height")},
        {"fileSize", contactSheet.value("fileSize")},
        {"valid", contactSheet.value("valid")},
      });
  }
  return compact;
}

} // namespace

void McpSessionState::clear()
{
  nextOperationIndex = 1;
  operationHistory.clear();
  brushMetadata.clear();
  modules.clear();
  irPreviewCache.clear();
  reviewResources.clear();
  nextIrPreviewIndex = 1;
  objectRegistry.clear();
  recentDocumentFingerprints.clear();
  evictions = {};
  evictedResourceHints.clear();
}

void McpSessionState::rememberDocumentFingerprint(const QString& documentFingerprint)
{
  if (documentFingerprint.isEmpty())
  {
    return;
  }

  recentDocumentFingerprints.removeAll(documentFingerprint);
  recentDocumentFingerprints.prepend(documentFingerprint);
  while (recentDocumentFingerprints.size() > MaxDocumentFingerprints)
  {
    const auto evictedFingerprint = recentDocumentFingerprints.takeLast();
    ++evictions.documentFingerprints;

    const auto oldOperationCount = operationHistory.size();
    std::erase_if(operationHistory, [&](const auto& operation) {
      if (operation.documentFingerprint != evictedFingerprint)
      {
        return false;
      }
      evictedResourceHints[QString{"tbmcp://operation/%1"}.arg(operation.operationId)] =
        QJsonObject{
          {"evicted", true},
          {"resourceUri", QString{"tbmcp://operation/%1"}.arg(operation.operationId)},
          {"reason", "document_fingerprint_budget"},
          {"recoveryAction", "refresh_history_for_active_document"},
        };
      return true;
    });
    evictions.operationRecords += oldOperationCount - operationHistory.size();

    std::erase_if(brushMetadata, [&](const auto& entry) {
      return entry.second.documentFingerprint == evictedFingerprint;
    });
    std::erase_if(modules, [&](const auto& entry) {
      return entry.second.documentFingerprint == evictedFingerprint;
    });

    const auto oldPreviewCount = irPreviewCache.size();
    std::erase_if(irPreviewCache, [&](const auto& entry) {
      return entry.second.documentFingerprint == evictedFingerprint;
    });
    evictions.irPreviews += oldPreviewCount - irPreviewCache.size();

    const auto oldReviewCount = reviewResources.size();
    std::erase_if(reviewResources, [&](const auto& entry) {
      if (entry.second.documentFingerprint != evictedFingerprint)
      {
        return false;
      }
      evictedResourceHints[entry.first] = QJsonObject{
        {"evicted", true},
        {"resourceUri", entry.first},
        {"reason", "document_fingerprint_budget"},
        {"recoveryAction", "activate_document_and_regenerate_review"},
      };
      return true;
    });
    evictions.reviewResources += oldReviewCount - reviewResources.size();
  }

  evictions.objectRegistryRecords +=
    objectRegistry.retainDocumentFingerprints(recentDocumentFingerprints);
  while (evictedResourceHints.size() > 512u)
  {
    evictedResourceHints.erase(evictedResourceHints.begin());
  }
}

void McpSessionState::prune(const QString& activeDocumentFingerprint, const qint64 nowMs)
{
  rememberDocumentFingerprint(activeDocumentFingerprint);

  const auto rememberEvictedResource = [&](const QString& uri, QJsonObject hint) {
    hint.insert("resourceUri", uri);
    hint.insert("evictedAtMs", nowMs);
    evictedResourceHints[uri] = std::move(hint);
    while (evictedResourceHints.size() > 512u)
    {
      evictedResourceHints.erase(evictedResourceHints.begin());
    }
  };

  for (auto it = irPreviewCache.begin(); it != irPreviewCache.end();)
  {
    if (it->second.expiresAtMs > nowMs)
    {
      ++it;
      continue;
    }
    rememberEvictedResource(
      QString{"tbmcp://ir-preview/%1"}.arg(it->first),
      QJsonObject{
        {"evicted", true},
        {"reason", "expired"},
        {"recoveryAction", "compile_preview_again"},
      });
    it = irPreviewCache.erase(it);
    ++evictions.irPreviews;
  }
  while (irPreviewCache.size() > MaxIrPreviews)
  {
    const auto oldest = std::ranges::min_element(
      irPreviewCache, {}, [](const auto& entry) { return entry.second.createdAtMs; });
    rememberEvictedResource(
      QString{"tbmcp://ir-preview/%1"}.arg(oldest->first),
      QJsonObject{
        {"evicted", true},
        {"reason", "preview_budget"},
        {"recoveryAction", "compile_preview_again"},
      });
    irPreviewCache.erase(oldest);
    ++evictions.irPreviews;
  }

  while (reviewResources.size() > MaxReviewResources)
  {
    const auto oldest = std::ranges::min_element(
      reviewResources, {}, [](const auto& entry) { return entry.second.createdAtMs; });
    rememberEvictedResource(
      oldest->first,
      QJsonObject{
        {"evicted", true},
        {"reason", "review_resource_budget"},
        {"recoveryAction", "regenerate_review"},
      });
    reviewResources.erase(oldest);
    ++evictions.reviewResources;
  }

  const auto eraseOperationFamily = [&](const McpOperationRecord& candidate) {
    const auto parentOperationId = candidate.parentOperationId.isEmpty()
                                     ? candidate.operationId
                                     : candidate.parentOperationId;
    const auto oldSize = operationHistory.size();
    std::erase_if(operationHistory, [&](const auto& operation) {
      const auto remove = operation.operationId == parentOperationId
                          || operation.parentOperationId == parentOperationId;
      if (remove)
      {
        rememberEvictedResource(
          QString{"tbmcp://operation/%1"}.arg(operation.operationId),
          QJsonObject{
            {"evicted", true},
            {"reason", "operation_record_budget"},
            {"recoveryAction", "refresh_history_status"},
          });
      }
      return remove;
    });
    evictions.operationRecords += oldSize - operationHistory.size();
  };

  while (operationHistory.size() > MaxOperationRecords)
  {
    const auto oldestMatching = [&](const auto& predicate) {
      auto result = operationHistory.end();
      for (auto it = operationHistory.begin(); it != operationHistory.end(); ++it)
      {
        if (
          predicate(*it)
          && (result == operationHistory.end() || it->createdAtMs < result->createdAtMs))
        {
          result = it;
        }
      }
      return result;
    };

    auto candidate =
      oldestMatching([](const auto& operation) { return operation.undone; });
    if (candidate == operationHistory.end())
    {
      candidate = oldestMatching([&](const auto& operation) {
        return !activeDocumentFingerprint.isEmpty()
               && operation.documentFingerprint != activeDocumentFingerprint;
      });
    }
    if (candidate == operationHistory.end())
    {
      candidate = oldestMatching([](const auto&) { return true; });
    }
    eraseOperationFamily(*candidate);
  }
}

void McpSessionState::cacheReviewResource(
  const QJsonObject& result, const QString& documentFingerprint)
{
  const auto resourceUri = result.value("resourceUri").toString();
  if (!resourceUri.startsWith("tbmcp://review/"))
  {
    return;
  }
  reviewResources[resourceUri] = McpReviewResourceRecord{
    compactReviewResource(result),
    result.value("documentFingerprint").toString(documentFingerprint),
    QDateTime::currentMSecsSinceEpoch(),
  };
  evictedResourceHints.erase(resourceUri);
}

std::optional<QJsonObject> McpSessionState::evictedResourceHint(const QString& uri) const
{
  const auto it = evictedResourceHints.find(uri);
  return it != evictedResourceHints.end() ? std::optional{it->second} : std::nullopt;
}

QJsonObject McpSessionState::diagnosticsJson() const
{
  return QJsonObject{
    {"limits",
     QJsonObject{
       {"operationRecords", static_cast<qint64>(MaxOperationRecords)},
       {"reviewResources", static_cast<qint64>(MaxReviewResources)},
       {"irPreviews", static_cast<qint64>(MaxIrPreviews)},
       {"irPreviewTtlMs", IrPreviewTtlMs},
       {"documentFingerprints", MaxDocumentFingerprints},
     }},
    {"counts",
     QJsonObject{
       {"operationRecords", static_cast<qint64>(operationHistory.size())},
       {"reviewResources", static_cast<qint64>(reviewResources.size())},
       {"irPreviews", static_cast<qint64>(irPreviewCache.size())},
       {"documentFingerprints", recentDocumentFingerprints.size()},
       {"objectRegistryRecords", static_cast<qint64>(objectRegistry.recordCount())},
     }},
    {"evictions",
     QJsonObject{
       {"operationRecords", static_cast<qint64>(evictions.operationRecords)},
       {"reviewResources", static_cast<qint64>(evictions.reviewResources)},
       {"irPreviews", static_cast<qint64>(evictions.irPreviews)},
       {"documentFingerprints", static_cast<qint64>(evictions.documentFingerprints)},
       {"objectRegistryRecords", static_cast<qint64>(evictions.objectRegistryRecords)},
     }},
  };
}

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

  auto probe = QLocalSocket{};
  probe.connectToServer(m_config.pipeName);
  if (probe.waitForConnected(250))
  {
    if (error)
    {
      *error =
        QString{"Another TrenchBroom MCP instance is already listening on pipe '%1'"}.arg(
          m_config.pipeName);
    }
    m_server.reset();
    return false;
  }

  if (
    probe.error() != QLocalSocket::ServerNotFoundError
    && probe.error() != QLocalSocket::ConnectionRefusedError)
  {
    if (error)
    {
      *error = QString{"Could not verify whether MCP pipe '%1' is active: %2"}.arg(
        m_config.pipeName, probe.errorString());
    }
    m_server.reset();
    return false;
  }

  if (m_server->listen(m_config.pipeName))
  {
    return true;
  }

  if (
    !QLocalServer::removeServer(m_config.pipeName)
    || !m_server->listen(m_config.pipeName))
  {
    if (error)
    {
      *error = QString{"Could not claim inactive MCP pipe '%1': %2"}.arg(
        m_config.pipeName, m_server->errorString());
    }
    m_server.reset();
    return false;
  }

  return true;
}

void McpBridgeServer::clearSessionState()
{
  m_overlayState = QJsonObject{};
  m_session.clear();
}

void McpBridgeServer::stop()
{
  if (m_server)
  {
    m_server->close();
    QLocalServer::removeServer(m_config.pipeName);
    m_server.reset();
  }
  clearSessionState();
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
    {"documentPath", operation.documentPath},
    {"documentFingerprint", operation.documentFingerprint},
    {"createdAt", operation.createdAt},
    {"createdAtMs", operation.createdAtMs},
    {"changedObjectCount", operation.changedObjectIds.size()},
    {"deletedObjectCount", operation.deletedObjectIds.size()},
    {"undone", operation.undone},
    {"undoable", operation.undoable},
    {"parentOperationId", operation.parentOperationId},
    {"childOperationIds", QJsonArray::fromStringList(operation.childOperationIds)},
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
  static const auto ReviewPrefix = QString{"tbmcp://review/"};
  if (uri.startsWith(ReviewPrefix))
  {
    const auto it = m_session.reviewResources.find(uri);
    if (it != m_session.reviewResources.end())
    {
      return it->second.resource;
    }
    return m_session.evictedResourceHint(uri);
  }

  static const auto Prefix = QString{"tbmcp://operation/"};
  if (!uri.startsWith(Prefix))
  {
    return std::nullopt;
  }

  const auto operationId = uri.mid(Prefix.size());
  const auto it = std::ranges::find_if(
    m_session.operationHistory,
    [&](const auto& operation) { return operation.operationId == operationId; });
  if (it == m_session.operationHistory.end())
  {
    return m_session.evictedResourceHint(uri);
  }

  if (m_activeMapProvider)
  {
    if (auto* map = m_activeMapProvider())
    {
      return m_session.objectRegistry.externalizeResult(
        *map,
        resourceObject(
          *it,
          m_session.objectRegistry.liveStateJson(
            *map, it->changedObjectIds, it->undone)));
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

  const auto effectiveMode =
    request.requestedMode && mcp::allowsMode(m_config.mode, *request.requestedMode)
      ? *request.requestedMode
      : m_config.mode;
  if (!mcp::canCallTool(*tool, effectiveMode))
  {
    return makeFailure(
      request,
      mcp::McpErrorCode::Forbidden,
      QString{"MCP tool is not available in mode %1"}.arg(mcp::modeName(effectiveMode)));
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
            {"mutatedDocument", false},
            {"retrySafe", true},
            {"recoveryAction", "activate_expected_document_then_retry"},
            {"processId", static_cast<int>(QCoreApplication::applicationPid())},
            {"bridgeInstanceId", m_bridgeInstanceId},
            {"bridgeStartedAt", m_bridgeStartedAtUtc.toString(Qt::ISODateWithMs)},
            {"httpPort", static_cast<int>(m_config.httpPort)},
          });
      }
    }

    const auto expectedDocumentFingerprint =
      params.value("expectedDocumentFingerprint").toString().trimmed();
    if (!expectedDocumentFingerprint.isEmpty())
    {
      const auto actualDocumentFingerprint =
        map != nullptr ? m_session.objectRegistry.documentFingerprint(*map) : QString{};
      if (actualDocumentFingerprint != expectedDocumentFingerprint)
      {
        return makeFailure(
          request,
          mcp::McpErrorCode::Forbidden,
          QString{"Active document does not match expectedDocumentFingerprint. "
                  "Expected '%1', actual '%2'."}
            .arg(expectedDocumentFingerprint, actualDocumentFingerprint),
          QJsonObject{
            {"expectedDocumentFingerprint", expectedDocumentFingerprint},
            {"actualDocumentFingerprint", actualDocumentFingerprint},
            {"actualDocumentPath",
             map != nullptr && !map->path().empty() ? pathAsQString(map->path())
                                                    : QString{}},
            {"mutatedDocument", false},
            {"retrySafe", true},
            {"recoveryAction", "refresh_status_or_activate_expected_document"},
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
    const auto internalParams =
      m_session.objectRegistry.internalizeParams(*map, params, error);
    if (!internalParams)
    {
      return makeFailure(request, mcp::McpErrorCode::InvalidParams, error);
    }
    params = *internalParams;
  }

  const auto requestDocumentFingerprint =
    map != nullptr ? m_session.objectRegistry.documentFingerprint(*map) : QString{};
  m_session.rememberDocumentFingerprint(requestDocumentFingerprint);

  const auto result = m_toolHandler(request.tool, params);
  if (result.ok)
  {
    auto* resultMap = m_activeMapProvider ? m_activeMapProvider() : nullptr;
    if (resultMap != nullptr)
    {
      auto externalResult =
        m_session.objectRegistry.externalizeResult(*resultMap, result.result);
      syncOperationHistoryWithExternalResult(
        m_session.operationHistory, *resultMap, m_session.objectRegistry, externalResult);
      const auto activeDocumentFingerprint =
        m_session.objectRegistry.documentFingerprint(*resultMap);
      m_session.cacheReviewResource(externalResult, activeDocumentFingerprint);
      m_session.prune(activeDocumentFingerprint, QDateTime::currentMSecsSinceEpoch());
      return mcp::McpBridgeResponse::success(request.id, std::move(externalResult));
    }
    m_session.cacheReviewResource(result.result);
    m_session.prune({}, QDateTime::currentMSecsSinceEpoch());
    return mcp::McpBridgeResponse::success(request.id, result.result);
  }
  m_session.prune(requestDocumentFingerprint, QDateTime::currentMSecsSinceEpoch());
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
