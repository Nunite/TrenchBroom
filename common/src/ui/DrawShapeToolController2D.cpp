/*
 Copyright (C) 2010 Kristian Duske

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

#include "DrawShapeToolController2D.h"

#include "render/Camera.h"
#include "ui/DrawShapeTool.h"
#include "ui/Grid.h"
#include "ui/HandleDragTracker.h"
#include "ui/InputState.h"
#include "ui/MapDocument.h"
#include "ui/Transaction.h"
#include "ui/BoxSelectionTool.h"
#include "mdl/ModelUtils.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushNode.h"
#include "mdl/WorldNode.h"
#include "mdl/LayerNode.h"
#include "mdl/GroupNode.h"
#include "mdl/EntityNode.h"
#include "mdl/PatchNode.h"
#include "render/RenderService.h"
#include "render/RenderContext.h"
#include "render/RenderBatch.h"
#include "Color.h"

#include "kdl/memory_utils.h"
#include "kdl/result.h"
#include "kdl/result_fold.h"
#include "kdl/overload.h"

#include "vm/intersection.h"

namespace tb::ui
{
namespace
{

class DrawShapeDragDelegate : public HandleDragTrackerDelegate
{
private:
  DrawShapeTool& m_tool;
  vm::bbox3d m_worldBounds;
  vm::bbox3d m_referenceBounds;

public:
  DrawShapeDragDelegate(
    DrawShapeTool& tool, const vm::bbox3d& worldBounds, const vm::bbox3d& referenceBounds)
    : m_tool{tool}
    , m_worldBounds{worldBounds}
    , m_referenceBounds{referenceBounds}
  {
  }

  HandlePositionProposer start(
    const InputState& inputState,
    const vm::vec3d& initialHandlePosition,
    const vm::vec3d& handleOffset) override
  {
    const auto currentBounds =
      makeBounds(inputState, initialHandlePosition, initialHandlePosition);

    m_tool.update(currentBounds);
    m_tool.refreshViews();

    const auto& camera = inputState.camera();
    const auto plane = vm::plane3d{
      initialHandlePosition,
      vm::vec3d{vm::get_abs_max_component_axis(camera.direction())}};

    return makeHandlePositionProposer(
      makePlaneHandlePicker(plane, handleOffset), makeIdentityHandleSnapper());
  }

  DragStatus update(
    const InputState& inputState,
    const DragState& dragState,
    const vm::vec3d& proposedHandlePosition) override
  {
    if (updateBounds(
          inputState,
          dragState.initialHandlePosition,
          dragState.currentHandlePosition,
          proposedHandlePosition))
    {
      m_tool.refreshViews();
      return DragStatus::Continue;
    }
    return DragStatus::Deny;
  }

  void end(const InputState&, const DragState&) override { m_tool.createBrushes(); }

  void cancel(const DragState&) override { m_tool.cancel(); }

  std::optional<UpdateDragConfig> modifierKeyChange(
    const InputState& inputState, const DragState& dragState) override
  {
    const auto currentBounds = makeBounds(
      inputState, dragState.initialHandlePosition, dragState.currentHandlePosition);

    if (!currentBounds.is_empty())
    {
      m_tool.update(currentBounds);
      m_tool.refreshViews();
    }

    return std::nullopt;
  }

  void render(
    const InputState&,
    const DragState&,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch) const override
  {
    m_tool.render(renderContext, renderBatch);
  }

private:
  bool updateBounds(
    const InputState& inputState,
    const vm::vec3d& initialHandlePosition,
    const vm::vec3d& lastHandlePosition,
    const vm::vec3d& currentHandlePosition)
  {
    const auto lastBounds =
      makeBounds(inputState, initialHandlePosition, lastHandlePosition);
    const auto currentBounds =
      makeBounds(inputState, initialHandlePosition, currentHandlePosition);

    if (currentBounds.is_empty() || currentBounds == lastBounds)
    {
      return false;
    }

    m_tool.update(currentBounds);
    return true;
  }

  vm::bbox3d makeBounds(
    const InputState& inputState,
    const vm::vec3d& initialHandlePosition,
    const vm::vec3d& currentHandlePosition) const
  {
    auto bounds = snapBounds(
      inputState,
      vm::merge(
        vm::bbox3d{initialHandlePosition, initialHandlePosition}, currentHandlePosition));

    if (inputState.modifierKeysDown(ModifierKeys::Shift))
    {
      const auto viewAxis = vm::abs(vm::vec3d{inputState.camera().direction()});
      const auto orthoAxes = vm::vec3d{1, 1, 1} - viewAxis;

      // The max length of the bounds along any of the ortho axes:
      const auto maxLength = vm::get_abs_max_component(bounds.size() * orthoAxes);

      if (inputState.modifierKeysDown(ModifierKeys::Alt))
      {
        const auto lengthDiff = vm::vec3d{maxLength, maxLength, maxLength};

        // The direction in which the user is dragging per component:
        const auto dragDir = vm::step(initialHandlePosition, currentHandlePosition);
        bounds = vm::bbox3d{
          vm::mix(bounds.min, bounds.max - lengthDiff, vm::vec3d{1, 1, 1} - dragDir),
          vm::mix(bounds.max, bounds.min + lengthDiff, dragDir)};
      }
      else
      {
        // A vector where the ortho axes have maxLength and the view axis has the size of
        // the bounds in that direction
        const auto lengthDiff = viewAxis * bounds.size() + orthoAxes * maxLength;

        // The direction in which the user is dragging per component:
        const auto dragDir = vm::step(initialHandlePosition, currentHandlePosition);
        bounds = vm::bbox3d{
          vm::mix(bounds.min, bounds.max - lengthDiff, vm::vec3d{1, 1, 1} - dragDir),
          vm::mix(bounds.max, bounds.min + lengthDiff, dragDir)};
      }
    }

    return vm::intersect(bounds, m_worldBounds);
  }

  vm::bbox3d snapBounds(const InputState& inputState, const vm::bbox3d& bounds) const
  {
    const auto& grid = m_tool.grid();
    const auto min = grid.snapDown(bounds.min);
    const auto max = grid.snapUp(bounds.max);

    const auto& camera = inputState.camera();
    const auto& refBounds = m_referenceBounds;
    const auto factors =
      vm::vec3d{vm::abs(vm::get_abs_max_component_axis(camera.direction()))};
    return vm::bbox3d{
      vm::mix(min, refBounds.min, factors), vm::mix(max, refBounds.max, factors)};
  }
};

} // namespace

DrawShapeToolController2D::DrawShapeToolController2D(
  DrawShapeTool& tool, std::weak_ptr<MapDocument> document)
  : m_tool{tool}
  , m_document{std::move(document)}
{
}

Tool& DrawShapeToolController2D::tool()
{
  return m_tool;
}

const Tool& DrawShapeToolController2D::tool() const
{
  return m_tool;
}

std::unique_ptr<GestureTracker> DrawShapeToolController2D::acceptMouseDrag(
  const InputState& inputState)
{
  // 检查是否按下Alt键，如果是则可能进入框选模式
  const auto modKeys = inputState.modifierKeys();
  const bool altDown = (modKeys & ModifierKeys::Alt) != 0;
  
  // 明确区分左键和右键
  const bool leftMouseDown = inputState.mouseButtonsDown(MouseButtons::Left);
  const bool rightMouseDown = inputState.mouseButtonsDown(MouseButtons::Right);
  
  // 右键按下时不进行任何操作，让系统显示右键菜单
  if (rightMouseDown) {
    return nullptr;
  }
  
  // 只有按下左键+Alt组合才进入框选模式
  if (leftMouseDown && altDown)
  {
    return handleBoxSelection(inputState);
  }

  // 如果只按下左键，进入原有的创建brush逻辑
  if (leftMouseDown) {
    if (!inputState.checkModifierKeys(
          ModifierKeyPressed::No,
          ModifierKeyPressed::DontCare,
          ModifierKeyPressed::DontCare))
    {
      return nullptr;
    }

    auto document = kdl::mem_lock(m_document);
    if (document->hasSelection())
    {
      return nullptr;
    }

    const auto& bounds = document->referenceBounds();
    const auto& camera = inputState.camera();
    const auto plane = vm::plane3d{
      bounds.min, vm::vec3d{vm::get_abs_max_component_axis(camera.direction())}};

    if (const auto distance = vm::intersect_ray_plane(inputState.pickRay(), plane))
    {
      const auto initialHandlePosition =
        vm::point_at_distance(inputState.pickRay(), *distance);
      return createHandleDragTracker(
        DrawShapeDragDelegate{m_tool, document->worldBounds(), document->referenceBounds()},
        inputState,
        initialHandlePosition,
        initialHandlePosition);
    }
  }

  return nullptr;
}

std::unique_ptr<GestureTracker> DrawShapeToolController2D::handleBoxSelection(
  const InputState& inputState)
{
  // 如果是右键被按下，不执行框选操作，允许右键菜单显示
  if (inputState.mouseButtonsDown(MouseButtons::Right)) {
    return nullptr;
  }
  
  auto document = kdl::mem_lock(m_document);
  
  // 使用位于BoxSelectionTool.cpp中实现的框选工具
  const auto& bounds = document->referenceBounds();
  const auto& camera = inputState.camera();
  const auto plane = vm::plane3d{
    bounds.min, vm::vec3d{vm::get_abs_max_component_axis(camera.direction())}};

  if (const auto distance = vm::intersect_ray_plane(inputState.pickRay(), plane))
  {
    const auto initialHandlePosition =
      vm::point_at_distance(inputState.pickRay(), *distance);
    
    return createHandleDragTracker(
      BoxSelectionDragDelegate{m_tool, m_document},
      inputState,
      initialHandlePosition,
      initialHandlePosition);
  }

  return nullptr;
}

bool DrawShapeToolController2D::cancel()
{
  return m_tool.cancel();
}

bool DrawShapeToolController2D::mouseClick(const InputState& inputState)
{
  // 如果是右键点击，返回false让事件继续传递到上层
  if (inputState.mouseButtonsDown(MouseButtons::Right)) {
    return false;
  }
  
  // 使用基类默认实现
  return ToolController::mouseClick(inputState);
}

} // namespace tb::ui
