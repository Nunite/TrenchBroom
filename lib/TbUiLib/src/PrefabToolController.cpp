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

#include "Color.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "gl/Camera.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Grid.h"
#include "mdl/HitAdapter.h"
#include "mdl/HitFilter.h"
#include "mdl/Map.h"
#include "mdl/Node.h"
#include "mdl/PickResult.h"
#include "render/BrushRenderer.h"
#include "render/ObjectRenderer.h"
#include "ui/DropTracker.h"
#include "ui/InputState.h"
#include "ui/PrefabTool.h"

#include "vm/intersection.h"
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

void addNodeRecursive(render::ObjectRenderer& renderer, mdl::Node& node)
{
  renderer.addNode(node);
  for (auto* child : node.children())
  {
    addNodeRecursive(renderer, *child);
  }
}

void setupPrefabPreviewRenderer(render::ObjectRenderer& renderer)
{
  renderer.setShowOverlays(false);
  renderer.setShowBrushEdges(true);
  renderer.setShowOccludedObjects(true);
  renderer.setOccludedEdgeColor(RgbaF{
    pref(Preferences::SelectedEdgeColor).to<RgbF>(),
    pref(Preferences::OccludedSelectedEdgeAlpha)});
  renderer.setTint(true);
  renderer.setTintColor(pref(Preferences::SelectedFaceColor));

  renderer.setOverrideGroupColors(true);
  renderer.setGroupBoundsColor(pref(Preferences::SelectedEdgeColor));

  renderer.setOverrideEntityBoundsColor(true);
  renderer.setEntityBoundsColor(pref(Preferences::SelectedEdgeColor));
  renderer.setShowEntityAngles(true);
  renderer.setEntityAngleColor(pref(Preferences::AngleIndicatorColor));

  renderer.setBrushFaceColor(pref(Preferences::FaceColor));
  renderer.setBrushEdgeColor(pref(Preferences::SelectedEdgeColor));
  renderer.setUseReadable2DBrushOutlines(false);
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

  const auto distance = vm::intersect_ray_plane(pickRay, dragPlane);
  if (!distance)
  {
    return vm::vec3d{};
  }

  const auto hitPoint = vm::point_at_distance(pickRay, *distance);
  const auto targetPoint = map.grid().snap(hitPoint, dragPlane);
  return prefabCenterPlacementDelta(bounds, targetPoint);
}

vm::vec3d placementDelta3D(
  mdl::Map& map,
  const InputState& inputState,
  const vm::bbox3d& bounds,
  const vm::bbox3d&)
{
  using namespace mdl::HitFilters;

  const auto& grid = map.grid();
  const auto& hit = inputState.pickResult().first(type(mdl::BrushNode::BrushHitType));
  if (const auto faceHandle = mdl::hitToFaceHandle(hit))
  {
    const auto targetPoint = grid.snap(hit.hitPoint(), faceHandle->face().boundary());
    return prefabCenterPlacementDelta(bounds, targetPoint);
  }

  const auto targetPoint = grid.snap(inputState.defaultPointUnderMouse());
  return prefabCenterPlacementDelta(bounds, targetPoint);
}

} // namespace

vm::vec3d prefabCenterPlacementDelta(
  const vm::bbox3d& bounds, const vm::vec3d& targetPoint)
{
  return targetPoint - bounds.center();
}

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

void PrefabToolController::validatePreviewRenderer() const
{
  if (m_previewRenderer && m_previewRendererVersion == m_tool.previewVersion())
  {
    return;
  }

  m_previewRenderer.reset();
  m_previewRendererVersion = m_tool.previewVersion();

  if (m_tool.previewNodes().empty())
  {
    return;
  }

  auto& map = m_tool.map();
  m_previewRenderer = std::make_unique<render::ObjectRenderer>(
    map.logger(),
    map.entityModelManager(),
    map.editorContext(),
    render::BrushRenderer::NoFilter{});
  setupPrefabPreviewRenderer(*m_previewRenderer);

  for (auto* node : m_tool.previewNodes())
  {
    addNodeRecursive(*m_previewRenderer, *node);
  }
}

void PrefabToolController::render(
  const InputState&,
  render::RenderContext& renderContext,
  render::RenderBatch& renderBatch)
{
  validatePreviewRenderer();
  if (m_previewRenderer)
  {
    m_previewRenderer->renderOpaque(renderContext, renderBatch);
    m_previewRenderer->renderTransparent(renderContext, renderBatch);
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
