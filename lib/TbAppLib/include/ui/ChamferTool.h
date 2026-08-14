/*
 Copyright (C) 2026 Kristian Duske

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

namespace tb::ui
{
class MapDocument;

enum class ChamferTarget
{
  Edges,
  Vertices,
};

class ChamferTool : public Tool
{
private:
  MapDocument& m_document;
  EdgeTool m_edgeTool;
  VertexTool m_vertexTool;
  ChamferTarget m_target = ChamferTarget::Edges;
  NotifierConnection m_notifierConnection;

public:
  Notifier<ChamferTool&> targetDidChangeNotifier;

public:
  explicit ChamferTool(MapDocument& document);

  ChamferTarget target() const;
  void setTarget(ChamferTarget target);

  EdgeTool& edgeTool();
  VertexTool& vertexTool();

  size_t selectedHandleCount() const;
  bool canApply(double distance, int segments) const;
  bool apply(double distance, int segments);

  bool canRemoveSelection() const;
  void removeSelection();
  void moveSelection(const vm::vec3d& delta);

private:
  bool activateTargetTool();
  void deactivateTargetTool();

  bool doActivate() override;
  bool doDeactivate() override;
};

} // namespace tb::ui
