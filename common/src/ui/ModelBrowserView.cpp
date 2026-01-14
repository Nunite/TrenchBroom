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
#include "io/ResourceUtils.h"
#include "io/PathQt.h"
#include "render/ActiveShader.h"
#include "render/FontDescriptor.h"
#include "render/FontManager.h"
#include "render/GLVertexType.h"
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

#include <QEvent>
#include <QOpenGLContext>
#include <QScrollBar>

#include <algorithm>
#include <ranges>

#include "kdl/path_utils.h"

namespace tb::ui
{

ModelBrowserView::ModelBrowserView(
  QScrollBar* scrollBar, GLContextManager& contextManager, mdl::Map& map)
  : CellView{contextManager, scrollBar}
  , m_map{map}
  , m_scrollBar{scrollBar}
{
  setMouseTracking(true);

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

  const auto pixmap =
    io::loadSVGPixmap(std::filesystem::path{"Map_folder.svg"});
  if (!pixmap.isNull())
  {
    m_folderIconImage =
      pixmap.toImage().convertToFormat(QImage::Format_RGBA8888);
  }
}

ModelBrowserView::~ModelBrowserView()
{
  destroyFolderIconTexture();
  clear();
}

void ModelBrowserView::setModelPaths(
  std::filesystem::path rootFolderPath, std::vector<std::filesystem::path> modelPaths)
{
  m_rootFolderPath = std::move(rootFolderPath);
  m_modelPaths = std::move(modelPaths);
  m_currentFolderPath.clear();
  m_hasSelection = false;
  m_hasHover = false;
  invalidate();
  update();
}

void ModelBrowserView::setCurrentFolderPath(std::filesystem::path currentFolderPath)
{
  currentFolderPath = currentFolderPath.lexically_normal();
  if (currentFolderPath == std::filesystem::path{"."})
  {
    currentFolderPath.clear();
  }

  if (m_currentFolderPath != currentFolderPath)
  {
    m_currentFolderPath = std::move(currentFolderPath);
    m_hasSelection = false;
    m_hasHover = false;
    invalidate();
    update();
  }
}

void ModelBrowserView::doMouseMove(Layout& layout, const float x, const float y)
{
  if (const auto* cell = layout.cellAt(x, y))
  {
    const auto& item = cellData(*cell);
    const auto changed =
      !m_hasHover || m_hoverType != item.type || m_hoverPath != item.path;

    m_hoverType = item.type;
    m_hoverPath = item.path;
    m_hasHover = true;

    if (changed)
    {
      update();
    }
  }
  else
  {
    if (m_hasHover)
    {
      m_hasHover = false;
      update();
    }
  }
}

void ModelBrowserView::leaveEvent(QEvent* event)
{
  CellView::leaveEvent(event);
  if (m_hasHover)
  {
    m_hasHover = false;
    update();
  }
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

  const auto currentFolderAbs = m_currentFolderPath.empty()
                                  ? m_rootFolderPath
                                  : (m_rootFolderPath / m_currentFolderPath);

  auto folderChildren = std::vector<std::filesystem::path>{};
  auto modelChildren = std::vector<std::filesystem::path>{};

  for (const auto& modelPath : m_modelPaths)
  {
    if (modelPath.empty())
    {
      continue;
    }

    const auto folderPath = modelPath.parent_path();
    if (!currentFolderAbs.empty() && !kdl::path_has_prefix(folderPath, currentFolderAbs))
    {
      continue;
    }

    const auto relFromCurrent =
      currentFolderAbs.empty() ? folderPath : folderPath.lexically_relative(currentFolderAbs);

    if (relFromCurrent.empty() || relFromCurrent == std::filesystem::path{"."})
    {
      modelChildren.push_back(modelPath);
      continue;
    }

    const auto first = *relFromCurrent.begin();
    folderChildren.push_back((m_currentFolderPath / first).lexically_normal());
  }

  std::ranges::sort(folderChildren, [](const auto& a, const auto& b) {
    return a.generic_string() < b.generic_string();
  });
  folderChildren.erase(
    std::unique(std::begin(folderChildren), std::end(folderChildren)),
    std::end(folderChildren));

  std::ranges::sort(modelChildren, [](const auto& a, const auto& b) {
    return a.filename().generic_string() < b.filename().generic_string();
  });

  if (!m_currentFolderPath.empty())
  {
    const auto title = std::string{".."};
    const auto titleHeight = fontManager().font(font).measure(title).y();
    layout.addItem(
      BrowserCellData{BrowserCellType::Folder, m_currentFolderPath.parent_path()},
      title,
      93.0f,
      93.0f,
      maxCellWidth,
      titleHeight + 4.0f);
  }

  for (const auto& folderRelPath : folderChildren)
  {
    const auto folderName = folderRelPath.filename();
    const auto titleUtf8 = io::pathAsGenericQString(folderName).toUtf8();
    const auto title = std::string{titleUtf8.constData(), size_t(titleUtf8.size())};
    const auto titleHeight = fontManager().font(font).measure(title).y();

    layout.addItem(
      BrowserCellData{BrowserCellType::Folder, folderRelPath},
      title,
      93.0f,
      93.0f,
      maxCellWidth,
      titleHeight + 4.0f);
  }

  for (const auto& modelPath : modelChildren)
  {
    const auto titleUtf8 = io::pathAsGenericQString(modelPath.filename()).toUtf8();
    const auto title = std::string{titleUtf8.constData(), size_t(titleUtf8.size())};
    const auto titleHeight = fontManager().font(font).measure(title).y();

    layout.addItem(
      BrowserCellData{BrowserCellType::Model, modelPath},
      title,
      93.0f,
      93.0f,
      maxCellWidth,
      titleHeight + 4.0f);
  }
}

void ModelBrowserView::doClear() {}

void ModelBrowserView::doRender(Layout& layout, const float y, const float height)
{
  const auto viewLeft = float(0);
  const auto viewTop = float(size().height());
  const auto viewRight = float(size().width());
  const auto viewBottom = float(0);

  auto uiTransformation = render::Transformation{
    vm::ortho_matrix(-1.0f, 1.0f, viewLeft, viewTop, viewRight, viewBottom),
    vm::view_matrix(vm::vec3f{0, 0, -1}, vm::vec3f{0, 1, 0})
      * vm::translation_matrix(vm::vec3f{0.0f, 0.0f, 0.1f})};
  renderHoveredCellBounds(layout, y, height, BrowserCellType::Model);
  renderSelectedCellBounds(layout, y, height, BrowserCellType::Model);
  renderFolders(layout, y, height);

  const auto projection =
    vm::ortho_matrix(-1024.0f, 1024.0f, viewLeft, viewTop, viewRight, viewBottom);
  const auto view =
    vm::view_matrix(CameraDirection, CameraUp) * vm::translation_matrix(CameraPosition);
  auto transformation = render::Transformation{projection, view};
  renderModels(layout, y, height, transformation);
}

void ModelBrowserView::ensureFolderIconTexture()
{
  if (m_folderIconTextureId != 0)
  {
    return;
  }

  if (m_folderIconImage.isNull())
  {
    return;
  }

  glAssert(glGenTextures(1, &m_folderIconTextureId));
  glAssert(glBindTexture(GL_TEXTURE_2D, m_folderIconTextureId));
  glAssert(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
  glAssert(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
  glAssert(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
  glAssert(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));

  glAssert(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
  glAssert(glTexImage2D(
    GL_TEXTURE_2D,
    0,
    GL_RGBA,
    m_folderIconImage.width(),
    m_folderIconImage.height(),
    0,
    GL_RGBA,
    GL_UNSIGNED_BYTE,
    m_folderIconImage.constBits()));

  glAssert(glBindTexture(GL_TEXTURE_2D, 0));
}

void ModelBrowserView::destroyFolderIconTexture()
{
  if (m_folderIconTextureId == 0)
  {
    return;
  }

  if (context() && context()->isValid())
  {
    makeCurrent();
    glAssert(glDeleteTextures(1, &m_folderIconTextureId));
    doneCurrent();
  }

  m_folderIconTextureId = 0;
}

bool ModelBrowserView::shouldRenderFocusIndicator() const
{
  return false;
}

const Color& ModelBrowserView::getBackgroundColor()
{
  return pref(Preferences::BrowserBackgroundColor);
}

void ModelBrowserView::doLeftClick(Layout& layout, const float x, const float y)
{
  if (const auto* cell = layout.cellAt(x, y))
  {
    const auto& item = cellData(*cell);
    m_selectedType = item.type;
    m_selectedPath = item.path;
    m_hasSelection = true;
    update();
  }
}

void ModelBrowserView::doDoubleClick(Layout& layout, const float x, const float y)
{
  if (const auto* cell = layout.cellAt(x, y))
  {
    const auto& item = cellData(*cell);
    if (item.type == BrowserCellType::Folder)
    {
      emit folderActivated(QString::fromStdString(item.path.generic_string()));
    }
  }
}

void ModelBrowserView::renderHoveredCellBounds(
  Layout& layout, const float y, const float height, const BrowserCellType type)
{
  if (!m_hasHover)
  {
    return;
  }

  if (m_hasSelection && m_selectedType == m_hoverType && m_selectedPath == m_hoverPath)
  {
    return;
  }

  using BoundsVertex = render::GLVertexTypes::P2C4::Vertex;
  auto vertices = std::vector<BoundsVertex>{};

  const auto rgb = pref(Preferences::BrowserTextColor).to<RgbF>();
  const auto color = RgbaF{rgb, 0.10f}.toVec();

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
            const auto& item = cellData(cell);
            if (item.type != type || item.type != m_hoverType || item.path != m_hoverPath)
            {
              continue;
            }

            const auto& bounds = cell.itemBounds();
            vertices.emplace_back(
              vm::vec2f{bounds.left() - 2.0f, height - (bounds.top() - 2.0f - y)}, color);
            vertices.emplace_back(
              vm::vec2f{bounds.left() - 2.0f, height - (bounds.bottom() + 2.0f - y)}, color);
            vertices.emplace_back(
              vm::vec2f{bounds.right() + 2.0f, height - (bounds.bottom() + 2.0f - y)}, color);
            vertices.emplace_back(
              vm::vec2f{bounds.right() + 2.0f, height - (bounds.top() - 2.0f - y)}, color);
          }
        }
      }
    }
  }

  if (vertices.empty())
  {
    return;
  }

  auto vertexArray = render::VertexArray::move(std::move(vertices));
  auto shader =
    render::ActiveShader{shaderManager(), render::Shaders::MaterialBrowserBorderShader};

  vertexArray.prepare(vboManager());
  vertexArray.render(render::PrimType::Quads);
}

