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

#include <QJsonArray>
#include <QJsonObject>

#include "McpBridgeServerTools.h"
#include "fs/PathMatcher.h"
#include "fs/TraversalMode.h"
#include "mcp/McpError.h"
#include "mdl/AddRemoveNodesCommand.h"
#include "mdl/Brush.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/GameFileSystem.h"
#include "mdl/Map.h"
#include "mdl/Map_Assets.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Map_World.h"
#include "mdl/Node.h"
#include "mdl/PatchNode.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/AssetBrowserModel.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <map>
#include <optional>

namespace tb::ui
{
namespace mcp = tb::mcp;

namespace
{

QString pathToQString(const std::filesystem::path& path)
{
  return path.empty() ? QString{} : pathAsQString(path);
}

QString genericPathToQString(const std::filesystem::path& path)
{
  return pathAsGenericQString(path);
}

QString browserCellTypeName(const BrowserCellType type)
{
  switch (type)
  {
  case BrowserCellType::Folder:
    return "folder";
  case BrowserCellType::Model:
    return "model";
  case BrowserCellType::Sprite:
    return "sprite";
  case BrowserCellType::Sound:
    return "sound";
  }
  return "unknown";
}

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

QString nodeTypeName(const mdl::Node& node)
{
  if (dynamic_cast<const mdl::WorldNode*>(&node) != nullptr)
  {
    return "world";
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

QString makeOperationId(int& nextOperationIndex)
{
  return QString{"mcp-op-%1"}.arg(nextOperationIndex++);
}

QJsonObject mutationResultJson(const McpOperationRecord& operation)
{
  auto result = QJsonObject{};
  result.insert("operationId", operation.operationId);
  result.insert("transactionName", operation.transactionName);
  result.insert("changedObjectIds", operation.changedObjectIdsJson());
  result.insert("changedObjectCount", operation.changedObjectIds.size());
  return result;
}

void recordOperation(
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const QString& toolName,
  const QString& transactionName,
  const QJsonArray& changedObjectIds,
  QJsonObject& result)
{
  auto operation = McpOperationRecord{};
  operation.operationId = makeOperationId(nextOperationIndex);
  operation.toolName = toolName;
  operation.transactionName = transactionName;
  operation.setChangedObjectIds(changedObjectIds);
  result = mutationResultJson(operation);
  history.push_back(std::move(operation));
}

std::optional<vm::vec3d> vec3FromJson(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto value = params.value(key);
  if (!value.isArray())
  {
    error = QString{"%1 must be an array of three numbers"}.arg(key);
    return std::nullopt;
  }

  const auto array = value.toArray();
  if (array.size() != 3)
  {
    error = QString{"%1 must contain exactly three numbers"}.arg(key);
    return std::nullopt;
  }

  auto components = std::array<double, 3>{};
  for (auto i = 0; i < 3; ++i)
  {
    if (!array[i].isDouble())
    {
      error = QString{"%1[%2] must be a number"}.arg(key).arg(i);
      return std::nullopt;
    }
    components[static_cast<size_t>(i)] = array[i].toDouble();
  }

  return vm::vec3d{components[0], components[1], components[2]};
}

std::string optionalString(
  const QJsonObject& params, const QString& key, const std::string& defaultValue = {})
{
  const auto value = params.value(key);
  return value.isString() ? value.toString().toStdString() : defaultValue;
}

bool optionalBool(const QJsonObject& params, const QString& key, const bool defaultValue)
{
  const auto value = params.value(key);
  return value.isBool() ? value.toBool() : defaultValue;
}

std::optional<QJsonArray> addNodesWithTransaction(
  mdl::Map& map,
  const QString& transactionName,
  const std::vector<mdl::Node*>& nodes,
  const bool selectCreated)
{
  auto addedNodes = std::vector<mdl::Node*>{};
  auto transaction = mdl::Transaction{map, transactionName.toStdString()};

  if (selectCreated)
  {
    mdl::deselectAll(map);
  }

  auto* parent = mdl::parentForNodes(map);
  if (!parent || !parent->canAddChildren(std::begin(nodes), std::end(nodes)))
  {
    transaction.cancel();
    return std::nullopt;
  }

  auto nodesToAdd = std::map<mdl::Node*, std::vector<mdl::Node*>>{};
  nodesToAdd.emplace(parent, nodes);
  if (!map.executeAndStore(mdl::AddRemoveNodesCommand::add(nodesToAdd)))
  {
    transaction.cancel();
    return std::nullopt;
  }

  addedNodes = nodes;
  if (selectCreated)
  {
    mdl::selectNodes(map, addedNodes);
  }
  if (!transaction.commit())
  {
    return std::nullopt;
  }

  auto result = QJsonArray{};
  for (const auto* node : addedNodes)
  {
    result.push_back(nodePathId(*node, map.worldNode()));
  }
  return result;
}

std::optional<BrowserCellType> browserCellTypeFromString(const QString& type)
{
  if (type.compare("model", Qt::CaseInsensitive) == 0)
  {
    return BrowserCellType::Model;
  }
  if (type.compare("sprite", Qt::CaseInsensitive) == 0)
  {
    return BrowserCellType::Sprite;
  }
  if (type.compare("sound", Qt::CaseInsensitive) == 0)
  {
    return BrowserCellType::Sound;
  }
  return std::nullopt;
}

std::optional<std::vector<BrowserAsset>> collectMcpAssets(mdl::Map& map)
{
  const auto enabledMods = mdl::enabledMods(map);
  if (enabledMods.empty())
  {
    return std::vector<BrowserAsset>{};
  }

  auto modRoots = std::vector<std::filesystem::path>{};
  modRoots.reserve(enabledMods.size());
  for (const auto& mod : enabledMods)
  {
    modRoots.push_back((map.gamePath() / std::filesystem::path{mod}).lexically_normal());
  }

  const auto& fs = map.gameFileSystem();
  return collectBrowserAssets(
    {},
    modRoots,
    [&](const auto& rootPath) {
      return fs.find(
        rootPath,
        fs::TraversalMode::Recursive,
        fs::makeExtensionPathMatcher(goldSrcAssetExtensions()));
    },
    [&](const auto& path) { return fs.makeAbsolute(path); });
}

QJsonObject assetJson(const BrowserAsset& asset)
{
  auto json = QJsonObject{
    {"type", browserCellTypeName(asset.type)},
    {"path", genericPathToQString(asset.path)},
    {"absolutePath", pathToQString(asset.absolutePath)},
    {"displayName", QString::fromStdString(asset.displayName)},
  };

  if (!asset.path.empty())
  {
    json.insert("sourceRoot", pathAsGenericQString(*asset.path.begin()));
  }
  if (asset.lastModified)
  {
    json.insert(
      "lastModified", QString::number(asset.lastModified->time_since_epoch().count()));
  }

  return json;
}

} // namespace

McpBridgeToolResult assetSearchResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto& map = mapWindow->document().map();
  const auto assets = collectMcpAssets(map);
  if (!assets)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not scan assets");
  }

