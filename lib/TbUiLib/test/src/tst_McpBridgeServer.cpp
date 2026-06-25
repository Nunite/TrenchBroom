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

#include <QColor>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "../../src/mcp/McpBridgeServerTools.h"
#include "Result.h"
#include "gl/GlManager.h"
#include "mdl/GameConfigFixture.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Selection.h"
#include "mdl/WorldNode.h"
#include "ui/AppControllerFixture.h"
#include "ui/CatchConfig.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/mcp/McpBridgeServer.h"

#include <optional>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace mcp = tb::mcp;

TEST_CASE("McpBridgeServer")
{
  auto server = McpBridgeServer{[](const QString& toolName, const QJsonObject&) {
    if (toolName == "tb_status")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"application", "TrenchBroom"},
        {"mode", "ReadOnly"},
      });
    }
    if (toolName == "documents_list")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 0},
      });
    }
    if (toolName == "documents_activate")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"activated", true},
      });
    }
    if (toolName == "documents_open")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"opened", true},
      });
    }
    if (toolName == "documents_save")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"saved", true},
      });
    }
    if (toolName == "documents_close")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"closed", true},
      });
    }
    if (toolName == "documents_export")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"exported", true},
      });
    }
    if (toolName == "map_search")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 1},
        {"query", "worldspawn"},
      });
    }
    if (toolName == "selection_set")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"selectedCount", 0},
      });
    }
    if (toolName == "selection_filter")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 0},
      });
    }
    if (toolName == "selection_by_bounds")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 0},
      });
    }
    if (toolName == "selection_grow")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"selectedCount", 0},
      });
    }
    if (toolName == "viewport_focus")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"focused", true},
      });
    }
    if (toolName == "viewport_clear_marks")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"active", false},
      });
    }
    if (
      toolName == "viewport_capture_current" || toolName == "viewport_capture_3d"
      || toolName == "viewport_capture_2d")
    {
      auto scope = QString{"window"};
      if (toolName == "viewport_capture_3d")
      {
        scope = "3d";
      }
      else if (toolName == "viewport_capture_2d")
      {
        scope = "2d";
      }
      return McpBridgeToolResult::success(QJsonObject{
        {"format", "png"},
        {"path", "C:/tmp/viewport.png"},
        {"width", 640},
        {"height", 480},
        {"scope", scope},
      });
    }
    if (toolName == "overlay_set")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"active", true},
      });
    }
    if (toolName == "entity_create")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-1"},
        {"transactionName", "MCP: Create info_player_start"},
      });
    }
    if (toolName == "fgd_entities_list")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 1},
      });
    }
    if (toolName == "entity_schema")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"classname", "func_wall"},
      });
    }
    if (toolName == "entity_create_from_schema")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-4"},
        {"transactionName", "MCP: Create light"},
      });
    }
    if (toolName == "entity_tie_brushes")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-5"},
        {"transactionName", "MCP: Tie brushes to func_wall"},
      });
    }
    if (toolName == "entity_untie_brushes")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-6"},
        {"transactionName", "MCP: Untie brushes"},
      });
    }
    if (toolName == "brush_types_list")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"types", QJsonArray{}},
      });
    }
    if (
      toolName == "brush_create" || toolName == "brush_create_cone"
      || toolName == "brush_create_pipe" || toolName == "brush_create_sphere"
      || toolName == "brush_create_pyramid" || toolName == "brush_create_tetrahedron"
      || toolName == "brush_create_from_planes" || toolName == "brush_create_prism"
      || toolName == "brush_create_cylinder_sector")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-7"},
        {"transactionName", "MCP: Create brush primitive"},
      });
    }
    if (toolName == "history_list")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 0},
      });
    }
    if (toolName == "asset_search")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 0},
      });
    }
    if (toolName == "textures_list" || toolName == "texture_search")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 0},
      });
    }
    if (toolName == "texture_lock_get")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"textureLock", true},
        {"uvLock", false},
      });
    }
    if (toolName == "texture_lock_set")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"textureLock", true},
        {"uvLock", true},
        {"changed", true},
      });
    }
    if (toolName == "face_list")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 0},
      });
    }
    if (toolName == "face_select")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"selectedCount", 1},
      });
    }
    if (
      toolName == "texture_apply" || toolName == "texture_replace"
      || toolName == "texture_align_face" || toolName == "texture_copy_from_face"
      || toolName == "face_texture_set")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-8"},
        {"transactionName", "MCP: Texture edit"},
      });
    }
    if (toolName == "objects_delete" || toolName == "objects_transform")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-9"},
        {"transactionName", "MCP: Object edit"},
      });
    }
    if (toolName == "map_validate" || toolName == "problems_check")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"valid", true},
        {"count", 0},
      });
    }
    if (toolName == "problems_fix" || toolName == "map_fix_all_safe")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-10"},
        {"transactionName", "MCP: Fix problems"},
      });
    }
    if (toolName == "compile_profiles_list")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"count", 1},
      });
    }
    if (toolName == "compile_log_tail")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"lineCount", 0},
        {"running", false},
      });
    }
    if (toolName == "compile_run")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"profile", "default"},
        {"started", true},
      });
    }
    if (toolName == "leaks_load_pointfile")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"loaded", true},
        {"pointCount", 2},
      });
    }
    if (toolName == "asset_place_model")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-2"},
        {"transactionName", "MCP: Place model asset"},
      });
    }
    if (toolName == "blockout_validate")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"valid", true},
      });
    }
    if (toolName == "geometry_analyze_selection")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"brushCount", 0},
        {"invalidBrushCount", 0},
      });
    }
    if (toolName == "blockout_validate_spiral_stairs")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"valid", true},
        {"gapCount", 0},
      });
    }
    if (
      toolName == "operation_inspect" || toolName == "operation_select"
      || toolName == "operation_validate")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-3"},
      });
    }
    if (
      toolName == "blockout_create_batch"
      || toolName == "blockout_create_curved_corridor")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-11"},
        {"transactionName",
         toolName == "blockout_create_batch" ? "MCP: Blockout batch"
                                             : "MCP: Blockout curved corridor"},
        {"brushCount", 4},
        {"resourceUri", "tbmcp://operation/mcp-op-11"},
      });
    }
    if (toolName == "heightmap_import_grayscale")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-12"},
        {"transactionName", "MCP: Import grayscale heightmap"},
        {"brushCount", 4},
        {"resourceUri", "tbmcp://operation/mcp-op-12"},
      });
    }
    if (toolName == "blockout_create_room" || toolName == "blockout_create_spiral_stairs")
    {
      return McpBridgeToolResult::success(QJsonObject{
        {"operationId", "mcp-op-3"},
        {"transactionName",
         toolName == "blockout_create_room" ? "MCP: Blockout room"
                                            : "MCP: Blockout spiral stairs"},
      });
    }
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::ToolNotFound, QString{"Unknown test tool"});
  }};

  SECTION("off mode does not listen")
  {
    auto error = QString{};
    CHECK(server.start(
      mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::Off}, &error));
    CHECK(error.isEmpty());
    CHECK(!server.isListening());
  }

  SECTION("rejects wrong token")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "wrong", "tb_status", {}, mcp::McpMode::ReadOnly});

    CHECK(!response.ok);
    REQUIRE(response.error);
    CHECK(response.error->code == mcp::McpErrorCode::Unauthorized);
  }

  SECTION("serves tb_status")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "secret", "tb_status", {}, mcp::McpMode::ReadOnly});

    CHECK(response.ok);
    CHECK(response.result.value("application").toString() == "TrenchBroom");
  }

  SECTION("serves wired read-only tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "secret", "documents_list", {}, mcp::McpMode::ReadOnly});

    CHECK(response.ok);
    CHECK(response.result.value("count").toInt() == 0);
  }

  SECTION("serves read-only document and selection parity tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto activateResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "documents_activate",
      QJsonObject{{"index", 0}},
      mcp::McpMode::ReadOnly});
    CHECK(activateResponse.ok);
    CHECK(activateResponse.result.value("activated").toBool());

    const auto filterResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "2", "secret", "selection_filter", {}, mcp::McpMode::ReadOnly});
    CHECK(filterResponse.ok);

    const auto boundsResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "3",
      "secret",
      "selection_by_bounds",
      QJsonObject{
        {"min", QJsonArray{0, 0, 0}},
        {"max", QJsonArray{128, 128, 128}},
      },
      mcp::McpMode::ReadOnly});
    CHECK(boundsResponse.ok);

    const auto growResponse = server.dispatchRequest(
      mcp::McpBridgeRequest{"4", "secret", "selection_grow", {}, mcp::McpMode::ReadOnly});
    CHECK(growResponse.ok);

    const auto focusResponse = server.dispatchRequest(
      mcp::McpBridgeRequest{"5", "secret", "viewport_focus", {}, mcp::McpMode::ReadOnly});
    CHECK(focusResponse.ok);

    const auto clearResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "6", "secret", "viewport_clear_marks", {}, mcp::McpMode::ReadOnly});
    CHECK(clearResponse.ok);

    const auto captureResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "7",
      "secret",
      "viewport_capture_current",
      QJsonObject{{"returnBase64", false}},
      mcp::McpMode::ReadOnly});
    CHECK(captureResponse.ok);
    CHECK(captureResponse.result.value("format").toString() == "png");
    CHECK(captureResponse.result.value("scope").toString() == "window");

    const auto capture3DResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "8",
      "secret",
      "viewport_capture_3d",
      QJsonObject{{"returnBase64", false}},
      mcp::McpMode::ReadOnly});
    CHECK(capture3DResponse.ok);
    CHECK(capture3DResponse.result.value("scope").toString() == "3d");

    const auto capture2DResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "9",
      "secret",
      "viewport_capture_2d",
      QJsonObject{{"returnBase64", false}},
      mcp::McpMode::ReadOnly});
    CHECK(capture2DResponse.ok);
    CHECK(capture2DResponse.result.value("scope").toString() == "2d");
  }

  SECTION("serves map_search")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "map_search",
      QJsonObject{{"query", "worldspawn"}},
      mcp::McpMode::ReadOnly});

    CHECK(response.ok);
    CHECK(response.result.value("query").toString() == "worldspawn");
    CHECK(response.result.value("count").toInt() == 1);
  }

  SECTION("serves selection_set")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "secret", "selection_set", {}, mcp::McpMode::ReadOnly});

    CHECK(response.ok);
    CHECK(response.result.value("selectedCount").toInt() == 0);
  }

  SECTION("serves overlay_set")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "overlay_set",
      QJsonObject{{"highlightObjectIds", QJsonArray{}}},
      mcp::McpMode::ReadOnly});

    CHECK(response.ok);
    CHECK(response.result.value("active").toBool());
  }

  SECTION("read-only mode rejects action_execute")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "action_execute",
      QJsonObject{{"actionId", "Menu/Edit/Undo"}},
      mcp::McpMode::Edit});

    CHECK(!response.ok);
    REQUIRE(response.error);
    CHECK(response.error->code == mcp::McpErrorCode::Forbidden);
  }

  SECTION("mode gating rejects edit tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "secret", "entity_create", {}, mcp::McpMode::Edit});

    CHECK(!response.ok);
    REQUIRE(response.error);
    CHECK(response.error->code == mcp::McpErrorCode::Forbidden);

    const auto pythonResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "2",
      "secret",
      "python_generate_blockout",
      QJsonObject{{"script", "print('{}')"}},
      mcp::McpMode::Edit});
    CHECK(!pythonResponse.ok);
    REQUIRE(pythonResponse.error);
    CHECK(pythonResponse.error->code == mcp::McpErrorCode::Forbidden);

    const auto heightmapResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "3",
      "secret",
      "heightmap_import_grayscale",
      QJsonObject{{"imagePath", "C:/tmp/heightmap.png"}},
      mcp::McpMode::Edit});
    CHECK(!heightmapResponse.ok);
    REQUIRE(heightmapResponse.error);
    CHECK(heightmapResponse.error->code == mcp::McpErrorCode::Forbidden);
  }

  SECTION("registered unsupported tools report not implemented")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::Edit}));

    const auto response = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "secret", "brush_create_arch", {}, mcp::McpMode::Edit});

    CHECK(!response.ok);
    REQUIRE(response.error);
    CHECK(response.error->code == mcp::McpErrorCode::ToolNotFound);
    CHECK(response.error->message.contains("not implemented"));
  }

  SECTION("read-only mode rejects document mutation tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "documents_save",
      QJsonObject{{"path", "C:/tmp/test.map"}},
      mcp::McpMode::Edit});

    CHECK(!response.ok);
    REQUIRE(response.error);
    CHECK(response.error->code == mcp::McpErrorCode::Forbidden);
  }

  SECTION("edit mode serves edit tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::Edit}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "entity_create",
      QJsonObject{{"classname", "info_player_start"}},
      mcp::McpMode::Edit});

    CHECK(response.ok);
    CHECK(response.result.value("operationId").toString() == "mcp-op-1");
  }

  SECTION("serves FGD schema tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto listResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "1", "secret", "fgd_entities_list", {}, mcp::McpMode::ReadOnly});
    CHECK(listResponse.ok);
    CHECK(listResponse.result.value("count").toInt() == 1);

    const auto schemaResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "2",
      "secret",
      "entity_schema",
      QJsonObject{{"classname", "func_wall"}},
      mcp::McpMode::ReadOnly});
    CHECK(schemaResponse.ok);
    CHECK(schemaResponse.result.value("classname").toString() == "func_wall");
  }

  SECTION("serves brush primitive discovery in read-only mode")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1", "secret", "brush_types_list", {}, mcp::McpMode::ReadOnly});
    CHECK(response.ok);
    CHECK(response.result.contains("types"));
  }

  SECTION("edit mode serves FGD entity mutation tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::Edit}));

    const auto createResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "entity_create_from_schema",
      QJsonObject{{"classname", "light"}},
      mcp::McpMode::Edit});
    CHECK(createResponse.ok);
    CHECK(createResponse.result.value("operationId").toString() == "mcp-op-4");

    const auto tieResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "2",
      "secret",
      "entity_tie_brushes",
      QJsonObject{{"classname", "func_wall"}},
      mcp::McpMode::Edit});
    CHECK(tieResponse.ok);
    CHECK(tieResponse.result.value("operationId").toString() == "mcp-op-5");

    const auto untieResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "3", "secret", "entity_untie_brushes", {}, mcp::McpMode::Edit});
    CHECK(untieResponse.ok);
    CHECK(untieResponse.result.value("operationId").toString() == "mcp-op-6");
  }

  SECTION("edit mode serves extended brush primitive tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::Edit}));

    for (const auto& toolName :
         {"brush_create",
          "brush_create_cone",
          "brush_create_pipe",
          "brush_create_sphere",
          "brush_create_pyramid",
          "brush_create_tetrahedron",
          "brush_create_from_planes",
          "brush_create_prism",
          "brush_create_cylinder_sector"})
    {
      const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
        "1", "secret", toolName, QJsonObject{{"type", "box"}}, mcp::McpMode::Edit});
      CHECK(response.ok);
      CHECK(response.result.value("operationId").toString() == "mcp-op-7");
    }
  }

  SECTION("edit mode serves document lifecycle tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::Edit}));

    const auto openResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "documents_open",
      QJsonObject{{"path", "C:/tmp/test.map"}},
      mcp::McpMode::Edit});
    CHECK(openResponse.ok);

    const auto saveResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "2",
      "secret",
      "documents_save",
      QJsonObject{{"path", "C:/tmp/test.map"}},
      mcp::McpMode::Edit});
    CHECK(saveResponse.ok);

    const auto exportResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "3",
      "secret",
      "documents_export",
      QJsonObject{{"path", "C:/tmp/export.map"}},
      mcp::McpMode::Edit});
    CHECK(exportResponse.ok);

    const auto closeResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "4",
      "secret",
      "documents_close",
      QJsonObject{{"discardChanges", true}},
      mcp::McpMode::Edit});
    CHECK(closeResponse.ok);
  }

  SECTION("read-only mode serves history_list")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "secret", "history_list", {}, mcp::McpMode::ReadOnly});

    CHECK(response.ok);
    CHECK(response.result.value("count").toInt() == 0);
  }

  SECTION("read-only mode serves asset and texture search")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto assetResponse = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "secret", "asset_search", {}, mcp::McpMode::ReadOnly});
    CHECK(assetResponse.ok);

    const auto textureResponse = server.dispatchRequest(
      mcp::McpBridgeRequest{"2", "secret", "texture_search", {}, mcp::McpMode::ReadOnly});
    CHECK(textureResponse.ok);

    const auto texturesListResponse = server.dispatchRequest(
      mcp::McpBridgeRequest{"3", "secret", "textures_list", {}, mcp::McpMode::ReadOnly});
    CHECK(texturesListResponse.ok);

    const auto textureLockResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "4", "secret", "texture_lock_get", {}, mcp::McpMode::ReadOnly});
    CHECK(textureLockResponse.ok);
    CHECK(textureLockResponse.result.value("textureLock").toBool());
    CHECK_FALSE(textureLockResponse.result.value("uvLock").toBool());

    const auto textureLockSetResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "5",
      "secret",
      "texture_lock_set",
      QJsonObject{{"textureLock", false}},
      mcp::McpMode::ReadOnly});
    CHECK(!textureLockSetResponse.ok);
    REQUIRE(textureLockSetResponse.error);
    CHECK(textureLockSetResponse.error->code == mcp::McpErrorCode::Forbidden);

    const auto faceListResponse = server.dispatchRequest(
      mcp::McpBridgeRequest{"6", "secret", "face_list", {}, mcp::McpMode::ReadOnly});
    CHECK(faceListResponse.ok);

    const auto faceSelectResponse = server.dispatchRequest(
      mcp::McpBridgeRequest{"7", "secret", "face_select", {}, mcp::McpMode::ReadOnly});
    CHECK(faceSelectResponse.ok);
  }

  SECTION("edit mode serves asset placement")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::Edit}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "asset_place_model",
      QJsonObject{{"path", "models/player.mdl"}},
      mcp::McpMode::Edit});

    CHECK(response.ok);
    CHECK(response.result.value("operationId").toString() == "mcp-op-2");
  }

  SECTION("edit mode serves texture and face mutation tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::Edit}));

    for (const auto& toolName :
         {"texture_apply",
          "texture_lock_set",
          "texture_replace",
          "texture_align_face",
          "texture_copy_from_face",
          "face_texture_set"})
    {
      const auto params = toolName == QString{"texture_lock_set"}
                            ? QJsonObject{{"textureLock", true}, {"uvLock", true}}
                            : QJsonObject{{"material", "test"}};
      const auto response = server.dispatchRequest(
        mcp::McpBridgeRequest{"1", "secret", toolName, params, mcp::McpMode::Edit});
      CHECK(response.ok);
      if (toolName == QString{"texture_lock_set"})
      {
        CHECK(response.result.value("textureLock").toBool());
        CHECK(response.result.value("uvLock").toBool());
      }
      else
      {
        CHECK(response.result.value("operationId").toString() == "mcp-op-8");
      }
    }
  }

  SECTION("edit mode serves object mutation tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::Edit}));

    for (const auto& toolName : {"objects_delete", "objects_transform"})
    {
      const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
        "1",
        "secret",
        toolName,
        QJsonObject{
          {"objectIds", QJsonArray{"node:1"}},
          {"operation", "translate"},
          {"delta", QJsonArray{16, 0, 0}},
        },
        mcp::McpMode::Edit});
      CHECK(response.ok);
      CHECK(response.result.value("operationId").toString() == "mcp-op-9");
    }
  }

  SECTION("read-only mode serves validation tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto validateResponse = server.dispatchRequest(
      mcp::McpBridgeRequest{"1", "secret", "map_validate", {}, mcp::McpMode::ReadOnly});
    CHECK(validateResponse.ok);
    CHECK(validateResponse.result.value("valid").toBool());

    const auto problemsResponse = server.dispatchRequest(
      mcp::McpBridgeRequest{"2", "secret", "problems_check", {}, mcp::McpMode::ReadOnly});
    CHECK(problemsResponse.ok);
    CHECK(problemsResponse.result.value("count").toInt() == 0);
  }

  SECTION("read-only mode serves compile profile and log tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto profilesResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "1", "secret", "compile_profiles_list", {}, mcp::McpMode::ReadOnly});
    CHECK(profilesResponse.ok);
    CHECK(profilesResponse.result.value("count").toInt() == 1);

    const auto logResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "2", "secret", "compile_log_tail", {}, mcp::McpMode::ReadOnly});
    CHECK(logResponse.ok);
    CHECK(logResponse.result.value("lineCount").toInt() == 0);
  }

  SECTION("read-only mode rejects compile run and pointfile loading")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto compileResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "compile_run",
      QJsonObject{{"profile", "default"}},
      mcp::McpMode::Edit});
    CHECK(!compileResponse.ok);
    REQUIRE(compileResponse.error);
    CHECK(compileResponse.error->code == mcp::McpErrorCode::Forbidden);

    const auto pointfileResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "2",
      "secret",
      "leaks_load_pointfile",
      QJsonObject{{"path", "C:/tmp/test.pts"}},
      mcp::McpMode::Edit});
    CHECK(!pointfileResponse.ok);
    REQUIRE(pointfileResponse.error);
    CHECK(pointfileResponse.error->code == mcp::McpErrorCode::Forbidden);
  }

  SECTION("edit mode serves safe problem fix tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::Edit}));

    for (const auto& toolName : {"problems_fix", "map_fix_all_safe"})
    {
      const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
        "1",
        "secret",
        toolName,
        QJsonObject{
          {"problemIds", QJsonArray{"issue:1:node:1:0"}},
          {"quickFix", "Reset UV Scale"},
        },
        mcp::McpMode::Edit});
      CHECK(response.ok);
      CHECK(response.result.value("operationId").toString() == "mcp-op-10");
    }
  }

  SECTION("edit mode serves compile run and pointfile loading")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::Edit}));

    const auto compileResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "compile_run",
      QJsonObject{{"profile", "default"}},
      mcp::McpMode::Edit});
    CHECK(compileResponse.ok);
    CHECK(compileResponse.result.value("started").toBool());

    const auto pointfileResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "2",
      "secret",
      "leaks_load_pointfile",
      QJsonObject{{"path", "C:/tmp/test.pts"}},
      mcp::McpMode::Edit});
    CHECK(pointfileResponse.ok);
    CHECK(pointfileResponse.result.value("loaded").toBool());
  }

  SECTION("read-only mode serves blockout validation")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "blockout_validate",
      QJsonObject{
        {"type", "room"},
        {"min", QJsonArray{0, 0, 0}},
        {"max", QJsonArray{128, 128, 128}},
      },
      mcp::McpMode::ReadOnly});

    CHECK(response.ok);
    CHECK(response.result.value("valid").toBool());
  }

  SECTION("read-only mode serves geometry analysis and spiral validation")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    const auto analysisResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "1", "secret", "geometry_analyze_selection", {}, mcp::McpMode::ReadOnly});
    CHECK(analysisResponse.ok);
    CHECK(analysisResponse.result.value("invalidBrushCount").toInt() == 0);

    const auto validateResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "2",
      "secret",
      "blockout_validate_spiral_stairs",
      QJsonObject{},
      mcp::McpMode::ReadOnly});
    CHECK(validateResponse.ok);
    CHECK(validateResponse.result.value("valid").toBool());
  }

  SECTION("read-only mode serves operation detail tools")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::ReadOnly}));

    for (const auto& toolName :
         {"operation_inspect", "operation_select", "operation_validate"})
    {
      const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
        "1",
        "secret",
        toolName,
        QJsonObject{{"operationId", "mcp-op-3"}},
        mcp::McpMode::ReadOnly});
      CHECK(response.ok);
      CHECK(response.result.value("operationId").toString() == "mcp-op-3");
    }
  }

  SECTION("edit mode serves blockout creation")
  {
    REQUIRE(
      server.start(mcp::McpBridgeConfig{"test-pipe", "secret", mcp::McpMode::Edit}));

    const auto response = server.dispatchRequest(mcp::McpBridgeRequest{
      "1",
      "secret",
      "blockout_create_room",
      QJsonObject{
        {"min", QJsonArray{0, 0, 0}},
        {"max", QJsonArray{128, 128, 128}},
      },
      mcp::McpMode::Edit});

    CHECK(response.ok);
    CHECK(response.result.value("operationId").toString() == "mcp-op-3");

    const auto spiralResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "2",
      "secret",
      "blockout_create_spiral_stairs",
      QJsonObject{{"steps", 24}},
      mcp::McpMode::Edit});

    CHECK(spiralResponse.ok);
    CHECK(
      spiralResponse.result.value("transactionName").toString()
      == "MCP: Blockout spiral stairs");

    const auto batchResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "3",
      "secret",
      "blockout_create_batch",
      QJsonObject{{"operations", QJsonArray{QJsonObject{{"type", "box"}}}}},
      mcp::McpMode::Edit});
    CHECK(batchResponse.ok);
    CHECK(batchResponse.result.value("operationId").toString() == "mcp-op-11");

    const auto curvedResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "4",
      "secret",
      "blockout_create_curved_corridor",
      QJsonObject{{"center", QJsonArray{0, 0, 0}}},
      mcp::McpMode::Edit});
    CHECK(curvedResponse.ok);
    CHECK(curvedResponse.result.value("operationId").toString() == "mcp-op-11");

    const auto heightmapResponse = server.dispatchRequest(mcp::McpBridgeRequest{
      "5",
      "secret",
      "heightmap_import_grayscale",
      QJsonObject{{"imagePath", "C:/tmp/heightmap.png"}},
      mcp::McpMode::Edit});
    CHECK(heightmapResponse.ok);
    CHECK(heightmapResponse.result.value("operationId").toString() == "mcp-op-12");
  }

  SECTION("rejects nested dispatch")
  {
    auto* nestedServer = static_cast<McpBridgeServer*>(nullptr);
    auto nestedResponse = std::optional<mcp::McpBridgeResponse>{};
    auto guardedServer = McpBridgeServer{[&](
                                           const QString& toolName, const QJsonObject&) {
      if (toolName == "tb_status")
      {
        nestedResponse = nestedServer->dispatchRequest(mcp::McpBridgeRequest{
          "nested", "secret", "tb_doctor", {}, mcp::McpMode::ReadOnly});
        return McpBridgeToolResult::success(QJsonObject{{"application", "TrenchBroom"}});
      }
      return McpBridgeToolResult::success(QJsonObject{{"ok", true}});
    }};
    nestedServer = &guardedServer;
    REQUIRE(guardedServer.start(
      mcp::McpBridgeConfig{"test-pipe-nested", "secret", mcp::McpMode::ReadOnly}));

    const auto response = guardedServer.dispatchRequest(
      mcp::McpBridgeRequest{"outer", "secret", "tb_status", {}, mcp::McpMode::ReadOnly});

    REQUIRE(response.ok);
    REQUIRE(nestedResponse);
    CHECK(!nestedResponse->ok);
    REQUIRE(nestedResponse->error);
    CHECK(nestedResponse->error->code == mcp::McpErrorCode::Forbidden);
  }
}

