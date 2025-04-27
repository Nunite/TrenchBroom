//Added by Lws

#include "BoxSelectionTool.h"

#include "render/Camera.h"
#include "ui/DrawShapeTool.h"
#include "ui/Grid.h"
#include "ui/HandleDragTracker.h"
#include "ui/InputState.h"
#include "ui/MapDocument.h"
#include "ui/Transaction.h"
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

BoxSelectionDragDelegate::BoxSelectionDragDelegate(
  DrawShapeTool& tool, std::weak_ptr<MapDocument> document)
  : m_tool{tool}
  , m_document{std::move(document)}
{
}

HandlePositionProposer BoxSelectionDragDelegate::start(
  const InputState& inputState,
  const vm::vec3d& initialHandlePosition,
  const vm::vec3d& handleOffset) 
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

DragStatus BoxSelectionDragDelegate::update(
  const InputState&,
  const DragState& dragState,
  const vm::vec3d& proposedHandlePosition) 
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

void BoxSelectionDragDelegate::end(
  const InputState& inputState, 
  const DragState&) 
{ 
  // 如果是右键被按下，不执行选择操作，允许右键菜单显示
  if (inputState.mouseButtonsDown(MouseButtons::Right)) {
    m_renderer.clear();
    return;
  }

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
    } catch (const std::exception&) {
    } catch (...) {
    }
  }
}

void BoxSelectionDragDelegate::cancel(const DragState&) 
{ 
  // 取消选择操作
  m_renderer.clear();
}

void BoxSelectionDragDelegate::render(
  const InputState&,
  const DragState&,
  render::RenderContext& renderContext,
  render::RenderBatch& renderBatch) const
{
  // 使用自定义渲染器绘制选择框
  m_renderer.render(renderContext, renderBatch);
}

} // namespace tb::ui 