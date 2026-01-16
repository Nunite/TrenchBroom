#pragma once

#include "ui/ToolController.h"

namespace tb::ui
{

class PathTool;

class PathToolController : public ToolController
{
private:
    PathTool& m_tool;

public:
    explicit PathToolController(PathTool& tool);
    ~PathToolController() override;

    Tool& tool() override;
    const Tool& tool() const override;

    bool mouseClick(const InputState& inputState) override;
    bool mouseDoubleClick(const InputState& inputState) override;
    void render(
        const InputState& inputState,
        render::RenderContext& renderContext,
        render::RenderBatch& renderBatch) override;
        
    // Handle keyboard input via modifierKeyChange or we might need to check standard shortcuts?
    // ToolController doesn't have explicit keyDown, but we can check InputState in other events
    // or rely on the host to forward keys.
    // However, usually tools handle Enter/Backspace via specific methods or action bindings.
    // For simplicity, let's assume we use mouse interaction mainly, or check modifiers.
    // Actually, TrenchBroom tools usually use Actions for Enter/Esc.
    // But we can check inputState modifiers.
    
    // Override cancel to handle Esc
    bool cancel() override;
};

} // namespace tb::ui