TEST_CASE("McpBridgeServer spiral stair geometry tools")
{
  auto appControllerFixture = AppControllerFixture{};
  auto& appController = appControllerFixture.appController();
  auto document = MapDocument::createDocument(
                    appController.environmentConfig(),
                    mdl::QuakeGameInfo,
                    mdl::MapFormat::Valve,
                    vm::bbox3d{8192.0},
                    appController.taskManager(),
                    appController.glManager().resourceManager())
                  | kdl::value();
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;

  const auto createResponse = blockoutCreateSpiralStairsForMapResult(
    map,
    QJsonObject{
      {"center", QJsonArray{0, 0, 0}},
      {"innerRadius", 32},
      {"outerRadius", 128},
      {"steps", 24},
      {"stepHeight", 8},
      {"turnDegrees", 360},
      {"select", true},
    },
    history,
    nextOperationIndex);

  const auto createError =
    createResponse.ok ? std::string{} : createResponse.error.message.toStdString();
  INFO(createError);
  REQUIRE(createResponse.ok);
  CHECK(
    createResponse.result.value("transactionName").toString()
    == "MCP: Blockout spiral stairs");
  CHECK(createResponse.result.value("operationId").toString() == "mcp-op-1");
  CHECK(createResponse.result.value("brushCount").toInt() == 26);
  CHECK(map.selection().nodes.size() == 26u);

  const auto validation = createResponse.result.value("validation").toObject();
  CHECK(validation.value("valid").toBool());
  CHECK(validation.value("gapCount").toInt() == 0);
  CHECK(!validation.value("radiusMismatch").toBool());
  CHECK(validation.value("columnFits").toBool());
  CHECK(validation.value("landingConnected").toBool());
  CHECK(validation.value("invalidBrushCount").toInt() == 0);

  const auto analyzeResponse = geometryAnalyzeSelectionResult(
    map, QJsonObject{{"grid", 1}, {"includeVertices", false}});
  REQUIRE(analyzeResponse.ok);
  CHECK(analyzeResponse.result.value("brushCount").toInt() == 26);
  CHECK(analyzeResponse.result.value("invalidBrushCount").toInt() == 0);
  CHECK(analyzeResponse.result.value("nonGridAlignedCount").toInt() == 0);
  CHECK(analyzeResponse.result.value("detail").toString() == "summary");
  CHECK(analyzeResponse.result.value("brushes").isUndefined());
  CHECK(analyzeResponse.result.value("objectIds").isUndefined());

  const auto fullAnalyzeResponse = geometryAnalyzeSelectionResult(
    map,
    QJsonObject{
      {"grid", 1},
      {"detail", "full"},
      {"maxBrushes", 2},
      {"includeVertices", false},
    });
  REQUIRE(fullAnalyzeResponse.ok);
  CHECK(fullAnalyzeResponse.result.value("brushCount").toInt() == 26);
  CHECK(fullAnalyzeResponse.result.value("returnedBrushCount").toInt() == 2);
  CHECK(fullAnalyzeResponse.result.value("truncated").toBool());
  CHECK(fullAnalyzeResponse.result.value("brushes").toArray().size() == 2);

  const auto idsAnalyzeResponse = geometryAnalyzeSelectionResult(
    map, QJsonObject{{"grid", 1}, {"detail", "ids"}, {"maxBrushes", 3}});
  REQUIRE(idsAnalyzeResponse.ok);
  CHECK(idsAnalyzeResponse.result.value("brushCount").toInt() == 26);
  CHECK(idsAnalyzeResponse.result.value("returnedBrushCount").toInt() == 3);
  CHECK(idsAnalyzeResponse.result.value("truncated").toBool());
  CHECK(idsAnalyzeResponse.result.value("objectIds").toArray().size() == 3);
  CHECK(idsAnalyzeResponse.result.value("brushes").isUndefined());

  const auto validateResponse = blockoutValidateSpiralStairsResult(
    map,
    QJsonObject{
      {"operationId", createResponse.result.value("operationId").toString()},
      {"center", QJsonArray{0, 0, 0}},
      {"innerRadius", 32},
      {"outerRadius", 128},
      {"steps", 24},
      {"stepHeight", 8},
      {"turnDegrees", 360},
      {"grid", 1},
    },
    history);
  REQUIRE(validateResponse.ok);
  CHECK(validateResponse.result.value("valid").toBool());
  CHECK(validateResponse.result.value("source").toString() == "operationId");
  CHECK(validateResponse.result.value("brushCountMatches").toBool());
  CHECK(validateResponse.result.value("gapCount").toInt() == 0);
  CHECK(validateResponse.result.value("columnFits").toBool());
  CHECK(validateResponse.result.value("landingConnected").toBool());

  REQUIRE(map.undoCommandName() != nullptr);
  CHECK(QString::fromStdString(*map.undoCommandName()) == "MCP: Blockout spiral stairs");
  map.undoCommand();
  CHECK(map.worldNode().childCount() == 1u);
}