  const auto query = params.value("query").toString().trimmed();
  const auto typeText = params.value("type").toString().trimmed();
  const auto type = typeText.isEmpty() ? std::optional<BrowserCellType>{}
                                       : browserCellTypeFromString(typeText);
  if (!typeText.isEmpty() && !type)
  {
    return invalidParamsFailure("type must be model, sprite, or sound");
  }

  const auto limit = std::max(1, params.value("limit").toInt(50));
  auto results = QJsonArray{};
  for (const auto& asset : *assets)
  {
    if (type && asset.type != *type)
    {
      continue;
    }

    const auto pathText = genericPathToQString(asset.path);
    const auto displayName = QString::fromStdString(asset.displayName);
    if (
      !query.isEmpty() && !pathText.contains(query, Qt::CaseInsensitive)
      && !displayName.contains(query, Qt::CaseInsensitive))
    {
      continue;
    }

    results.push_back(assetJson(asset));
    if (results.size() >= limit)
    {
      break;
    }
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"query", query},
    {"type", type ? browserCellTypeName(*type) : QString{}},
    {"results", results},
    {"count", results.size()},
  });
}

McpBridgeToolResult placeAssetResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  const auto path = params.value("path").toString().trimmed();
  if (path.isEmpty())
  {
    return invalidParamsFailure(QString{"%1 requires path"}.arg(toolName));
  }

  auto assetType = BrowserCellType::Model;
  auto defaultClassname = std::string{"cycler_sprite"};
  auto defaultProperty = std::string{"model"};
  auto transactionLabel = QString{"model"};
  if (toolName == "asset_place_sprite")
  {
    assetType = BrowserCellType::Sprite;
    defaultClassname = "cycler_sprite";
    defaultProperty = "model";
    transactionLabel = "sprite";
  }
  else if (toolName == "asset_place_sound")
  {
    assetType = BrowserCellType::Sound;
    defaultClassname = "ambient_generic";
    defaultProperty = "message";
    transactionLabel = "sound";
  }

  const auto actualType =
    assetTypeForExtension(std::filesystem::path{path.toStdString()});
  if (actualType != assetType)
  {
    return invalidParamsFailure(
      QString{"path does not match %1 asset type"}.arg(browserCellTypeName(assetType)));
  }

  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto error = QString{};
  auto entity = mdl::Entity{
    {{mdl::EntityPropertyKeys::Classname,
      optionalString(params, "classname", defaultClassname)}}};
  entity.addOrUpdateProperty(
    optionalString(params, "property", defaultProperty), path.toStdString());

  if (const auto origin = params.value("origin"); !origin.isUndefined())
  {
    const auto originVec = vec3FromJson(params, "origin", error);
    if (!originVec)
    {
      return invalidParamsFailure(error);
    }
    entity.setOrigin(*originVec);
  }

  auto& map = mapWindow->document().map();
  auto* entityNode = new mdl::EntityNode{std::move(entity)};
  const auto transactionName = QString{"MCP: Place %1 asset"}.arg(transactionLabel);
  const auto changedObjectIds = addNodesWithTransaction(
    map, transactionName, {entityNode}, optionalBool(params, "select", true));
  if (!changedObjectIds)
  {
    delete entityNode;
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not place asset entity");
  }

  auto result = QJsonObject{};
  recordOperation(
    history, nextOperationIndex, toolName, transactionName, *changedObjectIds, result);
  result.insert("entity", nodeSummaryJson(*entityNode, map.worldNode()));
  result.insert("assetPath", path);
  return McpBridgeToolResult::success(std::move(result));
}

} // namespace tb::ui
