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

#include "fs/DiskIO.h"
#include "fs/File.h"
#include "fs/Reader.h"
#include "mdl/GameFileSystem.h"
#include "mdl/Map.h"
#include "mdl/Map_CopyPaste.h"
#include "mdl/Map_Geometry.h"
#include "mdl/Map_Selection.h"
#include "mdl/PasteType.h"
#include "mdl/Transaction.h"
#include "ui/MapDocument.h"
#include "ui/PrefabAsset.h"

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

  return gameFileSystem.openFile(path) | kdl::transform([](auto file) {
           auto reader = file->reader().buffer();
           return std::string{reader.stringView()};
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