TEST_CASE("McpBridgeServer batch blockout tools")
{
  auto appControllerFixture = AppControllerFixture{};
  auto& appController = appControllerFixture.appController();
  auto document = MapDocument::createDocument(
                    appController.environmentConfig(),
                    mdl::QuakeGameInfo,
                    mdl::MapFormat::Valve,
                    vm::bbox3d{8192.0},
                    appController.taskManager(),
                    appController.glManager().resourceManager())
                  | kdl::value();
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;

  const auto batchResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"name", "MCP: Test batch"},
      {"grid", 16},
      {"select", true},
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{64, 64, 16}},
         },
         QJsonObject{
           {"type", "ramp"},
           {"min", QJsonArray{80, 0, 0}},
           {"max", QJsonArray{144, 64, 32}},
           {"axis", "x"},
         },
       }},
    },
    history,
    nextOperationIndex);

  const auto batchError =
    batchResponse.ok ? std::string{} : batchResponse.error.message.toStdString();
  INFO(batchError);
  REQUIRE(batchResponse.ok);
  const auto operationId = batchResponse.result.value("operationId").toString();
  CHECK(!operationId.isEmpty());
  CHECK(batchResponse.result.value("transactionName").toString() == "MCP: Test batch");
  CHECK(batchResponse.result.value("brushCount").toInt() == 2);
  CHECK(batchResponse.result.value("changedObjectCount").toInt() == 2);
  CHECK(batchResponse.result.value("grid").toDouble() == 16.0);
  CHECK(batchResponse.result.value("changedObjectIds").isUndefined());
  CHECK(batchResponse.result.value("resourceUri")
          .toString()
          .startsWith("tbmcp://operation/"));
  CHECK(batchResponse.result.value("validation").toObject().value("valid").toBool());
  CHECK(map.selection().nodes.size() == 2u);

  const auto descendantCountBeforeInvalid = map.worldNode().descendantCount();
  const auto invalidResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_batch",
    QJsonObject{
      {"operations",
       QJsonArray{
         QJsonObject{
           {"type", "box"},
           {"min", QJsonArray{0, 0, 0}},
           {"max", QJsonArray{0, 64, 16}},
         },
       }},
    },
    history,
    nextOperationIndex);
  REQUIRE(invalidResponse.ok);
  CHECK(!invalidResponse.result.value("validation").toObject().value("valid").toBool());
  CHECK(map.worldNode().descendantCount() == descendantCountBeforeInvalid);

  const auto inspectResponse = operationInspectResult(
    history, QJsonObject{{"operationId", operationId}, {"detail", "ids"}});
  REQUIRE(inspectResponse.ok);
  CHECK(inspectResponse.result.value("changedObjectIds").toArray().size() == 2);

  mdl::deselectAll(map);

  const auto curvedResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_curved_corridor",
    QJsonObject{
      {"center", QJsonArray{256, 0, 0}},
      {"innerRadius", 64},
      {"outerRadius", 128},
      {"startAngle", 0},
      {"turnDegrees", 90},
      {"height", 96},
      {"segments", 3},
      {"caps", "both"},
      {"select", true},
    },
    history,
    nextOperationIndex);

  const auto curvedError =
    curvedResponse.ok ? std::string{} : curvedResponse.error.message.toStdString();
  INFO(curvedError);
  REQUIRE(curvedResponse.ok);
  CHECK(curvedResponse.result.value("brushCount").toInt() == 14);
  CHECK(curvedResponse.result.value("changedObjectIds").isUndefined());
  CHECK(curvedResponse.result.value("validation").toObject().value("valid").toBool());
  CHECK(map.selection().nodes.size() == 14u);

  const auto radialCurvedResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_curved_corridor",
    QJsonObject{
      {"center", QJsonArray{512, 0, 0}},
      {"innerRadius", 64},
      {"outerRadius", 128},
      {"startAngle", 0},
      {"turnDegrees", 90},
      {"height", 96},
      {"segments", 3},
      {"caps", "none"},
      {"snapMode", "radial"},
      {"grid", 16},
      {"select", true},
      {"detail", "full"},
    },
    history,
    nextOperationIndex);

  const auto radialCurvedError = radialCurvedResponse.ok
                                   ? std::string{}
                                   : radialCurvedResponse.error.message.toStdString();
  INFO(radialCurvedError);
  REQUIRE(radialCurvedResponse.ok);
  CHECK(radialCurvedResponse.result.value("brushCount").toInt() == 12);
  CHECK(
    radialCurvedResponse.result.value("validation").toObject().value("valid").toBool());
  CHECK(radialCurvedResponse.result.value("grid").toDouble() == 16.0);
  CHECK(map.selection().nodes.size() == 12u);

  const auto radialInspectResponse = operationInspectResult(
    history,
    QJsonObject{
      {"operationId", radialCurvedResponse.result.value("operationId").toString()},
      {"detail", "full"},
    });
  REQUIRE(radialInspectResponse.ok);
  const auto radialInput = radialInspectResponse.result.value("operationDetail")
                             .toObject()
                             .value("input")
                             .toObject();
  const auto radialOperation =
    radialInput.value("operations").toArray().first().toObject();
  CHECK(radialOperation.value("snapMode").toString() == "radial");
  CHECK(radialInput.value("grid").toDouble() == 16.0);

  const auto invalidSnapResponse = blockoutCreateBatchForMapResult(
    map,
    "blockout_create_curved_corridor",
    QJsonObject{
      {"center", QJsonArray{768, 0, 0}},
      {"innerRadius", 64},
      {"outerRadius", 128},
      {"turnDegrees", 90},
      {"snapMode", "wobbly"},
    },
    history,
    nextOperationIndex);
  REQUIRE(invalidSnapResponse.ok);
  const auto invalidSnapValidation =
    invalidSnapResponse.result.value("validation").toObject();
  CHECK(!invalidSnapValidation.value("valid").toBool());
  CHECK(invalidSnapValidation.value("errors").toArray().first().toString().contains(
    "snapMode"));
}

