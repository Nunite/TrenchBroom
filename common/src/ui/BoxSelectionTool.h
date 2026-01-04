//Added by Lws

#pragma once

#include "ui/HandleDragTracker.h"
#include "vm/bbox.h"

#include <memory>

namespace tb::render
{
class RenderContext;
class RenderBatch;
}

namespace tb::mdl
{
class Map;
class Node;
}

namespace tb::ui
{
class DrawShapeTool;
class InputState;
struct DragState;

class SelectionBoxRenderer
{
private:
  vm::bbox3d m_selectionBounds;
  bool m_valid;
  
public:
  SelectionBoxRenderer();
  
  void setSelectionBounds(const vm::bbox3d& bounds);
  void clear();
  
  void render(render::RenderContext& renderContext, render::RenderBatch& renderBatch) const;
};

class BoxSelectionDragDelegate : public HandleDragTrackerDelegate
{
private:
  DrawShapeTool& m_tool;
  mdl::Map& m_map;
  vm::bbox3d m_selectionBounds;
  SelectionBoxRenderer m_renderer;

public:
  BoxSelectionDragDelegate(DrawShapeTool& tool, mdl::Map& map);

  HandlePositionProposer start(
    const InputState& inputState,
    const vm::vec3d& initialHandlePosition,
    const vm::vec3d& handleOffset) override;

  DragStatus update(
    const InputState& inputState,
    const DragState& dragState,
    const vm::vec3d& proposedHandlePosition) override;

  void end(const InputState& inputState, const DragState& dragState) override;
  void cancel(const DragState& dragState) override;

  void render(
    const InputState& inputState,
    const DragState& dragState,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch) const override;
};

} // namespace tb::ui 