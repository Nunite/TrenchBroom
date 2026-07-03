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

#include "ui/PrefabTool.h"

#include "Logger.h"
#include "ParserStatus.h"
#include "fs/DiskIO.h"
#include "fs/File.h"
#include "fs/Reader.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/GameFileSystem.h"
#include "mdl/Group.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_CopyPaste.h"
#include "mdl/Map_Geometry.h"
#include "mdl/Map_Selection.h"
#include "mdl/NodeReader.h"
#include "mdl/PasteType.h"
#include "mdl/PatchNode.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"
#include "ui/MapDocument.h"
#include "ui/PrefabAsset.h"

#include "kd/path_utils.h"
#include "kd/vector_utils.h"

#include "vm/mat_ext.h"

namespace tb::ui
{
namespace
{

class SilentParserStatus : public ParserStatus
{
private:
  NullLogger m_logger;

public:
  SilentParserStatus()
    : ParserStatus{m_logger, ""}
  {
  }

private:
  void doProgress(double) override {}
};

Result<std::string> readPrefabAssetText(
  const mdl::GameFileSystem& gameFileSystem, const std::filesystem::path& path)
{
  if (!isPrefabAssetPath(path))
  {
    return Error{"Not a prefab asset"};
  }

  if (path.is_absolute())
  {
    return ui::readPrefabAsset(path);
  }

  const auto prefix = std::filesystem::path{"prefabs"};
  if (kdl::path_has_prefix(path, prefix))
  {
    return ui::readPrefabAsset(
      configuredPrefabDirectory() / path.lexically_relative(prefix));
  }

  return gameFileSystem.openFile(path) | kdl::transform([](auto file) {
           auto reader = file->reader().buffer();
           return std::string{reader.stringView()};
         });
}

void addPrefabNodeBounds(vm::bbox3d::builder& builder, const mdl::Node& node)
{
  node.accept(kdl::overload(
    [&](const mdl::WorldNode& worldNode) {
      for (const auto* child : worldNode.children())
      {
        addPrefabNodeBounds(builder, *child);
      }
    },
    [&](const mdl::LayerNode& layerNode) {
      for (const auto* child : layerNode.children())
      {
        addPrefabNodeBounds(builder, *child);
      }
    },
    [&](const mdl::GroupNode& groupNode) { builder.add(groupNode.logicalBounds()); },
    [&](const mdl::EntityNode& entityNode) {
      if (mdl::isWorldspawn(entityNode.entity().classname()))
      {
        for (const auto* child : entityNode.children())
        {
          addPrefabNodeBounds(builder, *child);
        }
      }
      else
      {
        builder.add(entityNode.logicalBounds());
      }
    },
    [&](const mdl::BrushNode& brushNode) { builder.add(brushNode.logicalBounds()); },
    [&](const mdl::PatchNode& patchNode) { builder.add(patchNode.logicalBounds()); }));
}

vm::bbox3d computePrefabAssetBounds(const std::vector<mdl::Node*>& nodes)
{
  auto builder = vm::bbox3d::builder{};
  for (const auto* node : nodes)
  {
    addPrefabNodeBounds(builder, *node);
  }
  return builder.initialized() ? builder.bounds() : vm::bbox3d{};
}

Result<std::vector<mdl::Node*>> readPrefabAssetNodes(
  mdl::Map& map, const std::string& text)
{
  auto parserStatus = SilentParserStatus{};
  return mdl::NodeReader::read(
    text,
    map.worldNode().mapFormat(),
    map.worldBounds(),
    map.worldNode().entityPropertyConfig(),
    parserStatus,
    map.taskManager());
}

bool translatePrefabNode(mdl::Node& node, const vm::vec3d& delta, mdl::Map& map)
{
  const auto transformation = vm::translation_matrix(delta);
  const auto updateAngleProperty =
    map.worldNode().entityPropertyConfig().updateAnglePropertyAfterTransform;

  return node.accept(kdl::overload(
    [&](mdl::WorldNode& worldNode) {
      auto result = true;
      for (auto* child : worldNode.children())
      {
        result = translatePrefabNode(*child, delta, map) && result;
      }
      return result;
    },
    [&](mdl::LayerNode& layerNode) {
      auto result = true;
      for (auto* child : layerNode.children())
      {
        result = translatePrefabNode(*child, delta, map) && result;
      }
      return result;
    },
    [&](mdl::GroupNode& groupNode) {
      auto group = groupNode.group();
      group.transform(transformation);
      groupNode.setGroup(std::move(group));

      auto result = true;
      for (auto* child : groupNode.children())
      {
        result = translatePrefabNode(*child, delta, map) && result;
      }
      return result;
    },
    [&](mdl::EntityNode& entityNode) {
      auto entity = entityNode.entity();
      entity.transform(transformation, updateAngleProperty);
      entityNode.setEntity(std::move(entity));

      auto result = true;
      for (auto* child : entityNode.children())
      {
        result = translatePrefabNode(*child, delta, map) && result;
      }
      return result;
    },
    [&](mdl::BrushNode& brushNode) {
      auto brush = brushNode.brush();
      if (brush.transform(map.worldBounds(), transformation, false).is_error())
      {
        return false;
      }
      brushNode.setBrush(std::move(brush));
      return true;
    },
    [&](mdl::PatchNode& patchNode) {
      auto patch = patchNode.patch();
      patch.transform(transformation);
      patchNode.setPatch(std::move(patch));
      return true;
    }));
}

bool translatePrefabNodes(
  const std::vector<mdl::Node*>& nodes, const vm::vec3d& delta, mdl::Map& map)
{
  auto result = true;
  for (auto* node : nodes)
  {
    result = translatePrefabNode(*node, delta, map) && result;
  }
  return result;
}

} // namespace

PrefabTool::PrefabTool(MapDocument& document)
  : Tool{true}
  , m_document{document}
{
}

bool PrefabTool::canPlacePrefab(const std::filesystem::path& path) const
{
  return isPrefabAssetPath(path);
}

mdl::Map& PrefabTool::map() const
{
  return m_document.map();
}

const std::optional<vm::bbox3d>& PrefabTool::previewBounds() const
{
  return m_previewBounds;
}

const std::vector<mdl::Node*>& PrefabTool::previewNodes() const
{
  return m_previewNodes;
}

size_t PrefabTool::previewVersion() const
{
  return m_previewVersion;
}

bool PrefabTool::updatePreview(
  const std::filesystem::path& path,
  const InputState& inputState,
  const PlacementDelta& placementDelta)
{
  auto& map = m_document.map();
  const auto textResult = readPrefabAssetText(map.gameFileSystem(), path);
  if (textResult.is_error())
  {
    clearPreview();
    return false;
  }

  auto nodesResult = readPrefabAssetNodes(map, textResult.value());
  if (nodesResult.is_error())
  {
    clearPreview();
    return false;
  }

  auto nodes = std::move(nodesResult.value());
  const auto bounds = computePrefabAssetBounds(nodes);
  const auto delta = placementDelta(map, inputState, bounds, map.referenceBounds());
  if (!translatePrefabNodes(nodes, delta, map))
  {
    kdl::vec_clear_and_delete(nodes);
    clearPreview();
    return false;
  }

  kdl::vec_clear_and_delete(m_previewNodes);
  m_previewNodes = std::move(nodes);
  m_previewBounds = bounds.translate(delta);
  ++m_previewVersion;
  return true;
}

void PrefabTool::clearPreview()
{
  if (m_previewBounds || !m_previewNodes.empty())
  {
    kdl::vec_clear_and_delete(m_previewNodes);
    m_previewBounds = std::nullopt;
    ++m_previewVersion;
  }
}

bool PrefabTool::placePrefab(
  const std::filesystem::path& path,
  const InputState& inputState,
  const PlacementDelta& placementDelta)
{
  auto& map = m_document.map();
  const auto textResult = readPrefabAssetText(map.gameFileSystem(), path);
  if (textResult.is_error())
  {
    return false;
  }

  const auto referenceBounds = map.referenceBounds();
  auto transaction = mdl::Transaction{map, "Place Prefab"};

  if (mdl::paste(map, textResult.value()) != mdl::PasteType::Node)
  {
    transaction.cancel();
    return false;
  }

  if (const auto bounds = map.selectionBounds())
  {
    const auto delta = placementDelta(map, inputState, *bounds, referenceBounds);
    if (!mdl::translateSelection(map, delta))
    {
      transaction.cancel();
      return false;
    }
  }

  return transaction.commit();
}

} // namespace tb::ui