TEST_CASE("McpBridgeServer Python blockout tools")
{
  auto appControllerFixture = AppControllerFixture{};
  auto& appController = appControllerFixture.appController();
  auto document = MapDocument::createDocument(
                    appController.environmentConfig(),
                    mdl::QuakeGameInfo,
                    mdl::MapFormat::Valve,
                    vm::bbox3d{8192.0},
                    appController.taskManager(),
                    appController.glManager().resourceManager())
                  | kdl::value();
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;

  SECTION("compiles operations printed by Python")
  {
    const auto response = pythonGenerateBlockoutForMapResult(
      map,
      "python_generate_blockout",
      QJsonObject{
        {"name", "MCP: Python test blockout"},
        {"grid", 16},
        {"select", true},
        {"detail", "summary"},
        {"script",
         R"PY(
import json
print(json.dumps({
    "operations": [
        {"type": "box", "min": [0, 0, 0], "max": [64, 64, 16]}
    ]
}))
)PY"},
      },
      history,
      nextOperationIndex);

    const auto error = response.ok ? std::string{} : response.error.message.toStdString();
    INFO(error);
    REQUIRE(response.ok);
    CHECK(
      response.result.value("transactionName").toString() == "MCP: Python test blockout");
    CHECK(response.result.value("brushCount").toInt() == 1);
    CHECK(response.result.value("changedObjectCount").toInt() == 1);
    CHECK(response.result.value("changedObjectIds").isUndefined());
    CHECK(response.result.value("validation").toObject().value("valid").toBool());
    CHECK(response.result.value("python").toObject().value("stdoutBytes").toInt() > 0);
    CHECK(map.selection().nodes.size() == 1u);
  }

  SECTION("rejects invalid JSON without committing")
  {
    const auto descendantCount = map.worldNode().descendantCount();
    const auto response = pythonGenerateBlockoutForMapResult(
      map,
      "python_generate_blockout",
      QJsonObject{{"script", "print('not json')"}},
      history,
      nextOperationIndex);

    CHECK(!response.ok);
    CHECK(response.error.code == mcp::McpErrorCode::InvalidParams);
    CHECK(response.error.details.value("valid").toBool(true) == false);
    CHECK(map.worldNode().descendantCount() == descendantCount);
  }

  SECTION("requires operations array")
  {
    const auto descendantCount = map.worldNode().descendantCount();
    const auto response = pythonGenerateBlockoutForMapResult(
      map,
      "python_generate_blockout",
      QJsonObject{{"script", "print('{}')"}},
      history,
      nextOperationIndex);

    CHECK(!response.ok);
    CHECK(response.error.code == mcp::McpErrorCode::InvalidParams);
    CHECK(response.error.details.value("message").toString().contains("operations"));
    CHECK(map.worldNode().descendantCount() == descendantCount);
  }

  SECTION("reports nonzero Python exit")
  {
    const auto descendantCount = map.worldNode().descendantCount();
    const auto response = pythonGenerateBlockoutForMapResult(
      map,
      "python_generate_blockout",
      QJsonObject{
        {"script",
         R"PY(
import sys
print("script failed", file=sys.stderr)
sys.exit(7)
)PY"},
      },
      history,
      nextOperationIndex);

    CHECK(!response.ok);
    CHECK(response.error.code == mcp::McpErrorCode::InvalidParams);
    CHECK(response.error.details.value("stderr").toString().contains("script failed"));
    CHECK(map.worldNode().descendantCount() == descendantCount);
  }

  SECTION("kills timed out Python")
  {
    const auto descendantCount = map.worldNode().descendantCount();
    const auto response = pythonGenerateBlockoutForMapResult(
      map,
      "python_generate_blockout",
      QJsonObject{
        {"timeoutMs", 50},
        {"script",
         R"PY(
import time
time.sleep(2)
)PY"},
      },
      history,
      nextOperationIndex);

    CHECK(!response.ok);
    CHECK(response.error.code == mcp::McpErrorCode::InternalError);
    CHECK(response.error.message.contains("timed out"));
    CHECK(map.worldNode().descendantCount() == descendantCount);
  }

  SECTION("does not commit invalid generated operations")
  {
    const auto descendantCount = map.worldNode().descendantCount();
    const auto response = pythonGenerateBlockoutForMapResult(
      map,
      "python_generate_blockout",
      QJsonObject{
        {"script",
         R"PY(
import json
print(json.dumps({
    "operations": [
        {"type": "box", "min": [0, 0, 0], "max": [0, 64, 16]}
    ]
}))
)PY"},
      },
      history,
      nextOperationIndex);

    REQUIRE(response.ok);
    CHECK(!response.result.value("validation").toObject().value("valid").toBool());
    CHECK(map.worldNode().descendantCount() == descendantCount);
  }
}

