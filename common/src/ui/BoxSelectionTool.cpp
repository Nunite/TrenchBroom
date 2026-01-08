#include "BoxSelectionTool.h"

#include "render/Camera.h"
#include "ui/Tool.h"
#include "mdl/Grid.h"
#include "ui/HandleDragTracker.h"
#include "ui/InputState.h"
#include "mdl/Map.h"
#include "mdl/Map_Selection.h"
#include "mdl/Transaction.h"
#include "mdl/ModelUtils.h"
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
#include "kdl/overload.h"

#include "vm/intersection.h"

#include <algorithm>
#include <vector>

namespace tb::ui
{

SelectionBoxRenderer::SelectionBoxRenderer() 
  : m_valid(false) 
{
}
  
void SelectionBoxRenderer::setSelectionBounds(const vm::bbox3d& bounds) {
  m_selectionBounds = bounds;
  m_valid = true;
}

void SelectionBoxRenderer::clear() {
  m_valid = false;
}

void SelectionBoxRenderer::render(render::RenderContext& renderContext, render::RenderBatch& renderBatch) const {
  if (!m_valid) {
    return;
  }
  
  auto renderService = render::RenderService{renderContext, renderBatch};
  
  
  const auto color = Color{RgbaB{51, 102, 255, 153}};
  renderService.setForegroundColor(color);
  renderService.setLineWidth(2.0f); 
  
  
  const auto& camera = renderContext.camera();
  const auto direction = camera.direction();
  
  
  const auto min = m_selectionBounds.min;
  const auto max = m_selectionBounds.max;
  
  
  vm::axis::type majorAxis = vm::find_abs_max_component(direction);
  
  if (majorAxis == vm::axis::z) {
    
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

BoxSelectionDragDelegate::BoxSelectionDragDelegate(
  Tool& tool, mdl::Map& map)
  : m_tool{tool}
  , m_map{map}
{
}

HandlePositionProposer BoxSelectionDragDelegate::start(
  const InputState& inputState,
  const vm::vec3d& initialHandlePosition,
  const vm::vec3d& handleOffset) 
{
  
  m_selectionBounds = vm::bbox3d{initialHandlePosition, initialHandlePosition};
  
  
  m_renderer.setSelectionBounds(m_selectionBounds);
  
  
  
  m_tool.refreshViews();

  const auto& camera = inputState.camera();
  const auto plane = vm::plane3d{
    initialHandlePosition,
    vm::vec3d{vm::get_abs_max_component_axis(camera.direction())}};

  return makeHandlePositionProposer(
    makePlaneHandlePicker(plane, handleOffset), makeIdentityHandleSnapper());
}

DragStatus BoxSelectionDragDelegate::update(
  const InputState&,
  const DragState& dragState,
  const vm::vec3d& proposedHandlePosition) 
{
  
  
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
  
  
  const double minSize = 0.1; 
  
  
  if (max.x() - min.x() < minSize) {
    max = vm::vec3d(min.x() + minSize, max.y(), max.z());
  }
  if (max.y() - min.y() < minSize) {
    max = vm::vec3d(max.x(), min.y() + minSize, max.z());
  }
  if (max.z() - min.z() < minSize) {
    max = vm::vec3d(max.x(), max.y(), min.z() + minSize);
  }
  
  
  m_selectionBounds = vm::bbox3d{min, max};
  
  
  m_renderer.setSelectionBounds(m_selectionBounds);
  
  
  m_tool.refreshViews();
  return DragStatus::Continue;
}

void BoxSelectionDragDelegate::end(
  const InputState& inputState, 
  const DragState&) 
{ 
  
  if (inputState.mouseButtonsDown(MouseButtons::Right)) {
    m_renderer.clear();
    return;
  }

  try {
    
    if (m_selectionBounds.is_empty()) {
      return;
    }
      
    
    const auto& camera = inputState.camera();
    const auto direction = camera.direction();
    vm::axis::type majorAxis = vm::find_abs_max_component(direction);
      
    
    auto allNodes = std::vector<mdl::Node*>{};
    if (auto* world = m_map.world()) {
        world->accept(kdl::overload(
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
    }
      
    
    auto selectedNodes = std::vector<mdl::Node*>{};
    for (auto* node : allNodes) {
      const auto& nodeBounds = node->logicalBounds();
      const auto nodeCenter = nodeBounds.center();
      
      bool nodeSelected = false;
      
      
      if (majorAxis == vm::axis::z) {
        
        nodeSelected = nodeCenter.x() >= m_selectionBounds.min.x() &&
                      nodeCenter.x() <= m_selectionBounds.max.x() &&
                      nodeCenter.y() >= m_selectionBounds.min.y() &&
                      nodeCenter.y() <= m_selectionBounds.max.y();
      } else if (majorAxis == vm::axis::y) {
        
        nodeSelected = nodeCenter.x() >= m_selectionBounds.min.x() &&
                      nodeCenter.x() <= m_selectionBounds.max.x() &&
                      nodeCenter.z() >= m_selectionBounds.min.z() &&
                      nodeCenter.z() <= m_selectionBounds.max.z();
      } else {
        
        nodeSelected = nodeCenter.y() >= m_selectionBounds.min.y() &&
                      nodeCenter.y() <= m_selectionBounds.max.y() &&
                      nodeCenter.z() >= m_selectionBounds.min.z() &&
                      nodeCenter.z() <= m_selectionBounds.max.z();
      }
      
      if (nodeSelected) {
        selectedNodes.push_back(node);
      }
    }
    
    
    auto selectableNodes = mdl::collectSelectableNodes(
      selectedNodes, m_map.editorContext());
      
    
    if (!selectableNodes.empty()) {
      auto transaction = mdl::Transaction{m_map, "Box Select"};
      mdl::deselectAll(m_map);
      mdl::selectNodes(m_map, selectableNodes);
      transaction.commit();
    }
    
    
    m_renderer.clear();
  } catch (const std::exception&) {
  } catch (...) {
  }
}

void BoxSelectionDragDelegate::cancel(const DragState&) 
{ 
  
  m_renderer.clear();
}

void BoxSelectionDragDelegate::render(
  const InputState&,
  const DragState&,
  render::RenderContext& renderContext,
  render::RenderBatch& renderBatch) const
{
    m_renderer.render(renderContext, renderBatch);
}

} // namespace tb::ui
