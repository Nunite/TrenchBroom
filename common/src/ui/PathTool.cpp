#include "PathTool.h"

#include "mdl/Map.h"
#include "mdl/Transaction.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/WorldNode.h"
#include "Color.h"
#include "render/RenderService.h"
#include "render/RenderContext.h"
#include "render/RenderBatch.h"
#include "ui/QtUtils.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace tb::ui
{

PathTool::PathTool(mdl::Map& map)
    : Tool(false)
    , m_map(map)
{
    connectObservers();
}

PathTool::~PathTool() = default;

void PathTool::connectObservers()
{
    // Connect to map notifications if needed
}

bool PathTool::doActivate()
{
    return true;
}

bool PathTool::doDeactivate()
{
    clearPoints();
    return true;
}

void PathTool::addPoint(const vm::vec3d& point)
{
    m_points.push_back(point);
    m_redoStack.clear();
    refreshViews();
}

void PathTool::removeLastPoint()
{
    if (!m_points.empty())
    {
        m_redoStack.push_back(m_points.back());
        m_points.pop_back();
        refreshViews();
    }
}

void PathTool::redoLastPoint()
{
    if (!m_redoStack.empty())
    {
        m_points.push_back(m_redoStack.back());
        m_redoStack.pop_back();
        refreshViews();
    }
}

void PathTool::clearPoints()
{
    m_points.clear();
    m_redoStack.clear();
    refreshViews();
}

const std::vector<vm::vec3d>& PathTool::points() const
{
    return m_points;
}

bool PathTool::hasPoints() const
{
    return !m_points.empty();
}

bool PathTool::canRedoPoint() const
{
    return !m_redoStack.empty();
}

void PathTool::createPathEntities()
{
    if (m_points.empty()) return;

    const auto* def = m_map.entityDefinitionManager().definition("path_corner");
    if (!def) return;

    mdl::Transaction transaction(m_map, "Create Path");

    // Create path_corner entities
    // In Quake/GoldSrc, paths are usually a chain of path_corner entities
    // connected by "target" -> "targetname"
    
    // We'll generate a unique ID for this path sequence
    static int pathSequenceId = 0;
    pathSequenceId++;
    std::string pathPrefix = "path_" + std::to_string(pathSequenceId) + "_";

    std::vector<mdl::Node*> newNodes;
    newNodes.reserve(m_points.size());

    for (size_t i = 0; i < m_points.size(); ++i)
    {
        auto entity = mdl::Entity{{{mdl::EntityPropertyKeys::Classname, def->name}}};
        
        if (m_map.world()->entityPropertyConfig().setDefaultProperties)
        {
            mdl::setDefaultProperties(*def, entity, mdl::SetDefaultPropertyMode::SetAll);
        }

        entity.setOrigin(m_points[i]);
        
        std::string currentName = pathPrefix + std::to_string(i + 1);
        entity.addOrUpdateProperty("targetname", currentName);

        if (i < m_points.size() - 1)
        {
            std::string nextName = pathPrefix + std::to_string(i + 2);
            entity.addOrUpdateProperty("target", nextName);
        }

        auto* entityNode = new mdl::EntityNode{std::move(entity)};
        newNodes.push_back(entityNode);
    }

    mdl::deselectAll(m_map);

    auto* parent = mdl::parentForNodes(m_map);
    if (mdl::addNodes(m_map, {{parent, newNodes}}).empty())
    {
        transaction.cancel();
        return;
    }

    mdl::selectNodes(m_map, newNodes);

    transaction.commit();
    clearPoints();
}

void PathTool::render(render::RenderContext& renderContext, render::RenderBatch& renderBatch)
{
    if (m_points.empty()) return;

    render::RenderService renderService(renderContext, renderBatch);
    
    // Draw points
    renderService.setForegroundColor(tb::RgbaF(1.0f, 0.5f, 0.0f, 1.0f)); // Orange
    
    for (const auto& pt : m_points)
    {
        renderService.renderHandle(vm::vec3f(pt));
    }

    // Draw lines
    if (m_points.size() > 1)
    {
        renderService.setLineWidth(2.0f);
        std::vector<vm::vec3f> pointsf;
        pointsf.reserve(m_points.size());
        for (const auto& pt : m_points) {
            pointsf.push_back(vm::vec3f(pt));
        }
        renderService.renderLineStrip(pointsf);
    }
}

QWidget* PathTool::doCreatePage(QWidget* parent)
{
    // User requested no instructions panel
    return new QWidget(parent);
}

} // namespace tb::ui
