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

#include "ui/PrefabToolController.h"

#include "PreferenceManager.h"
#include "Preferences.h"
#include "gl/Camera.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Grid.h"
#include "mdl/HitAdapter.h"
#include "mdl/HitFilter.h"
#include "mdl/Map.h"
#include "mdl/PickResult.h"
#include "render/RenderService.h"
#include "ui/DropTracker.h"
#include "ui/InputState.h"
#include "ui/PrefabTool.h"

#include "vm/plane.h"
#include "vm/vec.h"

#include <optional>

namespace tb::ui
{
namespace
{

struct DropPayload
{
  std::string type;
  std::string value;
};

std::optional<DropPayload> parseDropPayload(const std::string& payload)
{
  const auto separatorPos = payload.find(':');
  if (separatorPos == std::string::npos)
  {
    return std::nullopt;
  }
  return DropPayload{payload.substr(0, separatorPos), payload.substr(separatorPos + 1)};
}

class PrefabDropTracker : public DropTracker
{
private:
  PrefabTool& m_tool;
  std::filesystem::path m_prefabPath;
  PrefabTool::PlacementDelta m_placementDelta;

public:
  PrefabDropTracker(
    PrefabTool& tool,
    std::filesystem::path prefabPath,
    PrefabTool::PlacementDelta placementDelta)
    : m_tool{tool}
    , m_prefabPath{std::move(prefabPath)}
    , m_placementDelta{std::move(placementDelta)}
  {
  }

  bool move(const InputState& inputState) override
  {
    m_tool.updatePreview(m_prefabPath, inputState, m_placementDelta);
    m_tool.refreshViews();
    return true;
  }

  bool drop(const InputState& inputState) override
  {
    const auto placed = m_tool.placePrefab(m_prefabPath, inputState, m_placementDelta);
    m_tool.clearPreview();
    m_tool.refreshViews();
    return placed;
  }

  void leave(const InputState&) override
  {
    m_tool.clearPreview();
    m_tool.refreshViews();
  }
};

vm::vec3d placementDelta2D(
  mdl::Map& map,
  const InputState& inputState,
  const vm::bbox3d& bounds,
  const vm::bbox3d& referenceBounds)
{
  const auto& pickRay = inputState.pickRay();
  const auto toMin = referenceBounds.min - pickRay.origin;
  const auto toMax = referenceBounds.max - pickRay.origin;
  const auto anchor =
    vm::dot(toMin, pickRay.direction) > vm::dot(toMax, pickRay.direction)
      ? referenceBounds.min
      : referenceBounds.max;
  const auto dragPlane = vm::plane3d{anchor, -pickRay.direction};

  return map.grid().moveDeltaForBounds(dragPlane, bounds, map.worldBounds(), pickRay);
}

vm::vec3d placementDelta3D(
  mdl::Map& map,
  const InputState& inputState,
  const vm::bbox3d& bounds,
  const vm::bbox3d&)
{
  using namespace mdl::HitFilters;

  const auto& grid = map.grid();
  const auto& pickRay = inputState.pickRay();
  const auto& hit = inputState.pickResult().first(type(mdl::BrushNode::BrushHitType));
  if (const auto faceHandle = mdl::hitToFaceHandle(hit))
  {
    return grid.moveDeltaForBounds(
      faceHandle->face().boundary(), bounds, map.worldBounds(), pickRay);
  }

  const auto point = grid.snap(inputState.defaultPointUnderMouse());
  const auto targetPlane =
    vm::plane3d{point, -vm::vec3d{inputState.camera().direction()}};
  return grid.moveDeltaForBounds(targetPlane, bounds, map.worldBounds(), pickRay);
}

} // namespace

PrefabToolController::PrefabToolController(PrefabTool& tool)
  : m_tool{tool}
{
}

PrefabToolController::~PrefabToolController() = default;

PrefabTool& PrefabToolController::prefabTool() const
{
  return m_tool;
}

Tool& PrefabToolController::tool()
{
  return m_tool;
}

const Tool& PrefabToolController::tool() const
{
  return m_tool;
}

bool PrefabToolController::shouldAcceptDrop(
  const InputState&, const std::string& payload) const
{
  const auto parsedPayload = parseDropPayload(payload);
  return parsedPayload && parsedPayload->type == "prefab"
         && m_tool.canPlacePrefab(parsedPayload->value);
}

std::unique_ptr<DropTracker> PrefabToolController::acceptDrop(
  const InputState&, const std::string& payload)
{
  const auto parsedPayload = parseDropPayload(payload);
  if (!parsedPayload || parsedPayload->type != "prefab")
  {
    return nullptr;
  }

  return createDropTracker(std::filesystem::path{parsedPayload->value});
}

void PrefabToolController::render(
  const InputState&,
  render::RenderContext& renderContext,
  render::RenderBatch& renderBatch)
{
  if (const auto& bounds = m_tool.previewBounds())
  {
    auto renderService = render::RenderService{renderContext, renderBatch};
    renderService.setForegroundColor(pref(Preferences::SelectionBoundsColor));
    renderService.setLineWidth(2.0f);
    renderService.setShowOccludedObjectsTransparent();
    renderService.renderBounds(vm::bbox3f{*bounds});
  }
}

PrefabToolController2D::PrefabToolController2D(PrefabTool& tool)
  : PrefabToolController{tool}
{
}

std::unique_ptr<DropTracker> PrefabToolController2D::createDropTracker(
  std::filesystem::path prefabPath) const
{
  return std::make_unique<PrefabDropTracker>(
    prefabTool(),
    std::move(prefabPath),
    [](
      auto& map,
      const auto& inputState,
      const auto& bounds,
      const auto& referenceBounds) {
      return placementDelta2D(map, inputState, bounds, referenceBounds);
    });
}

PrefabToolController3D::PrefabToolController3D(PrefabTool& tool)
  : PrefabToolController{tool}
{
}

std::unique_ptr<DropTracker> PrefabToolController3D::createDropTracker(
  std::filesystem::path prefabPath) const
{
  return std::make_unique<PrefabDropTracker>(
    prefabTool(),
    std::move(prefabPath),
    [](
      auto& map,
      const auto& inputState,
      const auto& bounds,
      const auto& referenceBounds) {
      return placementDelta3D(map, inputState, bounds, referenceBounds);
    });
}

} // namespace tb::ui