TEST_CASE("McpBridgeServer grayscale heightmap import tool")
{
  auto appControllerFixture = AppControllerFixture{};
  auto& appController = appControllerFixture.appController();
  auto document = MapDocument::createDocument(
                    appController.environmentConfig(),
                    mdl::QuakeGameInfo,
                    mdl::MapFormat::Valve,
                    vm::bbox3d{8192.0},
                    appController.taskManager(),
                    appController.glManager().resourceManager())
                  | kdl::value();
  auto& map = document->map();
  auto history = std::vector<McpOperationRecord>{};
  auto nextOperationIndex = 1;

  auto tempDir = QTemporaryDir{};
  REQUIRE(tempDir.isValid());

  const auto saveImage = [&](const QString& fileName, const QImage& image) {
    const auto path = tempDir.filePath(fileName);
    REQUIRE(image.save(path));
    return path;
  };

  SECTION("imports terraced merged brushes from a grayscale image")
  {
    auto image = QImage{2, 2, QImage::Format_RGB32};
    image.setPixelColor(0, 0, QColor{0, 0, 0});
    image.setPixelColor(1, 0, QColor{128, 128, 128});
    image.setPixelColor(0, 1, QColor{128, 128, 128});
    image.setPixelColor(1, 1, QColor{255, 255, 255});
    const auto imagePath = saveImage("heightmap.png", image);

    const auto response = heightmapImportGrayscaleForMapResult(
      map,
      "heightmap_import_grayscale",
      QJsonObject{
        {"imagePath", imagePath},
        {"origin", QJsonArray{0, 0, 0}},
        {"cellSize", 32},
        {"heightScale", 64},
        {"heightSteps", 4},
        {"maxSize", 4},
        {"maxBrushes", 16},
        {"select", true},
        {"detail", "summary"},
      },
      history,
      nextOperationIndex);

    const auto error = response.ok ? std::string{} : response.error.message.toStdString();
    INFO(error);
    REQUIRE(response.ok);
    CHECK(
      response.result.value("transactionName").toString()
      == "MCP: Import grayscale heightmap");
    CHECK(response.result.value("operationId").toString() == "mcp-op-1");
    CHECK(response.result.value("brushCount").toInt() == 3);
    CHECK(response.result.value("changedObjectCount").toInt() == 3);
    CHECK(response.result.value("changedObjectIds").isUndefined());
    CHECK(response.result.value("validation").toObject().value("valid").toBool());
    CHECK(
      response.result.value("heightmap").toObject().value("sourceWidth").toInt() == 2);
    CHECK(
      response.result.value("heightmap").toObject().value("sampledHeight").toInt() == 2);
    CHECK(
      response.result.value("heightmap")
        .toObject()
        .value("skippedZeroHeightCells")
        .toInt()
      == 1);
    CHECK(map.selection().nodes.size() == 3u);

    REQUIRE(map.undoCommandName() != nullptr);
    CHECK(
      QString::fromStdString(*map.undoCommandName())
      == "MCP: Import grayscale heightmap");
  }

  SECTION("rejects missing image path without committing")
  {
    const auto descendantCount = map.worldNode().descendantCount();
    const auto response = heightmapImportGrayscaleForMapResult(
      map,
      "heightmap_import_grayscale",
      QJsonObject{{"imagePath", tempDir.filePath("missing.png")}},
      history,
      nextOperationIndex);

    CHECK(!response.ok);
    CHECK(response.error.code == mcp::McpErrorCode::InvalidParams);
    CHECK(map.worldNode().descendantCount() == descendantCount);
  }

  SECTION("reports empty all-black heightmap without committing")
  {
    auto image = QImage{4, 4, QImage::Format_RGB32};
    image.fill(QColor{0, 0, 0});
    const auto imagePath = saveImage("black.png", image);

    const auto descendantCount = map.worldNode().descendantCount();
    const auto response = heightmapImportGrayscaleForMapResult(
      map,
      "heightmap_import_grayscale",
      QJsonObject{{"imagePath", imagePath}},
      history,
      nextOperationIndex);

    REQUIRE(response.ok);
    CHECK(!response.result.value("validation").toObject().value("valid").toBool());
    CHECK(
      response.result.value("heightmap").toObject().value("mergedBrushCount").toInt()
      == 0);
    CHECK(map.worldNode().descendantCount() == descendantCount);
  }

  SECTION("imports adaptive surface brushes from a grayscale image")
  {
    auto image = QImage{3, 3, QImage::Format_RGB32};
    image.setPixelColor(0, 0, QColor{0, 0, 0});
    image.setPixelColor(1, 0, QColor{96, 96, 96});
    image.setPixelColor(2, 0, QColor{32, 32, 32});
    image.setPixelColor(0, 1, QColor{128, 128, 128});
    image.setPixelColor(1, 1, QColor{255, 255, 255});
    image.setPixelColor(2, 1, QColor{96, 96, 96});
    image.setPixelColor(0, 2, QColor{32, 32, 32});
    image.setPixelColor(1, 2, QColor{96, 96, 96});
    image.setPixelColor(2, 2, QColor{0, 0, 0});
    const auto imagePath = saveImage("adaptive.png", image);

    const auto response = heightmapImportGrayscaleForMapResult(
      map,
      "heightmap_import_grayscale",
      QJsonObject{
        {"imagePath", imagePath},
        {"mode", "adaptive_surface"},
        {"origin", QJsonArray{0, 0, 0}},
        {"cellSize", 32},
        {"heightScale", 64},
        {"maxSize", 2},
        {"minCellSize", 32},
        {"maxCellSize", 64},
        {"errorTolerance", 0},
        {"maxBrushes", 16},
        {"select", true},
        {"detail", "summary"},
      },
      history,
      nextOperationIndex);

    const auto error = response.ok ? std::string{} : response.error.message.toStdString();
    INFO(error);
    REQUIRE(response.ok);
    CHECK(response.result.value("validation").toObject().value("valid").toBool());
    CHECK(response.result.value("brushCount").toInt() > 0);
    CHECK(response.result.value("changedObjectIds").isUndefined());

    const auto heightmap = response.result.value("heightmap").toObject();
    CHECK(heightmap.value("mode").toString() == "adaptive_surface");
    CHECK(heightmap.value("surfaceCellCount").toInt() > 0);
    CHECK(
      heightmap.value("triangleBrushCount").toInt()
      == response.result.value("brushCount").toInt());
    CHECK(heightmap.value("minCellSize").toDouble() == 32.0);
    CHECK(heightmap.value("maxCellSize").toDouble() == 64.0);
    CHECK(
      map.selection().nodes.size()
      == static_cast<size_t>(response.result.value("brushCount").toInt()));
  }

  SECTION("rejects excessive brush count without committing")
  {
    auto image = QImage{4, 4, QImage::Format_RGB32};
    for (auto y = 0; y < image.height(); ++y)
    {
      for (auto x = 0; x < image.width(); ++x)
      {
        const auto value = ((x + y) % 2 == 0) ? 64 : 192;
        image.setPixelColor(x, y, QColor{value, value, value});
      }
    }
    const auto imagePath = saveImage("checker.png", image);

    const auto descendantCount = map.worldNode().descendantCount();
    const auto response = heightmapImportGrayscaleForMapResult(
      map,
      "heightmap_import_grayscale",
      QJsonObject{
        {"imagePath", imagePath},
        {"heightSteps", 8},
        {"maxSize", 4},
        {"maxBrushes", 4},
      },
      history,
      nextOperationIndex);

    REQUIRE(response.ok);
    CHECK(!response.result.value("validation").toObject().value("valid").toBool());
    CHECK(
      response.result.value("heightmap").toObject().value("mergedBrushCount").toInt()
      > 4);
    CHECK(map.worldNode().descendantCount() == descendantCount);
  }

  SECTION("rejects excessive adaptive surface brush count without committing")
  {
    auto image = QImage{4, 4, QImage::Format_RGB32};
    for (auto y = 0; y < image.height(); ++y)
    {
      for (auto x = 0; x < image.width(); ++x)
      {
        const auto value = ((x + y) % 2 == 0) ? 64 : 192;
        image.setPixelColor(x, y, QColor{value, value, value});
      }
    }
    const auto imagePath = saveImage("adaptive_checker.png", image);

    const auto descendantCount = map.worldNode().descendantCount();
    const auto response = heightmapImportGrayscaleForMapResult(
      map,
      "heightmap_import_grayscale",
      QJsonObject{
        {"imagePath", imagePath},
        {"mode", "adaptive_surface"},
        {"cellSize", 32},
        {"heightScale", 64},
        {"maxSize", 4},
        {"minCellSize", 32},
        {"maxCellSize", 32},
        {"errorTolerance", 0},
        {"maxBrushes", 4},
      },
      history,
      nextOperationIndex);

    REQUIRE(response.ok);
    CHECK(!response.result.value("validation").toObject().value("valid").toBool());
    CHECK(
      response.result.value("heightmap").toObject().value("triangleBrushCount").toInt()
      > 4);
    CHECK(map.worldNode().descendantCount() == descendantCount);
  }
}

} // namespace tb::ui