void ModelBrowserView::renderSelectedCellBounds(
  Layout& layout, const float y, const float height, const BrowserCellType type)
{
  if (!m_hasSelection)
  {
    return;
  }

  using BoundsVertex = render::GLVertexTypes::P2C4::Vertex;
  auto vertices = std::vector<BoundsVertex>{};

  const auto rgb = pref(Preferences::BrowserTextColor).to<RgbF>();
  const auto color = RgbaF{rgb, 0.18f}.toVec();

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
            const auto& item = cellData(cell);
            if (item.type != type || item.type != m_selectedType || item.path != m_selectedPath)
            {
              continue;
            }

            const auto& bounds = cell.itemBounds();
            vertices.emplace_back(
              vm::vec2f{bounds.left() - 2.0f, height - (bounds.top() - 2.0f - y)}, color);
            vertices.emplace_back(
              vm::vec2f{bounds.left() - 2.0f, height - (bounds.bottom() + 2.0f - y)}, color);
            vertices.emplace_back(
              vm::vec2f{bounds.right() + 2.0f, height - (bounds.bottom() + 2.0f - y)}, color);
            vertices.emplace_back(
              vm::vec2f{bounds.right() + 2.0f, height - (bounds.top() - 2.0f - y)}, color);
          }
        }
      }
    }
  }

  if (vertices.empty())
  {
    return;
  }

  auto vertexArray = render::VertexArray::move(std::move(vertices));
  auto shader =
    render::ActiveShader{shaderManager(), render::Shaders::MaterialBrowserBorderShader};

  vertexArray.prepare(vboManager());
  vertexArray.render(render::PrimType::Quads);
}

