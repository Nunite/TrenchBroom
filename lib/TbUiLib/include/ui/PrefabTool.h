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

#include "ui/Tool.h"

#include "vm/bbox.h"
#include "vm/vec.h"

#include <filesystem>
#include <functional>
#include <optional>

namespace tb::mdl
{
class Map;
}

namespace tb::ui
{
class InputState;
class MapDocument;

class PrefabTool : public Tool
{
private:
  MapDocument& m_document;
  std::optional<vm::bbox3d> m_previewBounds;

public:
  using PlacementDelta = std::function<vm::vec3d(
    mdl::Map&, const InputState&, const vm::bbox3d&, const vm::bbox3d&)>;

  explicit PrefabTool(MapDocument& document);

  bool canPlacePrefab(const std::filesystem::path& path) const;
  const std::optional<vm::bbox3d>& previewBounds() const;
  bool updatePreview(
    const std::filesystem::path& path,
    const InputState& inputState,
    const PlacementDelta& placementDelta);
  void clearPreview();
  bool placePrefab(
    const std::filesystem::path& path,
    const InputState& inputState,
    const PlacementDelta& placementDelta);
};

} // namespace tb::ui
