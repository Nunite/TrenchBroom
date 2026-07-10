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

#include <QDateTime>

#include "ui/mcp/McpBridgeServer.h"

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("McpSessionState enforces bounded caches", "[McpBridgeServer][McpSessionState]")
{
  const auto nowMs = QDateTime::currentMSecsSinceEpoch();

  SECTION("operation records prefer evicting undone entries")
  {
    auto state = McpSessionState{};
    for (auto i = 0; i <= static_cast<int>(McpSessionState::MaxOperationRecords); ++i)
    {
      auto operation = McpOperationRecord{};
      operation.operationId = QString{"mcp-op-%1"}.arg(i);
      operation.documentFingerprint = "doc:active";
      operation.createdAtMs = nowMs + i;
      operation.undone = i == 0;
      state.operationHistory.push_back(std::move(operation));
    }

    state.prune("doc:active", nowMs);

    CHECK(state.operationHistory.size() == McpSessionState::MaxOperationRecords);
    CHECK(state.evictions.operationRecords == 1u);
    CHECK_FALSE(std::ranges::any_of(state.operationHistory, [](const auto& operation) {
      return operation.operationId == "mcp-op-0";
    }));
    const auto hint = state.evictedResourceHint("tbmcp://operation/mcp-op-0");
    REQUIRE(hint);
    CHECK(hint->value("recoveryAction").toString() == "refresh_history_status");
  }

  SECTION("IR previews expire and stay within the fixed budget")
  {
    auto state = McpSessionState{};
    for (auto i = 0; i < 66; ++i)
    {
      const auto previewId = QString{"ir-preview-%1"}.arg(i);
      state.irPreviewCache[previewId] = McpIrPreviewCacheRecord{
        previewId,
        {},
        {},
        "doc:active",
        {},
        nowMs + i,
        i == 0 ? nowMs - 1 : nowMs + McpSessionState::IrPreviewTtlMs,
        {},
      };
    }

    state.prune("doc:active", nowMs);

    CHECK(state.irPreviewCache.size() == McpSessionState::MaxIrPreviews);
    CHECK(state.evictions.irPreviews == 2u);
    CHECK_FALSE(state.irPreviewCache.contains("ir-preview-0"));
    CHECK_FALSE(state.irPreviewCache.contains("ir-preview-1"));
  }

  SECTION("only the current and three recent document fingerprints are retained")
  {
    auto state = McpSessionState{};
    state.brushMetadata["doc:1|mcp:1:1"] =
      McpBrushMetadataRecord{"mcp:1:1", "doc:1", {}, false};
    state.modules["doc:1|module"] = McpModuleRecord{"module", "doc:1"};

    for (auto i = 1; i <= 5; ++i)
    {
      state.rememberDocumentFingerprint(QString{"doc:%1"}.arg(i));
    }

    CHECK(
      state.recentDocumentFingerprints
      == QStringList{"doc:5", "doc:4", "doc:3", "doc:2"});
    CHECK(state.evictions.documentFingerprints == 1u);
    CHECK(state.brushMetadata.empty());
    CHECK(state.modules.empty());
  }

  SECTION("diagnostics expose limits, counts, and eviction counters")
  {
    auto state = McpSessionState{};
    state.rememberDocumentFingerprint("doc:active");
    const auto diagnostics = state.diagnosticsJson();

    CHECK(
      diagnostics.value("limits").toObject().value("reviewResources").toInt()
      == static_cast<int>(McpSessionState::MaxReviewResources));
    CHECK(
      diagnostics.value("counts").toObject().value("documentFingerprints").toInt() == 1);
    CHECK(
      diagnostics.value("evictions").toObject().value("operationRecords").toInt() == 0);
  }
}

} // namespace tb::ui