void ModelBrowserView::renderFolders(Layout& layout, const float y, const float height)
{
  using Vertex = render::GLVertexTypes::P2::Vertex;
  auto vertices = std::vector<Vertex>{};

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
            const auto& item = cellData(cell);
            if (item.type != BrowserCellType::Folder)
            {
              continue;
            }

            const auto& bounds = cell.itemBounds();
            vertices.emplace_back(vm::vec2f{bounds.left(), height - (bounds.top() - y)});
            vertices.emplace_back(vm::vec2f{bounds.left(), height - (bounds.bottom() - y)});
            vertices.emplace_back(vm::vec2f{bounds.right(), height - (bounds.bottom() - y)});
            vertices.emplace_back(vm::vec2f{bounds.right(), height - (bounds.top() - y)});
          }
        }
      }
    }
  }

  if (vertices.empty())
  {
    return;
  }

  auto shader =
    render::ActiveShader{shaderManager(), render::Shaders::VaryingPUniformCShader};
  shader.set(
    "Color",
    RgbaF{pref(Preferences::BrowserGroupBackgroundColor).to<RgbF>(), 0.35f});

  auto vertexArray = render::VertexArray::move(std::move(vertices));
  vertexArray.prepare(vboManager());
  vertexArray.render(render::PrimType::Quads);

  renderHoveredCellBounds(layout, y, height, BrowserCellType::Folder);
  renderSelectedCellBounds(layout, y, height, BrowserCellType::Folder);

  ensureFolderIconTexture();
  if (m_folderIconTextureId == 0)
  {
    return;
  }

  using IconVertex = render::GLVertexTypes::P2UV2::Vertex;
  auto iconVertices = std::vector<IconVertex>{};

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
            const auto& item = cellData(cell);
            if (item.type != BrowserCellType::Folder)
            {
              continue;
            }

            const auto& bounds = cell.itemBounds();
            const auto iconSize = std::min(bounds.width, bounds.height) * 0.6f;
            const auto left = bounds.left() + (bounds.width - iconSize) / 2.0f;
            const auto top = bounds.top() + (bounds.height - iconSize) / 2.0f;
            const auto right = left + iconSize;
            const auto bottom = top + iconSize;

            iconVertices.emplace_back(
              vm::vec2f{left, height - (top - y)}, vm::vec2f{0, 0});
            iconVertices.emplace_back(
              vm::vec2f{left, height - (bottom - y)}, vm::vec2f{0, 1});
            iconVertices.emplace_back(
              vm::vec2f{right, height - (bottom - y)}, vm::vec2f{1, 1});
            iconVertices.emplace_back(
              vm::vec2f{right, height - (top - y)}, vm::vec2f{1, 0});
          }
        }
      }
    }
  }

  if (iconVertices.empty())
  {
    return;
  }

  auto iconShader =
    render::ActiveShader{shaderManager(), render::Shaders::MaterialBrowserShader};
  iconShader.set("ApplyTinting", false);
  iconShader.set("Material", 0);
  iconShader.set("Brightness", 0.8f);

  glAssert(glActiveTexture(GL_TEXTURE0));
  glAssert(glBindTexture(GL_TEXTURE_2D, m_folderIconTextureId));

  auto iconVertexArray = render::VertexArray::move(std::move(iconVertices));
  iconVertexArray.prepare(vboManager());
  iconVertexArray.render(render::PrimType::Quads);

  glAssert(glBindTexture(GL_TEXTURE_2D, 0));
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
            const auto& item = cellData(cell);
            if (item.type != BrowserCellType::Model)
            {
              continue;
            }
            const auto& modelPath = item.path;
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

bool ModelBrowserView::dndEnabled()
{
  return true;
}

QString ModelBrowserView::dndData(const Cell& cell)
{
  static const auto prefix = QString{"model:"};
  const auto& item = cellData(cell);
  if (item.type != BrowserCellType::Model)
  {
    return "";
  }
  return prefix + io::pathAsGenericQString(item.path);
}

QString ModelBrowserView::tooltip(const Cell& cell)
{
  const auto& item = cellData(cell);
  if (item.type == BrowserCellType::Folder)
  {
    const auto folderAbs = item.path.empty() ? m_rootFolderPath : (m_rootFolderPath / item.path);
    return QString::fromStdString(folderAbs.generic_string());
  }
  return QString::fromStdString(item.path.generic_string());
}

const BrowserCellData& ModelBrowserView::cellData(const Cell& cell) const
{
  return cell.itemAs<BrowserCellData>();
}

} // namespace tb::ui
