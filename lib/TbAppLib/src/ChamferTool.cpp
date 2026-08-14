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

#include "ui/ChamferTool.h"

#include "mdl/Map.h"
#include "mdl/Map_Geometry.h"
#include "mdl/NodeHandleManager.h"
#include "mdl/NodeHandles.h"
#include "ui/MapDocument.h"

#include "kd/contracts.h"
#include "kd/string_format.h"

namespace tb::ui
{

ChamferTool::ChamferTool(MapDocument& document)
  : Tool{false}
  , m_document{document}
  , m_edgeTool{document}
  , m_vertexTool{document}
{
  const auto connectTool = [this](Tool& tool) {
    m_notifierConnection +=
      tool.refreshViewsNotifier.connect([this](Tool&) { refreshViews(); });
    m_notifierConnection += tool.toolHandleSelectionChangedNotifier.connect(
      [this](Tool&) { notifyToolHandleSelectionChanged(); });
  };
  connectTool(m_edgeTool);
  connectTool(m_vertexTool);
}

ChamferTarget ChamferTool::target() const
{
  return m_target;
}

void ChamferTool::setTarget(const ChamferTarget target)
{
  if (m_target == target)
  {
    return;
  }

  const auto wasActive = active();
  if (wasActive)
  {
    deactivateTargetTool();
  }

  m_target = target;

  if (wasActive)
  {
    const auto activated = activateTargetTool();
    contract_assert(activated);
  }

  targetDidChangeNotifier(*this);
  notifyToolHandleSelectionChanged();
  refreshViews();
}

EdgeTool& ChamferTool::edgeTool()
{
  return m_edgeTool;
}

VertexTool& ChamferTool::vertexTool()
{
  return m_vertexTool;
}

size_t ChamferTool::selectedHandleCount() const
{
  const auto& handles = m_document.map().nodeHandles();
  switch (m_target)
  {
  case ChamferTarget::Edges:
    return handles.selectedHandleCount<mdl::EdgeHandle>();
  case ChamferTarget::Vertices:
    return handles.selectedHandleCount<mdl::VertexHandle>();
  }
  return 0u;
}

bool ChamferTool::canApply(const double distance, const int segments) const
{
  return active() && selectedHandleCount() > 0u && distance > 0.0
         && (m_target == ChamferTarget::Vertices || segments > 0);
}

bool ChamferTool::apply(const double distance, const int segments)
{
  if (!canApply(distance, segments))
  {
    return false;
  }

  auto& map = m_document.map();
  auto result = false;

  switch (m_target)
  {
  case ChamferTarget::Edges: {
    const auto edgePositions =
      mdl::EdgeHandle::getPositions(map.nodeHandles().selectedHandles<mdl::EdgeHandle>());
    const auto commandName =
      kdl::str_plural(edgePositions.size(), "Chamfer Brush Edge", "Chamfer Brush Edges");
    result = mdl::chamferEdges(map, commandName, edgePositions, distance, segments);
    break;
  }
  case ChamferTarget::Vertices: {
    const auto vertexPositions = mdl::VertexHandle::getPositions(
      map.nodeHandles().selectedHandles<mdl::VertexHandle>());
    const auto commandName = kdl::str_plural(
      vertexPositions.size(), "Chamfer Brush Vertex", "Chamfer Brush Vertices");
    result = mdl::chamferVertices(map, commandName, vertexPositions, distance);
    break;
  }
  }

  if (result)
  {
    notifyToolHandleSelectionChanged();
    refreshViews();
  }
  return result;
}

bool ChamferTool::canRemoveSelection() const
{
  switch (m_target)
  {
  case ChamferTarget::Edges:
    return m_edgeTool.canRemoveSelection();
  case ChamferTarget::Vertices:
    return m_vertexTool.canRemoveSelection();
  }
  return false;
}

void ChamferTool::removeSelection()
{
  contract_pre(canRemoveSelection());

  switch (m_target)
  {
  case ChamferTarget::Edges:
    m_edgeTool.removeSelection();
    break;
  case ChamferTarget::Vertices:
    m_vertexTool.removeSelection();
    break;
  }
}

void ChamferTool::moveSelection(const vm::vec3d& delta)
{
  switch (m_target)
  {
  case ChamferTarget::Edges:
    m_edgeTool.moveSelection(delta);
    break;
  case ChamferTarget::Vertices:
    m_vertexTool.moveSelection(delta);
    break;
  }
}

bool ChamferTool::activateTargetTool()
{
  switch (m_target)
  {
  case ChamferTarget::Edges:
    return m_edgeTool.activate();
  case ChamferTarget::Vertices:
    return m_vertexTool.activate();
  }
  return false;
}

void ChamferTool::deactivateTargetTool()
{
  switch (m_target)
  {
  case ChamferTarget::Edges:
    contract_assert(m_edgeTool.active());
    m_edgeTool.deactivate();
    break;
  case ChamferTarget::Vertices:
    contract_assert(m_vertexTool.active());
    m_vertexTool.deactivate();
    break;
  }
}

bool ChamferTool::doActivate()
{
  return activateTargetTool();
}

bool ChamferTool::doDeactivate()
{
  deactivateTargetTool();
  return true;
}

} // namespace tb::ui
