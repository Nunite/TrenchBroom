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

#include "SimpleParserStatus.h"
#include "fs/DiskIO.h"
#include "fs/File.h"
#include "fs/Reader.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/GameFileSystem.h"
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

namespace tb::ui
{
namespace
{

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

Result<vm::bbox3d> readPrefabAssetBounds(mdl::Map& map, const std::string& text)
{
  auto parserStatus = SimpleParserStatus{map.logger()};
  return mdl::NodeReader::read(
           text,
           map.worldNode().mapFormat(),
           map.worldBounds(),
           map.worldNode().entityPropertyConfig(),
           parserStatus,
           map.taskManager())
         | kdl::transform([](auto nodes) {
             const auto bounds = computePrefabAssetBounds(nodes);
             kdl::vec_clear_and_delete(nodes);
             return bounds;
           });
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

const std::optional<vm::bbox3d>& PrefabTool::previewBounds() const
{
  return m_previewBounds;
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

  const auto boundsResult = readPrefabAssetBounds(map, textResult.value());
  if (boundsResult.is_error())
  {
    clearPreview();
    return false;
  }

  const auto& bounds = boundsResult.value();
  const auto delta = placementDelta(map, inputState, bounds, map.referenceBounds());
  m_previewBounds = bounds.translate(delta);
  return true;
}

void PrefabTool::clearPreview()
{
  m_previewBounds = std::nullopt;
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
