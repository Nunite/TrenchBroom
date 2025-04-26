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

// 自定义渲染类用于显示框选框
class SelectionBoxRenderer
{
private:
  vm::bbox3d m_selectionBounds;
  bool m_valid;
  
public:
  SelectionBoxRenderer() : m_valid(false) {}
  
  void setSelectionBounds(const vm::bbox3d& bounds) {
    m_selectionBounds = bounds;
    m_valid = true;
  }
  
  void clear() {
    m_valid = false;
  }
  
  void render(render::RenderContext& renderContext, render::RenderBatch& renderBatch) const {
    if (!m_valid) {
      return;
    }
    
    auto renderService = render::RenderService{renderContext, renderBatch};
    
    // 使用蓝色半透明线显示选择框
    const auto color = Color(0.2f, 0.4f, 1.0f, 0.6f);
    renderService.setForegroundColor(color);
    renderService.setLineWidth(2.0f); // 设置线宽
    
    // 获取相机方向以适应不同视图
    const auto& camera = renderContext.camera();
    const auto direction = camera.direction();
    
    // 绘制框的边界线
    const auto min = m_selectionBounds.min;
    const auto max = m_selectionBounds.max;
    
    // 确定主投影平面，根据相机方向决定
    vm::axis::type majorAxis = vm::find_abs_max_component(direction);
    
    if (majorAxis == vm::axis::z) {
      // 顶视图或底视图 - XY平面
      renderService.renderLine(
        vm::vec3f(min.x(), min.y(), min.z()),
        vm::vec3f(max.x(), min.y(), min.z()));
      renderService.renderLine(
        vm::vec3f(max.x(), min.y(), min.z()),
        vm::vec3f(max.x(), max.y(), min.z()));
      renderService.renderLine(
        vm::vec3f(max.x(), max.y(), min.z()),
        vm::vec3f(min.x(), max.y(), min.z()));
      renderService.renderLine(
        vm::vec3f(min.x(), max.y(), min.z()),
        vm::vec3f(min.x(), min.y(), min.z()));
    } else if (majorAxis == vm::axis::y) {
      // 前视图或后视图 - XZ平面
      renderService.renderLine(
        vm::vec3f(min.x(), min.y(), min.z()),
        vm::vec3f(max.x(), min.y(), min.z()));
      renderService.renderLine(
        vm::vec3f(max.x(), min.y(), min.z()),
        vm::vec3f(max.x(), min.y(), max.z()));
      renderService.renderLine(
        vm::vec3f(max.x(), min.y(), max.z()),
        vm::vec3f(min.x(), min.y(), max.z()));
      renderService.renderLine(
        vm::vec3f(min.x(), min.y(), max.z()),
        vm::vec3f(min.x(), min.y(), min.z()));
    } else {
      // 侧视图 - YZ平面
      renderService.renderLine(
        vm::vec3f(min.x(), min.y(), min.z()),
        vm::vec3f(min.x(), max.y(), min.z()));
      renderService.renderLine(
        vm::vec3f(min.x(), max.y(), min.z()),
        vm::vec3f(min.x(), max.y(), max.z()));
      renderService.renderLine(
        vm::vec3f(min.x(), max.y(), max.z()),
        vm::vec3f(min.x(), min.y(), max.z()));
      renderService.renderLine(
        vm::vec3f(min.x(), min.y(), max.z()),
        vm::vec3f(min.x(), min.y(), min.z()));
    }
  }
};

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

// 框选工具代理
class BoxSelectionDragDelegate : public HandleDragTrackerDelegate
{
private:
  DrawShapeTool& m_tool;
  std::weak_ptr<MapDocument> m_document;
  vm::bbox3d m_selectionBounds;
  SelectionBoxRenderer m_renderer;

public:
  BoxSelectionDragDelegate(DrawShapeTool& tool, std::weak_ptr<MapDocument> document)
    : m_tool{tool}
    , m_document{std::move(document)}
  {
  }

  HandlePositionProposer start(
    const InputState& inputState,
    const vm::vec3d& initialHandlePosition,
    const vm::vec3d& handleOffset) override
  {
    // 记录选择框的起始位置
    m_selectionBounds = vm::bbox3d{initialHandlePosition, initialHandlePosition};
    
    // 更新渲染器
    m_renderer.setSelectionBounds(m_selectionBounds);
    
    // 不再使用工具的update方法，避免brush创建错误
    // 只刷新视图
    m_tool.refreshViews();

    const auto& camera = inputState.camera();
    const auto plane = vm::plane3d{
      initialHandlePosition,
      vm::vec3d{vm::get_abs_max_component_axis(camera.direction())}};

    return makeHandlePositionProposer(
      makePlaneHandlePicker(plane, handleOffset), makeIdentityHandleSnapper());
  }

