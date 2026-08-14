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

#include "base/PreferenceManager.h"
#include "mdl/Brush.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/Map.h"
#include "mdl/Map_Geometry.h"
#include "mdl/NodeHandleManager.h"
#include "mdl/NodeHandles.h"
#include "prefs/Preferences.h"
#include "render/BrushRenderer.h"
#include "render/RenderBatch.h"
#include "render/RenderContext.h"
#include "ui/MapDocument.h"

#include "kd/contracts.h"
#include "kd/result.h"
#include "kd/string_format.h"

#include <utility>

namespace tb::ui
{

ChamferTool::ChamferTool(MapDocument& document)
  : Tool{false}
  , m_document{document}
  , m_edgeTool{document}
  , m_vertexTool{document}
  , m_brushRenderer{std::make_unique<render::BrushRenderer>()}
{
  const auto connectTool = [this](Tool& tool) {
    m_notifierConnection += tool.refreshViewsNotifier.connect([this](Tool&) {
      updatePreview();
      refreshViews();
    });
    m_notifierConnection += tool.toolHandleSelectionChangedNotifier.connect(
      [this](Tool&) { notifyToolHandleSelectionChanged(); });
  };
  connectTool(m_edgeTool);
  connectTool(m_vertexTool);
}

ChamferTool::~ChamferTool() = default;

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

  updatePreview();
  targetDidChangeNotifier(*this);
  notifyToolHandleSelectionChanged();
  refreshViews();
}

const ChamferParameters& ChamferTool::parameters() const
{
  return m_parameters;
}

void ChamferTool::setParameters(const ChamferParameters& parameters)
{
  if (m_parameters == parameters)
  {
    return;
  }

  m_parameters = parameters;
  updatePreview();
  parametersDidChangeNotifier(*this);
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

bool ChamferTool::hasPreview() const
{
  return !m_previewBrushes.empty() && !m_previewFailed;
}

bool ChamferTool::previewFailed() const
{
  return m_previewFailed;
}

bool ChamferTool::canApply() const
{
  return active() && hasPreview();
}

bool ChamferTool::apply()
{
  if (!canApply())
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
    result = mdl::chamferEdges(
      map, commandName, edgePositions, m_parameters.distance, m_parameters.segments);
    break;
  }
  case ChamferTarget::Vertices: {
    const auto vertexPositions = mdl::VertexHandle::getPositions(
      map.nodeHandles().selectedHandles<mdl::VertexHandle>());
    const auto commandName = kdl::str_plural(
      vertexPositions.size(), "Chamfer Brush Vertex", "Chamfer Brush Vertices");
    result =
      mdl::chamferVertices(map, commandName, vertexPositions, m_parameters.distance);
    break;
  }
  }

  updatePreview();
  if (result)
  {
    notifyToolHandleSelectionChanged();
    refreshViews();
  }
  return result;
}

void ChamferTool::renderPreview(
  render::RenderContext& renderContext, render::RenderBatch& renderBatch) const
{
  if (!hasPreview())
  {
    return;
  }

  m_brushRenderer->setFaceColor(pref(Preferences::FaceColor));
  m_brushRenderer->setEdgeColor(pref(Preferences::SelectedEdgeColor));
  m_brushRenderer->setShowEdges(true);
  m_brushRenderer->setShowOccludedEdges(true);
  m_brushRenderer->setOccludedEdgeColor(RgbaF{
    pref(Preferences::SelectedEdgeColor).to<RgbF>(),
    pref(Preferences::OccludedSelectedEdgeAlpha)});
  m_brushRenderer->setTint(true);
  m_brushRenderer->setTintColor(pref(Preferences::SelectedFaceColor));
  m_brushRenderer->render(renderContext, renderBatch);
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

void ChamferTool::clearPreview()
{
  m_brushRenderer->clear();
  m_previewBrushes.clear();
  m_previewFailed = false;
}

void ChamferTool::updatePreview()
{
  clearPreview();

  if (
    !active() || selectedHandleCount() == 0u || m_parameters.distance <= 0.0
    || (m_target == ChamferTarget::Edges && m_parameters.segments < 1))
  {
    return;
  }

  auto& map = m_document.map();
  const auto edgePositions = m_target == ChamferTarget::Edges
                               ? mdl::EdgeHandle::getPositions(
                                   map.nodeHandles().selectedHandles<mdl::EdgeHandle>())
                               : std::vector<vm::segment3d>{};
  const auto vertexPositions =
    m_target == ChamferTarget::Vertices
      ? mdl::VertexHandle::getPositions(
          map.nodeHandles().selectedHandles<mdl::VertexHandle>())
      : std::vector<vm::vec3d>{};

  auto previewBrushes = std::vector<std::unique_ptr<mdl::BrushNode>>{};
  previewBrushes.reserve(map.selection().brushes.size());
  auto affected = false;

  for (const auto* sourceNode : map.selection().brushes)
  {
    auto previewBrush = sourceNode->brush();

    if (m_target == ChamferTarget::Edges)
    {
      auto brushEdges = std::vector<vm::segment3d>{};
      for (const auto& edge : edgePositions)
      {
        if (previewBrush.hasEdge(edge))
        {
          brushEdges.push_back(edge);
        }
      }

      if (!brushEdges.empty())
      {
        affected = true;
        if (
          !previewBrush.canChamferEdges(
            map.worldBounds(), brushEdges, m_parameters.distance, m_parameters.segments)
          || !(
            previewBrush.chamferEdges(
              map.worldBounds(),
              brushEdges,
              m_parameters.distance,
              m_parameters.segments,
              map.editorContext().uvLock())
            | kdl::is_success()))
        {
          clearPreview();
          m_previewFailed = true;
          return;
        }
      }
    }
    else
    {
      auto brushVertices = std::vector<vm::vec3d>{};
      for (const auto& vertex : vertexPositions)
      {
        if (previewBrush.hasVertex(vertex))
        {
          brushVertices.push_back(vertex);
        }
      }

      if (!brushVertices.empty())
      {
        affected = true;
        if (
          !previewBrush.canChamferVertices(
            map.worldBounds(), brushVertices, m_parameters.distance)
          || !(
            previewBrush.chamferVertices(
              map.worldBounds(),
              brushVertices,
              m_parameters.distance,
              map.editorContext().uvLock())
            | kdl::is_success()))
        {
          clearPreview();
          m_previewFailed = true;
          return;
        }
      }
    }

    previewBrushes.push_back(std::make_unique<mdl::BrushNode>(std::move(previewBrush)));
  }

  if (!affected)
  {
    return;
  }

  m_previewBrushes = std::move(previewBrushes);
  for (const auto& brushNode : m_previewBrushes)
  {
    m_brushRenderer->addBrush(*brushNode);
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
  clearPreview();
  refreshViews();
  return true;
}

} // namespace tb::ui
