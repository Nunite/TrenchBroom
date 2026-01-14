/*
 Copyright (C) 2026

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

#include "ModelBrowserView.h"

#include "PreferenceManager.h"
#include "Preferences.h"
#include "Exceptions.h"
#include "mdl/EntityModelManager.h"
#include "mdl/Map.h"
#include "io/PathQt.h"
#include "render/ActiveShader.h"
#include "render/FontDescriptor.h"
#include "render/FontManager.h"
#include "render/TextureFont.h"
#include "render/GL.h"
#include "render/MaterialIndexRangeRenderer.h"
#include "render/PrimType.h"
#include "render/RenderUtils.h"
#include "render/Shaders.h"
#include "render/Transformation.h"
#include "render/VertexArray.h"

#include "vm/mat.h"
#include "vm/mat_ext.h"
#include "vm/quat.h"
#include "vm/vec.h"

#include <QScrollBar>

#include <algorithm>

namespace tb::ui
{

ModelBrowserView::ModelBrowserView(
  QScrollBar* scrollBar, GLContextManager& contextManager, mdl::Map& map)
  : CellView{contextManager, scrollBar}
  , m_map{map}
{
  const auto hRotation = vm::quatf{vm::vec3f{0, 0, 1}, vm::to_radians(-30.0f)};
  const auto vRotation = vm::quatf{vm::vec3f{0, 1, 0}, vm::to_radians(20.0f)};
  m_rotation = vRotation * hRotation;

  if (scrollBar)
  {
    connect(scrollBar, &QAbstractSlider::valueChanged, this, [this](const int) {
      update();
    });
  }

  m_notifierConnection += m_map.resourcesWereProcessedNotifier.connect(
    this, &ModelBrowserView::resourcesWereProcessed);
}

ModelBrowserView::~ModelBrowserView()
{
  clear();
}

void ModelBrowserView::setModelPaths(std::vector<std::filesystem::path> modelPaths)
{
  m_modelPaths = std::move(modelPaths);
  invalidate();
  update();
}

void ModelBrowserView::resourcesWereProcessed(const std::vector<mdl::ResourceId>&)
{
  invalidate();
  update();
}

void ModelBrowserView::doInitLayout(Layout& layout)
{
  layout.setOuterMargin(5.0f);
  layout.setGroupMargin(0.0f);
  layout.setRowMargin(5.0f);
  layout.setCellMargin(5.0f);
  layout.setTitleMargin(2.0f);
  layout.setCellWidth(93.0f, 93.0f);
  layout.setCellHeight(93.0f, 93.0f);
  layout.setMaxUpScale(1.5f);
}

void ModelBrowserView::doReloadLayout(Layout& layout)
{
  const auto& fontPath = pref(Preferences::RendererFontPath());
  const auto fontSize = pref(Preferences::BrowserFontSize);
  const auto font = render::FontDescriptor{fontPath, size_t(fontSize)};
  const auto maxCellWidth = layout.maxCellWidth();

  for (const auto& modelPath : m_modelPaths)
  {
    const auto titleUtf8 = io::pathAsGenericQString(modelPath.filename()).toUtf8();
    const auto title = std::string{titleUtf8.constData(), size_t(titleUtf8.size())};
    const auto titleHeight = fontManager().font(font).measure(title).y();

    layout.addItem(ModelCellData{modelPath}, title, 93.0f, 93.0f, maxCellWidth, titleHeight + 4.0f);
  }
}

void ModelBrowserView::doClear() {}

void ModelBrowserView::doRender(Layout& layout, const float y, const float height)
{
  const auto viewLeft = float(0);
  const auto viewTop = float(size().height());
  const auto viewRight = float(size().width());
  const auto viewBottom = float(0);

  const auto projection =
    vm::ortho_matrix(-1024.0f, 1024.0f, viewLeft, viewTop, viewRight, viewBottom);
  const auto view =
    vm::view_matrix(CameraDirection, CameraUp) * vm::translation_matrix(CameraPosition);
  auto transformation = render::Transformation{projection, view};

  renderModels(layout, y, height, transformation);
}

bool ModelBrowserView::shouldRenderFocusIndicator() const
{
  return false;
}

const Color& ModelBrowserView::getBackgroundColor()
{
  return pref(Preferences::BrowserBackgroundColor);
}

void ModelBrowserView::renderModels(
  Layout& layout,
  const float y,
  const float height,
  render::Transformation& transformation)
{
  glAssert(glFrontFace(GL_CW));

  auto& entityModelManager = m_map.entityModelManager();
  entityModelManager.prepare(vboManager());

  auto shader = render::ActiveShader{shaderManager(), render::Shaders::EntityModelShader};
  shader.set("ApplyTinting", false);
  shader.set("Brightness", pref(Preferences::Brightness));
  shader.set("GrayScale", false);

  shader.set("CameraPosition", CameraPosition);
  shader.set("CameraDirection", CameraDirection);
  shader.set("CameraRight", vm::cross(CameraDirection, CameraUp));
  shader.set("CameraUp", CameraUp);
  shader.set("ViewMatrix", transformation.viewMatrix());

  for (const auto& group : layout.groups())
  {
    if (group.intersectsY(y, height))
    {
      for (const auto& row : group.rows())
      {
        if (row.intersectsY(y, height))
        {
          for (const auto& cell : row.cells())
          {
            const auto& cellData = this->cellData(cell);
            const auto& modelPath = cellData.modelPath;
            if (modelPath.empty())
            {
              continue;
            }
            const auto spec = mdl::ModelSpecification{modelPath, 0, 0};
            auto* modelRenderer = entityModelManager.renderer(spec);
            if (!modelRenderer)
            {
              continue;
            }
            modelRenderer->prepare(vboManager());

            shader.set("Orientation", int(mdl::Orientation::Oriented));

            auto bounds = vm::bbox3f{8.0f};
            if (const auto* frame = entityModelManager.frame(spec))
            {
              bounds = frame->bounds();
            }

            const auto center = bounds.center();
            const auto transform = vm::translation_matrix(center) * vm::rotation_matrix(m_rotation)
                                   * vm::translation_matrix(-center);

            const auto itemTrans = itemTransformation(cell, y, height, bounds, transform);
            shader.set("ModelMatrix", itemTrans);

            const auto multMatrix =
              render::MultiplyModelMatrix{transformation, itemTrans};

            auto renderFunc = render::DefaultMaterialRenderFunc{
              pref(Preferences::TextureMinFilter), pref(Preferences::TextureMagFilter)};
            modelRenderer->render(renderFunc);
          }
        }
      }
    }
  }
}

vm::mat4x4f ModelBrowserView::itemTransformation(
  const Cell& cell,
  const float y,
  const float height,
  const vm::bbox3f& bounds,
  const vm::mat4x4f& transform) const
{
  const auto offset =
    vm::vec3f{0.0f, cell.itemBounds().left(), height - (cell.itemBounds().bottom() - y)};

  const auto rotatedBounds = bounds.transform(transform);
  const auto rotationOffset =
    vm::vec3f{0.0f, -rotatedBounds.min.y(), -rotatedBounds.min.z()};

  const auto rotatedBoundsSize = rotatedBounds.size();
  const auto targetWidth = cell.itemBounds().width;
  const auto targetHeight = cell.itemBounds().height;

  const auto widthScale =
    rotatedBoundsSize.y() > 0.0f ? (targetWidth / rotatedBoundsSize.y()) : 1.0f;
  const auto heightScale =
    rotatedBoundsSize.z() > 0.0f ? (targetHeight / rotatedBoundsSize.z()) : 1.0f;
  const auto scaling = std::min(widthScale, heightScale);

  return vm::translation_matrix(offset) * vm::scaling_matrix(vm::vec3f::fill(scaling))
         * vm::translation_matrix(rotationOffset) * transform;
}

QString ModelBrowserView::tooltip(const Cell& cell)
{
  return QString::fromStdString(cellData(cell).modelPath.generic_string());
}

const ModelCellData& ModelBrowserView::cellData(const Cell& cell) const
{
  return cell.itemAs<ModelCellData>();
}

} // namespace tb::ui
