/*
 Copyright (C) 2026 XiangXtreme

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

#pragma once

#include "base/Notifier.h"
#include "base/NotifierConnection.h"
#include "ui/EdgeTool.h"
#include "ui/Tool.h"
#include "ui/VertexTool.h"

#include "vm/vec.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace tb::mdl
{
class BrushNode;
}

namespace tb::render
{
class BrushRenderer;
class RenderBatch;
class RenderContext;
} // namespace tb::render

namespace tb::ui
{
class MapDocument;

enum class ChamferTarget
{
  Edges,
  Vertices,
};

struct ChamferParameters
{
  double distance = 8.0;
  int segments = 1;

  friend bool operator==(const ChamferParameters&, const ChamferParameters&) = default;
};

class ChamferTool : public Tool
{
private:
  MapDocument& m_document;
  EdgeTool m_edgeTool;
  VertexTool m_vertexTool;
  ChamferTarget m_target = ChamferTarget::Edges;
  ChamferParameters m_parameters;
  std::vector<std::unique_ptr<mdl::BrushNode>> m_previewBrushes;
  std::unique_ptr<render::BrushRenderer> m_brushRenderer;
  bool m_previewFailed = false;
  NotifierConnection m_notifierConnection;

public:
  Notifier<ChamferTool&> targetDidChangeNotifier;
  Notifier<ChamferTool&> parametersDidChangeNotifier;

public:
  explicit ChamferTool(MapDocument& document);
  ~ChamferTool() override;

  ChamferTarget target() const;
  void setTarget(ChamferTarget target);

  const ChamferParameters& parameters() const;
  void setParameters(const ChamferParameters& parameters);

  EdgeTool& edgeTool();
  VertexTool& vertexTool();

  size_t selectedHandleCount() const;
  bool hasPreview() const;
  bool previewFailed() const;
  bool canApply() const;
  bool apply();

  void renderPreview(
    render::RenderContext& renderContext, render::RenderBatch& renderBatch) const;

  bool canRemoveSelection() const;
  void removeSelection();
  void moveSelection(const vm::vec3d& delta);

private:
  void clearPreview();
  void updatePreview();

  bool activateTargetTool();
  void deactivateTargetTool();

  bool doActivate() override;
  bool doDeactivate() override;
};

} // namespace tb::ui