  DragStatus update(
    const InputState&,
    const DragState& dragState,
    const vm::vec3d& proposedHandlePosition) override
  {
    // 确保生成有效的框选范围
    // 我们需要确保min和max正确设置，不会产生零尺寸或负尺寸的box
    auto min = vm::vec3d{
      std::min(dragState.initialHandlePosition.x(), proposedHandlePosition.x()),
      std::min(dragState.initialHandlePosition.y(), proposedHandlePosition.y()),
      std::min(dragState.initialHandlePosition.z(), proposedHandlePosition.z())
    };
    
    auto max = vm::vec3d{
      std::max(dragState.initialHandlePosition.x(), proposedHandlePosition.x()),
      std::max(dragState.initialHandlePosition.y(), proposedHandlePosition.y()),
      std::max(dragState.initialHandlePosition.z(), proposedHandlePosition.z())
    };
    
    // 确保box至少有最小尺寸，以防止空brush错误
    const double minSize = 0.1; // 最小尺寸（根据实际需要调整）
    
    // 正确修改vec3d的各个分量 - 创建新的向量而不是尝试修改现有向量
    if (max.x() - min.x() < minSize) {
      max = vm::vec3d(min.x() + minSize, max.y(), max.z());
    }
    if (max.y() - min.y() < minSize) {
      max = vm::vec3d(max.x(), min.y() + minSize, max.z());
    }
    if (max.z() - min.z() < minSize) {
      max = vm::vec3d(max.x(), max.y(), min.z() + minSize);
    }
    
    // 更新选择框大小
    m_selectionBounds = vm::bbox3d{min, max};
    
    // 更新渲染器
    m_renderer.setSelectionBounds(m_selectionBounds);
    
    // 不再通过工具创建brush，只刷新视图
    m_tool.refreshViews();
    return DragStatus::Continue;
  }

  void end(const InputState& inputState, const DragState&) override 
  { 
    // 框选结束时，执行实际选择操作
    auto document = kdl::mem_lock(m_document);
    if (document)
    {
      try {
        // 验证选择框是否有效
        if (m_selectionBounds.is_empty()) {
          return;
        }
          
        // 获取相机主方向以确定投影平面
        const auto& camera = inputState.camera();
        const auto direction = camera.direction();
        vm::axis::type majorAxis = vm::find_abs_max_component(direction);
        
        // 获取当前world中的所有节点
        auto allNodes = std::vector<mdl::Node*>{};
        document->world()->accept(kdl::overload(
          [&](auto&& thisLambda, mdl::WorldNode* world) { 
            world->visitChildren(thisLambda);
          },
          [&](auto&& thisLambda, mdl::LayerNode* layer) {
            layer->visitChildren(thisLambda);
          },
          [&](auto&& thisLambda, mdl::GroupNode* group) {
            allNodes.push_back(group);
            group->visitChildren(thisLambda);
          },
          [&](auto&& thisLambda, mdl::EntityNode* entity) {
            allNodes.push_back(entity);
            entity->visitChildren(thisLambda);
          },
          [&](mdl::BrushNode* brush) {
            allNodes.push_back(brush);
          },
          [&](mdl::PatchNode* patch) {
            allNodes.push_back(patch);
          }));
          
        // 过滤出选择框中包含的节点 - 基于视图投影
        auto selectedNodes = std::vector<mdl::Node*>{};
        for (auto* node : allNodes) {
          const auto& nodeBounds = node->logicalBounds();
          const auto nodeCenter = nodeBounds.center();
          
          bool nodeSelected = false;
          
          // 根据当前视图投影判断节点是否在选择范围内
          if (majorAxis == vm::axis::z) {
            // 顶视图 - 投影到XY平面
            nodeSelected = nodeCenter.x() >= m_selectionBounds.min.x() &&
                          nodeCenter.x() <= m_selectionBounds.max.x() &&
                          nodeCenter.y() >= m_selectionBounds.min.y() &&
                          nodeCenter.y() <= m_selectionBounds.max.y();
          } else if (majorAxis == vm::axis::y) {
            // 前视图 - 投影到XZ平面
            nodeSelected = nodeCenter.x() >= m_selectionBounds.min.x() &&
                          nodeCenter.x() <= m_selectionBounds.max.x() &&
                          nodeCenter.z() >= m_selectionBounds.min.z() &&
                          nodeCenter.z() <= m_selectionBounds.max.z();
          } else {
            // 侧视图 - 投影到YZ平面
            nodeSelected = nodeCenter.y() >= m_selectionBounds.min.y() &&
                          nodeCenter.y() <= m_selectionBounds.max.y() &&
                          nodeCenter.z() >= m_selectionBounds.min.z() &&
                          nodeCenter.z() <= m_selectionBounds.max.z();
          }
          
          if (nodeSelected) {
            selectedNodes.push_back(node);
          }
        }
        
        // 过滤出可选择的节点
        auto selectableNodes = mdl::collectSelectableNodes(
          selectedNodes, document->editorContext());
          
        // 如果有可选节点，创建选择事务
        if (!selectableNodes.empty()) {
          auto transaction = Transaction{*document, "Box Select"};
          document->deselectAll();
          document->selectNodes(selectableNodes);
          transaction.commit();
        }
        
        // 清除渲染器
        m_renderer.clear();
      } catch (const std::exception& e) {
      } catch (...) {
      }
    }
  }

  void cancel(const DragState&) override 
  { 
    // 取消选择操作
    m_renderer.clear();
  }

  void render(
    const InputState&,
    const DragState&,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch) const override
  {
    // 使用自定义渲染器绘制选择框
    m_renderer.render(renderContext, renderBatch);
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
  if (!inputState.mouseButtonsPressed(MouseButtons::Left))
  {
    return nullptr;
  }

  // 检查是否按下Alt键，如果是则进入框选模式
  const auto modKeys = inputState.modifierKeys();
  const bool altDown = (modKeys & ModifierKeys::Alt) != 0;
  
  if (altDown)
  {
    return handleBoxSelection(inputState);
  }

  // 原有的创建brush逻辑保持不变
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

  return nullptr;
}

std::unique_ptr<GestureTracker> DrawShapeToolController2D::handleBoxSelection(
  const InputState& inputState)
{
  auto document = kdl::mem_lock(m_document);
  
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

} // namespace tb::ui
