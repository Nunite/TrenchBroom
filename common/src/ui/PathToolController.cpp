
#include "PathToolController.h"

#include "PathTool.h"
#include "ui/InputState.h"
#include "mdl/Map.h"
#include "mdl/Grid.h"
#include "mdl/PickResult.h"
#include "mdl/Hit.h"
#include "mdl/HitFilter.h"
#include "mdl/BrushNode.h"
#include "render/RenderContext.h"
#include "render/Camera.h"
#include "vm/intersection.h"
#include "vm/plane.h"
#include "vm/vec.h"

namespace tb::ui
{

PathToolController::PathToolController(PathTool& tool) : m_tool(tool)
{
}

PathToolController::~PathToolController() = default;

Tool& PathToolController::tool()
{
    return m_tool;
}

const Tool& PathToolController::tool() const
{
    return m_tool;
}

bool PathToolController::mouseClick(const InputState& inputState)
{
    if (!m_tool.active())
        return false;

    if (inputState.mouseButtons() != MouseButtons::Left)
        return false;

    // Try to pick geometry first
    const auto& pickResult = inputState.pickResult();
    auto hit = pickResult.first(mdl::HitFilters::type(mdl::BrushNode::BrushHitType));

    vm::vec3d point;
    bool found = false;

    if (hit.isMatch())
    {
        point = hit.hitPoint();
        found = true;
    }
    else
    {
        // Intersect with a plane perpendicular to the dominant view axis
        auto ray = inputState.pickRay();
        auto dir = ray.direction;
        vm::vec3d planeNormal(0, 0, 1); // Default Z up

        if (std::abs(dir.x()) > std::abs(dir.y()) && std::abs(dir.x()) > std::abs(dir.z()))
            planeNormal = vm::vec3d(1, 0, 0);
        else if (std::abs(dir.y()) > std::abs(dir.x()) && std::abs(dir.y()) > std::abs(dir.z()))
            planeNormal = vm::vec3d(0, 1, 0);

        auto dist = vm::intersect_ray_plane(ray, vm::plane3d(vm::vec3d(0, 0, 0), planeNormal));
        if (dist)
        {
            point = vm::point_at_distance(ray, *dist);
            found = true;
        }
    }

    if (found)
    {
        // Snap to grid
        point = m_tool.map().grid().snap(point);
        m_tool.addPoint(point);
        return true;
    }
    
    return false;
}

bool PathToolController::mouseDoubleClick(const InputState& inputState)
{
    if (!m_tool.active())
        return false;

    if (inputState.mouseButtons() != MouseButtons::Left)
        return false;
        
    m_tool.createPathEntities();
    return true;
}

void PathToolController::render(
    const InputState& inputState,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch)
{
    if (!m_tool.active())
        return;

    m_tool.render(renderContext, renderBatch);
}

bool PathToolController::cancel()
{
    if (!m_tool.active())
        return false;

    if (m_tool.hasPoints())
    {
        m_tool.removeLastPoint();
        return true;
    }
    return false;
}

} // namespace tb::ui
