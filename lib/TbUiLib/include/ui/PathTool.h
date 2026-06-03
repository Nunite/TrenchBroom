#pragma once

#include "ui/Tool.h"
#include "NotifierConnection.h"
#include "vm/vec.h"
#include <vector>

namespace tb::mdl
{
class Map;
} // namespace tb::mdl

namespace tb::render
{
class RenderContext;
class RenderBatch;
} // namespace tb::render

namespace tb::ui
{

class PathTool : public Tool
{
private:
    mdl::Map& m_map;
    std::vector<vm::vec3d> m_points;
    std::vector<vm::vec3d> m_redoStack;
    NotifierConnection m_notifierConnection;
    
public:
    explicit PathTool(mdl::Map& map);
    ~PathTool() override;

    mdl::Map& map() { return m_map; }
    const mdl::Map& map() const { return m_map; }

    // Point management
    void addPoint(const vm::vec3d& point);
    void removeLastPoint();
    void redoLastPoint();
    void clearPoints();
    const std::vector<vm::vec3d>& points() const;
    bool hasPoints() const;
    bool canRedoPoint() const;

    // Rendering
    void render(render::RenderContext& renderContext, render::RenderBatch& renderBatch);

    // Entity creation
    void createPathEntities();

protected:
    bool doActivate() override;
    bool doDeactivate() override;
    QWidget* doCreatePage(QWidget* parent) override;

private:
    void connectObservers();
};

} // namespace tb::ui
