/*
 Copyright (C) 2026

 This file is part of TrenchBroom.

 TrenchBroom is free software:
 * you can redistribute it and/or modify
 it under the terms of the GNU General Public
 * License as published by
 the Free Software Foundation, either version 3 of the License,
 * or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that
 * it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of

 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public
 * License for more details.

 You should have received a copy of the GNU General Public
 * License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include "McpBridgeServerTools.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "gl/Material.h"
#include "gl/MaterialManager.h"
#include "mcp/McpError.h"
#include "mdl/Brush.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushFaceHandle.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/Map.h"
#include "mdl/Map_Brushes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Node.h"
#include "mdl/NodeQueries.h"
#include "mdl/Selection.h"
#include "mdl/Transaction.h"
#include "mdl/UpdateBrushFaceAttributes.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"

#include "kd/string_compare.h"

#include "vm/bbox.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <vector>

namespace tb::ui
{
namespace mcp = tb::mcp;

namespace
{

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

QString pathToQString(const std::filesystem::path& path)
{
  return path.empty() ? QString{} : pathAsQString(path);
}

QString genericPathToQString(const std::filesystem::path& path)
{
  return pathAsGenericQString(path);
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

std::optional<mdl::NodePath> parseNodePathId(const QString& id)
{
  if (id == "node:world")
  {
    return mdl::NodePath{};
  }

  static const auto Prefix = QString{"node:"};
  if (!id.startsWith(Prefix))
  {
    return std::nullopt;
  }

  auto path = mdl::NodePath{};
  for (const auto& part : id.mid(Prefix.size()).split('/', Qt::SkipEmptyParts))
  {
    auto ok = false;
    const auto index = part.toULongLong(&ok);
    if (!ok)
    {
      return std::nullopt;
    }
    path.indices.push_back(static_cast<std::size_t>(index));
  }
  return path;
}

mdl::Node* resolveNodeId(mdl::WorldNode& worldNode, const QString& id)
{
  const auto path = parseNodePathId(id);
  if (!path)
  {
    return nullptr;
  }
  return worldNode.resolvePath(*path);
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

void mcpRecordOperation(
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

size_t optionalSize(
  const QJsonObject& params, const QString& key, const size_t defaultValue)
{
  const auto value = params.value(key);
  if (!value.isDouble())
  {
    return defaultValue;
  }
  return static_cast<size_t>(std::max(0, value.toInt()));
}

bool executeTransaction(
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

} // namespace

QJsonObject materialJson(const gl::Material& material)
{
  return QJsonObject{
    {"name", QString::fromStdString(material.name())},
    {"collection", QString::fromStdString(material.collectionName())},
    {"relativePath", genericPathToQString(material.relativePath())},
    {"absolutePath", pathToQString(material.absolutePath())},
    {"usageCount", static_cast<int>(material.usageCount())},
  };
}

QJsonObject brushFaceAttributesJson(const mdl::BrushFaceAttributes& attributes)
{
  return QJsonObject{
    {"material", QString::fromStdString(attributes.materialName())},
    {"xOffset", attributes.xOffset()},
    {"yOffset", attributes.yOffset()},
    {"xScale", attributes.xScale()},
    {"yScale", attributes.yScale()},
    {"rotation", attributes.rotation()},
  };
}

vm::bbox3d brushFaceBounds(const mdl::BrushFace& face)
{
  auto vertices = face.vertexPositions();
  if (vertices.empty())
  {
    return vm::bbox3d{};
  }

  auto bounds = vm::bbox3d{vertices.front(), vertices.front()};
  for (const auto& vertex : vertices)
  {
    bounds = vm::merge(bounds, vertex);
  }
  return bounds;
}

QJsonObject brushFaceJson(
  const mdl::BrushFaceHandle& handle, const mdl::WorldNode& worldNode)
{
  const auto& face = handle.face();
  return QJsonObject{
    {"objectId", nodePathId(*handle.node(), worldNode)},
    {"faceIndex", static_cast<int>(handle.faceIndex())},
    {"material", QString::fromStdString(face.attributes().materialName())},
    {"normal", vecToJson(face.normal())},
    {"bounds", boundsToJson(brushFaceBounds(face))},
    {"attributes", brushFaceAttributesJson(face.attributes())},
  };
}

McpBridgeToolResult textureSearchResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  const auto query = params.value("query").toString().trimmed();
  const auto limit = std::max(1, params.value("limit").toInt(50));
  auto results = QJsonArray{};

  const auto& materials = mapWindow->document().map().materialManager().materials();
  for (const auto* material : materials)
  {
    if (!material)
    {
      continue;
    }
    const auto name = QString::fromStdString(material->name());
    const auto relativePath = genericPathToQString(material->relativePath());
    if (
      !query.isEmpty() && !name.contains(query, Qt::CaseInsensitive)
      && !relativePath.contains(query, Qt::CaseInsensitive))
    {
      continue;
    }

    results.push_back(materialJson(*material));
    if (results.size() >= limit)
    {
      break;
    }
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"query", query},
    {"results", results},
    {"count", results.size()},
  });
}

QJsonObject textureLockJson(mdl::Map& map)
{
  const auto& editorContext = map.editorContext();
  return QJsonObject{
    {"textureLock", editorContext.alignmentLock()},
    {"uvLock", editorContext.uvLock()},
  };
}

McpBridgeToolResult textureLockGetResult(AppController& appController)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return McpBridgeToolResult::success(textureLockJson(mapWindow->document().map()));
}

McpBridgeToolResult textureLockSetResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  const auto textureLockValue = params.value("textureLock");
  const auto uvLockValue = params.value("uvLock");
  if (textureLockValue.isUndefined() && uvLockValue.isUndefined())
  {
    return invalidParamsFailure("texture_lock_set requires textureLock or uvLock");
  }
  if (!textureLockValue.isUndefined() && !textureLockValue.isBool())
  {
    return invalidParamsFailure("textureLock must be a boolean");
  }
  if (!uvLockValue.isUndefined() && !uvLockValue.isBool())
  {
    return invalidParamsFailure("uvLock must be a boolean");
  }

  auto& map = mapWindow->document().map();
  auto& editorContext = map.editorContext();
  if (textureLockValue.isBool())
  {
    const auto textureLock = textureLockValue.toBool();
    setPref(Preferences::AlignmentLock, textureLock);
    editorContext.setAlignmentLock(textureLock);
  }
  if (uvLockValue.isBool())
  {
    const auto uvLock = uvLockValue.toBool();
    setPref(Preferences::UVLock, uvLock);
    editorContext.setUVLock(uvLock);
  }

  auto result = textureLockJson(map);
  result.insert("changed", true);
  return McpBridgeToolResult::success(std::move(result));
}

std::optional<mdl::BrushFaceHandle> brushFaceHandleFromJson(
  mdl::Map& map,
  const QJsonObject& params,
  const QString& objectIdKey,
  const QString& faceIndexKey,
  QString& error)
{
  const auto objectId = params.value(objectIdKey).toString().trimmed();
  if (objectId.isEmpty())
  {
    error = QString{"%1 is required"}.arg(objectIdKey);
    return std::nullopt;
  }

  auto* node = resolveNodeId(map.worldNode(), objectId);
  auto* brushNode = dynamic_cast<mdl::BrushNode*>(node);
  if (!brushNode)
  {
    error = QString{"%1 is not a brush: %2"}.arg(objectIdKey, objectId);
    return std::nullopt;
  }

  const auto faceIndexValue = params.value(faceIndexKey);
  if (!faceIndexValue.isDouble())
  {
    error = QString{"%1 must be an integer"}.arg(faceIndexKey);
    return std::nullopt;
  }

  const auto faceIndex = static_cast<size_t>(faceIndexValue.toInt());
  if (faceIndex >= brushNode->brush().faceCount())
  {
    error = QString{"%1 is out of range"}.arg(faceIndexKey);
    return std::nullopt;
  }

  return mdl::BrushFaceHandle{brushNode, faceIndex};
}

std::vector<mdl::BrushFaceHandle> brushFaceHandlesFromParamsOrSelection(
  mdl::Map& map, const QJsonObject& params, QString& error)
{
  auto result = std::vector<mdl::BrushFaceHandle>{};
  const auto objectId = params.value("objectId").toString().trimmed();
  if (!objectId.isEmpty())
  {
    auto* node = resolveNodeId(map.worldNode(), objectId);
    auto* brushNode = dynamic_cast<mdl::BrushNode*>(node);
    if (!brushNode)
    {
      error = QString{"objectId is not a brush: %1"}.arg(objectId);
      return {};
    }

    const auto faceIndexValue = params.value("faceIndex");
    if (faceIndexValue.isUndefined())
    {
      return mdl::toHandles(brushNode);
    }

    if (!faceIndexValue.isDouble())
    {
      error = "faceIndex must be an integer";
      return {};
    }
    const auto faceIndex = static_cast<size_t>(faceIndexValue.toInt());
    if (faceIndex >= brushNode->brush().faceCount())
    {
      error = "faceIndex is out of range";
      return {};
    }
    result.emplace_back(brushNode, faceIndex);
    return result;
  }

  if (!map.selection().brushFaces.empty())
  {
    return map.selection().brushFaces;
  }

  for (auto* brushNode : map.selection().brushes)
  {
    const auto handles = mdl::toHandles(brushNode);
    result.insert(std::end(result), std::begin(handles), std::end(handles));
  }

  if (result.empty())
  {
    error = "tool requires objectId or selected brush faces/brushes";
  }
  return result;
}

std::vector<mdl::BrushFaceHandle> brushFaceHandlesFromFacesArray(
  mdl::Map& map, const QJsonArray& faces, QString& error)
{
  auto result = std::vector<mdl::BrushFaceHandle>{};
  for (const auto& faceValue : faces)
  {
    if (!faceValue.isObject())
    {
      error = "faces must contain objects";
      return {};
    }
    const auto faceObject = faceValue.toObject();
    auto handle =
      brushFaceHandleFromJson(map, faceObject, "objectId", "faceIndex", error);
    if (!handle)
    {
      return {};
    }
    result.push_back(*handle);
  }
  return result;
}

std::vector<mdl::BrushFaceHandle> faceSelectionHandlesFromParams(
  mdl::Map& map, const QJsonObject& params, QString& error)
{
  if (params.value("faces").isArray())
  {
    auto handles =
      brushFaceHandlesFromFacesArray(map, params.value("faces").toArray(), error);
    if (handles.empty() && error.isEmpty())
    {
      error = "faces must not be empty";
    }
    return handles;
  }
  return brushFaceHandlesFromParamsOrSelection(map, params, error);
}

QJsonArray changedBrushIds(
  const std::vector<mdl::BrushFaceHandle>& handles, const mdl::WorldNode& worldNode)
{
  auto result = QJsonArray{};
  for (const auto* node : mdl::toNodes(handles))
  {
    result.push_back(nodePathId(*node, worldNode));
  }
  return result;
}

McpBridgeToolResult textureApplyResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  const auto material = params.value("material").toString().trimmed();
  if (material.isEmpty())
  {
    return invalidParamsFailure("texture_apply requires material");
  }

  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto& map = mapWindow->document().map();
  auto error = QString{};
  auto handles = brushFaceHandlesFromParamsOrSelection(map, params, error);
  if (handles.empty())
  {
    return invalidParamsFailure(error);
  }

  auto changedNodes = changedBrushIds(handles, map.worldNode());

  const auto transactionName = QString{"MCP: Apply texture"};
  auto ok = executeTransaction(map, transactionName, [&]() {
    mdl::deselectAll(map);
    mdl::selectBrushFaces(map, handles);
    return mdl::setBrushFaceAttributes(
      map, mdl::UpdateBrushFaceAttributes{.materialName = material.toStdString()});
  });
  if (!ok)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not apply texture");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history, nextOperationIndex, toolName, transactionName, changedNodes, result);
  result.insert("material", material);
  result.insert("faceCount", static_cast<int>(handles.size()));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult faceListResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto& map = mapWindow->document().map();
  auto handles = std::vector<mdl::BrushFaceHandle>{};
  const auto objectId = params.value("objectId").toString().trimmed();
  if (!objectId.isEmpty())
  {
    auto* node = resolveNodeId(map.worldNode(), objectId);
    auto* brushNode = dynamic_cast<mdl::BrushNode*>(node);
    if (!brushNode)
    {
      return invalidParamsFailure(QString{"objectId is not a brush: %1"}.arg(objectId));
    }
    handles = mdl::toHandles(brushNode);
  }
  else if (!map.selection().brushFaces.empty())
  {
    handles = map.selection().brushFaces;
  }
  else
  {
    for (auto* brushNode : map.selection().brushes)
    {
      const auto brushHandles = mdl::toHandles(brushNode);
      handles.insert(std::end(handles), std::begin(brushHandles), std::end(brushHandles));
    }
  }

  const auto limit = optionalSize(params, "limit", 500);
  auto faces = QJsonArray{};
  for (const auto& handle : handles)
  {
    if (faces.size() >= static_cast<int>(limit))
    {
      break;
    }
    faces.push_back(brushFaceJson(handle, map.worldNode()));
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"faces", faces},
    {"count", faces.size()},
  });
}

McpBridgeToolResult faceSelectResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto& map = mapWindow->document().map();
  auto error = QString{};
  auto handles = std::vector<mdl::BrushFaceHandle>{};
  if (params.value("faces").isArray())
  {
    handles = brushFaceHandlesFromFacesArray(map, params.value("faces").toArray(), error);
  }
  else
  {
    const auto handle =
      brushFaceHandleFromJson(map, params, "objectId", "faceIndex", error);
    if (handle)
    {
      handles.push_back(*handle);
    }
  }

  if (handles.empty())
  {
    return invalidParamsFailure(
      error.isEmpty() ? "face_select requires faces or objectId/faceIndex" : error);
  }

  mdl::deselectAll(map);
  mdl::selectBrushFaces(map, handles);

  auto faces = QJsonArray{};
  for (const auto& handle : handles)
  {
    faces.push_back(brushFaceJson(handle, map.worldNode()));
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"faces", faces},
    {"selectedCount", faces.size()},
  });
}

std::optional<mdl::UpdateBrushFaceAttributes> updateBrushFaceAttributesFromParams(
  const QJsonObject& params, QString& error)
{
  auto update = mdl::UpdateBrushFaceAttributes{};
  auto hasUpdate = false;

  const auto material = params.value("material").toString().trimmed();
  if (!material.isEmpty())
  {
    update.materialName = material.toStdString();
    hasUpdate = true;
  }

  const auto setFloat = [&](const QString& key, auto& target) {
    const auto value = params.value(key);
    if (!value.isUndefined())
    {
      if (!value.isDouble())
      {
        error = QString{"%1 must be a number"}.arg(key);
        return false;
      }
      target = mdl::SetValue{static_cast<float>(value.toDouble())};
      hasUpdate = true;
    }
    return true;
  };

  if (
    !setFloat("xOffset", update.xOffset) || !setFloat("yOffset", update.yOffset)
    || !setFloat("xScale", update.xScale) || !setFloat("yScale", update.yScale)
    || !setFloat("rotation", update.rotation))
  {
    return std::nullopt;
  }

  if (!hasUpdate)
  {
    error = "No face texture attributes were provided";
    return std::nullopt;
  }
  return update;
}

McpBridgeToolResult faceTextureSetResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto& map = mapWindow->document().map();
  auto error = QString{};
  const auto update = updateBrushFaceAttributesFromParams(params, error);
  if (!update)
  {
    return invalidParamsFailure(error);
  }

  auto handles = faceSelectionHandlesFromParams(map, params, error);
  if (handles.empty())
  {
    return invalidParamsFailure(error);
  }

  const auto changedNodes = changedBrushIds(handles, map.worldNode());
  const auto transactionName = QString{"MCP: Set face texture"};
  const auto ok = executeTransaction(map, transactionName, [&]() {
    mdl::deselectAll(map);
    mdl::selectBrushFaces(map, handles);
    return mdl::setBrushFaceAttributes(map, *update);
  });
  if (!ok)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not set face texture attributes");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history, nextOperationIndex, toolName, transactionName, changedNodes, result);
  result.insert("faceCount", static_cast<int>(handles.size()));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult textureReplaceResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  const auto find = params.value("find").toString().trimmed();
  const auto replace = params.value("replace").toString().trimmed();
  if (find.isEmpty() || replace.isEmpty())
  {
    return invalidParamsFailure("texture_replace requires find and replace");
  }

  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto& map = mapWindow->document().map();
  const auto scope = params.value("scope").toString("selection").trimmed().toLower();
  auto handles = scope == "map" ? mdl::collectBrushFaces({&map.worldNode()})
                                : map.selection().allBrushFaces();
  if (scope != "map" && scope != "selection")
  {
    return invalidParamsFailure("scope must be selection or map");
  }
  if (handles.empty())
  {
    return invalidParamsFailure("texture_replace found no candidate faces");
  }

  const auto findMaterial = find.toStdString();
  handles.erase(
    std::remove_if(
      handles.begin(),
      handles.end(),
      [&](const auto& handle) {
        return !kdl::ci::str_is_equal(
          handle.face().attributes().materialName(), findMaterial);
      }),
    handles.end());
  if (handles.empty())
  {
    return invalidParamsFailure("No faces use the requested material");
  }

  const auto changedNodes = changedBrushIds(handles, map.worldNode());
  const auto transactionName = QString{"MCP: Replace texture"};
  const auto ok = executeTransaction(map, transactionName, [&]() {
    mdl::deselectAll(map);
    mdl::selectBrushFaces(map, handles);
    return mdl::setBrushFaceAttributes(
      map, mdl::UpdateBrushFaceAttributes{.materialName = replace.toStdString()});
  });
  if (!ok)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not replace texture");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history, nextOperationIndex, toolName, transactionName, changedNodes, result);
  result.insert("find", find);
  result.insert("replace", replace);
  result.insert("faceCount", static_cast<int>(handles.size()));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult textureAlignFaceResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  const auto mode = params.value("mode").toString().trimmed().toLower();
  auto update = mdl::UpdateBrushFaceAttributes{};
  if (mode == "reset")
  {
    update.axis = mdl::ResetAxis{};
  }
  else if (mode == "paraxial" || mode == "world")
  {
    update.axis = mdl::ToParaxial{};
  }
  else if (mode == "parallel" || mode == "face")
  {
    update.axis = mdl::ToParallel{};
  }
  else
  {
    return invalidParamsFailure(
      "texture_align_face mode must be reset, paraxial, world, parallel, or face");
  }

  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto& map = mapWindow->document().map();
  auto error = QString{};
  auto handles = brushFaceHandlesFromParamsOrSelection(map, params, error);
  if (handles.empty())
  {
    return invalidParamsFailure(error);
  }

  const auto changedNodes = changedBrushIds(handles, map.worldNode());
  const auto transactionName = QString{"MCP: Align face texture"};
  const auto ok = executeTransaction(map, transactionName, [&]() {
    mdl::deselectAll(map);
    mdl::selectBrushFaces(map, handles);
    return mdl::setBrushFaceAttributes(map, update);
  });
  if (!ok)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not align face texture");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history, nextOperationIndex, toolName, transactionName, changedNodes, result);
  result.insert("mode", mode);
  result.insert("faceCount", static_cast<int>(handles.size()));
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult textureCopyFromFaceResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto& map = mapWindow->document().map();
  auto error = QString{};
  const auto source =
    brushFaceHandleFromJson(map, params, "sourceObjectId", "sourceFaceIndex", error);
  if (!source)
  {
    return invalidParamsFailure(error);
  }

  auto targetParams = params;
  targetParams.remove("sourceObjectId");
  targetParams.remove("sourceFaceIndex");
  auto handles = brushFaceHandlesFromParamsOrSelection(map, targetParams, error);
  if (handles.empty())
  {
    return invalidParamsFailure(error);
  }

  const auto sourceAttributes = source->face().attributes();
  const auto changedNodes = changedBrushIds(handles, map.worldNode());
  const auto transactionName = QString{"MCP: Copy face texture"};
  const auto ok = executeTransaction(map, transactionName, [&]() {
    mdl::deselectAll(map);
    mdl::selectBrushFaces(map, handles);
    return mdl::setBrushFaceAttributes(map, mdl::copyAll(sourceAttributes));
  });
  if (!ok)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not copy face texture");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history, nextOperationIndex, toolName, transactionName, changedNodes, result);
  result.insert("faceCount", static_cast<int>(handles.size()));
  result.insert("source", brushFaceJson(*source, map.worldNode()));
  return McpBridgeToolResult::success(std::move(result));
}
} // namespace tb::ui
