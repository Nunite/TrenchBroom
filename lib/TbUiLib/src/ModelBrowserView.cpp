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

#include "ui/ModelBrowserView.h"

#include <QContextMenuEvent>
#include <QEvent>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "fs/DiskIO.h"
#include "fs/FileSystem.h"
#include "fs/PathInfo.h"
#include "gl/ActiveShader.h"
#include "gl/FontDescriptor.h"
#include "gl/FontManager.h"
#include "gl/GlInterface.h"
#include "gl/GlUtils.h"
#include "gl/MaterialIndexRangeRenderer.h"
#include "gl/MaterialRenderFunc.h"
#include "gl/PrimType.h"
#include "gl/Shaders.h"
#include "gl/TextureFont.h"
#include "gl/VertexArray.h"
#include "gl/VertexType.h"
#include "mdl/EntityModelManager.h"
#include "mdl/GameFileSystem.h"
#include "mdl/GoldSrcMdlScaler.h"
#include "mdl/Map.h"
#include "render/Transformation.h"
#include "ui/AppController.h"
#include "ui/ImageUtils.h"
#include "ui/QPathUtils.h"

#include "kd/path_utils.h"
#include "kd/ranges/repeat_view.h"
#include "kd/ranges/stride_view.h"
#include "kd/ranges/zip_view.h"
#include "kd/vector_utils.h"

#include "vm/mat.h"
#include "vm/mat_ext.h"
#include "vm/quat.h"
#include "vm/vec.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <variant>
#include <vector>

namespace tb::ui
{
namespace
{

std::optional<RgbaF> assetPlaceholderColor(const BrowserCellType type)
{
  switch (type)
  {
  case BrowserCellType::Sprite:
    return RgbaF{0.78f, 0.12f, 0.16f, 0.22f};
  case BrowserCellType::Sound:
    return RgbaF{0.66f, 0.54f, 0.95f, 0.35f};
  case BrowserCellType::Folder:
  case BrowserCellType::Model:
    return std::nullopt;
  }

  return std::nullopt;
}

} // namespace

std::vector<ModelBrowserEntry> modelBrowserEntries(
  const std::filesystem::path& rootFolderPath,
  const std::vector<BrowserAsset>& assets,
  const std::filesystem::path& currentFolderPath,
  const QString& searchText)
{
  const auto trimmedSearchText = searchText.trimmed();
  const auto hasSearch = !trimmedSearchText.isEmpty();
  const auto matches = [&](const std::filesystem::path& path) {
    return !hasSearch
           || pathAsGenericQString(path).contains(trimmedSearchText, Qt::CaseInsensitive);
  };

  auto normalizedCurrentFolderPath = currentFolderPath.lexically_normal();
  if (normalizedCurrentFolderPath == std::filesystem::path{"."})
  {
    normalizedCurrentFolderPath.clear();
  }

  const auto currentFolderAbs = normalizedCurrentFolderPath.empty()
                                  ? rootFolderPath
                                  : (rootFolderPath / normalizedCurrentFolderPath);

  auto folderChildren = std::vector<std::filesystem::path>{};
  auto assetChildren = std::vector<BrowserAsset>{};

  for (const auto& asset : assets)
  {
    const auto& assetPath = asset.path;
    if (assetPath.empty())
    {
      continue;
    }

    const auto folderPath = assetPath.parent_path();
    if (!currentFolderAbs.empty() && !kdl::path_has_prefix(folderPath, currentFolderAbs))
    {
      continue;
    }

    const auto relFromCurrent = currentFolderAbs.empty()
                                  ? folderPath
                                  : folderPath.lexically_relative(currentFolderAbs);

    const auto assetNameMatches = matches(assetPath.filename());

    if (relFromCurrent.empty() || relFromCurrent == std::filesystem::path{"."})
    {
      if (!hasSearch || assetNameMatches)
      {
        assetChildren.push_back(asset);
      }
      continue;
    }

    const auto first = *relFromCurrent.begin();
    const auto firstRelPath = (normalizedCurrentFolderPath / first).lexically_normal();

    if (hasSearch)
    {
      if (assetNameMatches)
      {
        assetChildren.push_back(asset);
        folderChildren.push_back(firstRelPath);
        continue;
      }

      if (matches(first))
      {
        folderChildren.push_back(firstRelPath);
      }
      continue;
    }

    folderChildren.push_back(firstRelPath);
  }

  std::ranges::sort(folderChildren, [](const auto& a, const auto& b) {
    return a.generic_string() < b.generic_string();
  });
  folderChildren.erase(
    std::unique(std::begin(folderChildren), std::end(folderChildren)),
    std::end(folderChildren));

  std::ranges::sort(assetChildren, [&](const auto& a, const auto& b) {
    if (hasSearch)
    {
      return a.path.generic_string() < b.path.generic_string();
    }
    if (a.type != b.type)
    {
      return a.type < b.type;
    }
    return a.path.filename().generic_string() < b.path.filename().generic_string();
  });

  auto entries = std::vector<ModelBrowserEntry>{};
  if (!normalizedCurrentFolderPath.empty())
  {
    entries.push_back(
      {BrowserCellData{
         BrowserCellType::Folder, normalizedCurrentFolderPath.parent_path()},
       ".."});
  }

  for (const auto& folderRelPath : folderChildren)
  {
    entries.push_back(
      {BrowserCellData{BrowserCellType::Folder, folderRelPath},
       folderRelPath.filename().generic_string()});
  }

  for (const auto& asset : assetChildren)
  {
    const auto& assetPath = asset.path;
    const auto titlePath =
      hasSearch
        ? (currentFolderAbs.empty() ? assetPath
                                    : assetPath.lexically_relative(currentFolderAbs))
        : assetPath.filename();
    entries.push_back(
      {BrowserCellData{asset.type, assetPath}, titlePath.generic_string()});
  }

  return entries;
}

ModelBrowserView::ModelBrowserView(
  AppController& appController, QScrollBar* scrollBar, mdl::Map& map)
  : CellView{appController, scrollBar}
  , m_map{map}
  , m_scrollBar{scrollBar}
{
  setMouseTracking(true);

  const auto hRotation = vm::quatf{vm::vec3f{0, 0, 1}, vm::to_radians(-30.0f)};
  const auto vRotation = vm::quatf{vm::vec3f{0, 1, 0}, vm::to_radians(20.0f)};
  m_rotation = vRotation * hRotation;

  if (scrollBar)
  {
    connect(
      scrollBar, &QAbstractSlider::valueChanged, this, [this](const int) { update(); });
  }

  const auto pixmap = loadSVGPixmap(std::filesystem::path{"Map_folder.svg"});
  if (!pixmap.isNull())
  {
    m_folderIconImage = pixmap.toImage().convertToFormat(QImage::Format_RGBA8888);
  }
}

ModelBrowserView::~ModelBrowserView()
{
  destroyFolderIconTexture();
  clear();
}

void ModelBrowserView::setAssets(
  std::filesystem::path rootFolderPath, std::vector<BrowserAsset> assets)
{
  m_rootFolderPath = std::move(rootFolderPath);
  m_assets = std::move(assets);
  m_spritePreviewCache.clear();
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

void ModelBrowserView::setSearchText(QString searchText)
{
  searchText = searchText.trimmed();
  if (m_searchText == searchText)
  {
    return;
  }

  m_searchText = std::move(searchText);
  invalidate();
  update();
}

void ModelBrowserView::resourcesWereProcessed(const std::vector<mdl::ResourceId>&)
{
  m_spritePreviewCache.clear();
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
  const auto& fontPath = pref(Preferences::RendererFontPath);
  const auto fontSize = pref(Preferences::BrowserFontSize);
  const auto font = gl::FontDescriptor{fontPath, size_t(fontSize)};
  const auto maxCellWidth = layout.maxCellWidth();
  for (const auto& entry :
       modelBrowserEntries(m_rootFolderPath, m_assets, m_currentFolderPath, m_searchText))
  {
    const auto titleHeight = fontManager().font(font).measure(entry.title).y();

    layout.addItem(
      entry.cellData, entry.title, 93.0f, 93.0f, maxCellWidth, titleHeight + 4.0f);
  }
}

void ModelBrowserView::doClear() {}

void ModelBrowserView::doRender(
  gl::Gl& gl, Layout& layout, const float y, const float height)
{
  const auto viewLeft = float(0);
  const auto viewTop = float(size().height());
  const auto viewRight = float(size().width());
  const auto viewBottom = float(0);

  auto uiTransformation = render::Transformation{
    gl,
    vm::ortho_matrix(-1.0f, 1.0f, viewLeft, viewTop, viewRight, viewBottom),
    vm::view_matrix(vm::vec3f{0, 0, -1}, vm::vec3f{0, 1, 0})
      * vm::translation_matrix(vm::vec3f{0.0f, 0.0f, 0.1f})};
  renderHoveredCellBounds(gl, layout, y, height, BrowserCellType::Model);
  renderSelectedCellBounds(gl, layout, y, height, BrowserCellType::Model);
  renderHoveredCellBounds(gl, layout, y, height, BrowserCellType::Sprite);
  renderSelectedCellBounds(gl, layout, y, height, BrowserCellType::Sprite);
  renderHoveredCellBounds(gl, layout, y, height, BrowserCellType::Sound);
  renderSelectedCellBounds(gl, layout, y, height, BrowserCellType::Sound);
  renderFolders(gl, layout, y, height);
  renderAssetPlaceholders(gl, layout, y, height);
  renderSpritePreviews(gl, layout, y, height);

  const auto projection =
    vm::ortho_matrix(-1024.0f, 1024.0f, viewLeft, viewTop, viewRight, viewBottom);
  const auto view =
    vm::view_matrix(CameraDirection, CameraUp) * vm::translation_matrix(CameraPosition);
  auto transformation = render::Transformation{gl, projection, view};
  renderModels(gl, layout, y, height, transformation);
}

void ModelBrowserView::ensureFolderIconTexture(gl::Gl& gl)
{
  if (m_folderIconTextureId != 0)
  {
    return;
  }

  if (m_folderIconImage.isNull())
  {
    return;
  }

  gl.genTextures(1, &m_folderIconTextureId);
  gl.bindTexture(GL_TEXTURE_2D, m_folderIconTextureId);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  gl.pixelStorei(GL_UNPACK_ALIGNMENT, 1);
  gl.texImage2D(
    GL_TEXTURE_2D,
    0,
    GL_RGBA,
    m_folderIconImage.width(),
    m_folderIconImage.height(),
    0,
    GL_RGBA,
    GL_UNSIGNED_BYTE,
    m_folderIconImage.constBits());

  gl.bindTexture(GL_TEXTURE_2D, 0);
}

void ModelBrowserView::destroyFolderIconTexture()
{
  if (m_folderIconTextureId == 0)
  {
    return;
  }

  m_folderIconTextureId = 0;
}

const std::optional<GoldSrcSpritePreview>& ModelBrowserView::spritePreview(
  const std::filesystem::path& path)
{
  auto& entry = m_spritePreviewCache[path];
  if (entry.loaded)
  {
    return entry.preview;
  }

  entry.loaded = true;

  auto file = std::shared_ptr<fs::File>{};
  if (path.is_absolute())
  {
    auto fileResult = fs::Disk::openFile(path);
    if (fileResult.is_error())
    {
      return entry.preview;
    }
    file = fileResult.value();
  }
  else
  {
    auto fileResult = m_map.gameFileSystem().openFile(path);
    if (fileResult.is_error())
    {
      return entry.preview;
    }
    file = fileResult.value();
  }

  auto reader = file->reader().buffer();
  const auto bytes = reader.stringView();
  entry.preview = loadGoldSrcSpritePreview(
    std::span{reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
  return entry.preview;
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

void ModelBrowserView::doContextMenu(
  Layout& layout, const float x, const float y, QContextMenuEvent* event)
{
  const auto* cell = layout.cellAt(x, y);
  if (!cell)
  {
    return;
  }

  const auto& item = cellData(*cell);
  if (item.type != BrowserCellType::Model)
  {
    return;
  }

  if (kdl::path_to_lower(item.path.extension()) != ".mdl")
  {
    return;
  }

  auto menu = QMenu{this};
  menu.addAction(tr("Scale MDL..."), this, [this, modelPath = item.path]() {
    auto ok = false;
    const auto scale = QInputDialog::getDouble(
      this, tr("Scale MDL"), tr("Scale factor"), 1.0, 0.001, 1000.0, 3, &ok);
    if (!ok)
    {
      return;
    }

    if (scale == 1.0)
    {
      return;
    }

    auto absPath = std::filesystem::path{};
    if (modelPath.is_absolute())
    {
      absPath = modelPath;
    }
    else
    {
      const auto absPathResult = m_map.gameFileSystem().makeAbsolute(modelPath);
      if (!absPathResult.is_error())
      {
        absPath = absPathResult.value();
      }
    }

    if (absPath.empty() || fs::Disk::pathInfo(absPath) != fs::PathInfo::File)
    {
      QMessageBox::warning(
        this, tr("Error"), tr("Cannot locate a writable MDL file path."));
      return;
    }

    const auto result = io::scaleGoldSrcMdlFile(absPath, float(scale));
    if (result.is_error())
    {
      const auto err = result.error();
      const auto& e = std::get<tb::Error>(err);
      QMessageBox::warning(this, tr("Error"), QString::fromStdString(e.msg));
      return;
    }

    m_map.reloadEntityModels(modelPath);
    invalidate();
    update();
  });

  menu.exec(event->globalPos());
}

void ModelBrowserView::renderAssetPlaceholders(
  gl::Gl& gl, Layout& layout, const float y, const float height)
{
  using Vertex = gl::VertexTypes::P2C4::Vertex;
  using TextVertex = gl::VertexTypes::P2UV2C4::Vertex;
  auto backgroundVertices = std::vector<Vertex>{};
  auto iconVertices = std::vector<Vertex>{};
  auto errorTextVertices = std::vector<TextVertex>{};
  auto errorFont = gl::FontDescriptor{
    pref(Preferences::RendererFontPath),
    size_t(std::max(8, pref(Preferences::BrowserFontSize) - 1))};

  const auto addQuad = [&](
                         auto& vertices,
                         const float left,
                         const float top,
                         const float right,
                         const float bottom,
                         const vm::vec4f& color) {
    vertices.emplace_back(vm::vec2f{left, height - (top - y)}, color);
    vertices.emplace_back(vm::vec2f{left, height - (bottom - y)}, color);
    vertices.emplace_back(vm::vec2f{right, height - (bottom - y)}, color);
    vertices.emplace_back(vm::vec2f{right, height - (top - y)}, color);
  };

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
            if (
              item.type == BrowserCellType::Sprite && spritePreview(item.path)
              && !spritePreview(item.path)->rgba.empty())
            {
              continue;
            }

            const auto color = assetPlaceholderColor(item.type);
            if (!color)
            {
              continue;
            }

            const auto& bounds = cell.itemBounds();
            const auto colorVec = color->toVec();
            addQuad(
              backgroundVertices,
              bounds.left() + 6.0f,
              bounds.top() + 6.0f,
              bounds.right() - 6.0f,
              bounds.bottom() - 6.0f,
              colorVec);

            if (item.type == BrowserCellType::Sound)
            {
              const auto iconColor = RgbaF{0.90f, 0.86f, 1.0f, 0.92f}.toVec();
              const auto iconSize = std::min(bounds.width, bounds.height) * 0.48f;
              const auto left = bounds.left() + (bounds.width - iconSize) / 2.0f;
              const auto top = bounds.top() + (bounds.height - iconSize) / 2.0f;
              const auto unit = iconSize / 16.0f;
              const auto cy = top + iconSize / 2.0f;

              addQuad(
                iconVertices,
                left + 1.0f * unit,
                cy - 3.0f * unit,
                left + 4.0f * unit,
                cy + 3.0f * unit,
                iconColor);
              addQuad(
                iconVertices,
                left + 4.0f * unit,
                cy - 5.0f * unit,
                left + 7.0f * unit,
                cy + 5.0f * unit,
                iconColor);
              addQuad(
                iconVertices,
                left + 9.0f * unit,
                cy - 3.0f * unit,
                left + 10.4f * unit,
                cy + 3.0f * unit,
                iconColor);
              addQuad(
                iconVertices,
                left + 11.6f * unit,
                cy - 4.6f * unit,
                left + 13.0f * unit,
                cy + 4.6f * unit,
                iconColor);
              addQuad(
                iconVertices,
                left + 14.2f * unit,
                cy - 6.2f * unit,
                left + 15.6f * unit,
                cy + 6.2f * unit,
                iconColor);
            }
            else if (item.type == BrowserCellType::Sprite)
            {
              const auto errorText = std::string{"ERROR"};
              auto& font = fontManager().font(errorFont);
              const auto textSize = font.measure(errorText);
              const auto textOffset = vm::vec2f{
                bounds.left() + std::max((bounds.width - textSize.x()) / 2.0f, 0.0f),
                height - (bounds.top() - y) - (bounds.height - textSize.y()) / 2.0f};
              const auto textColor = RgbaF{1.0f, 0.84f, 0.84f, 0.95f}.toVec();
              const auto textQuads = font.quads(errorText, false, textOffset);
              const auto textVertices = TextVertex::toList(kdl::views::zip(
                textQuads | kdl::views::stride(2),
                textQuads | std::views::drop(1) | kdl::views::stride(2),
                kdl::views::repeat(textColor)));
              kdl::vec_append(errorTextVertices, textVertices);
            }
          }
        }
      }
    }
  }

  if (backgroundVertices.empty())
  {
    return;
  }

  auto shader =
    gl::ActiveShader{gl, shaderManager(), gl::Shaders::MaterialBrowserBorderShader};

  auto vertexArray = gl::VertexArray::move(std::move(backgroundVertices));
  vertexArray.prepare(gl, vboManager());
  if (vertexArray.setup(gl, shader.program()))
  {
    vertexArray.render(gl, gl::PrimType::Quads);
    vertexArray.cleanup(gl, shader.program());
  }

  auto iconVertexArray = gl::VertexArray::move(std::move(iconVertices));
  iconVertexArray.prepare(gl, vboManager());
  if (iconVertexArray.setup(gl, shader.program()))
  {
    iconVertexArray.render(gl, gl::PrimType::Quads);
    iconVertexArray.cleanup(gl, shader.program());
  }

  if (!errorTextVertices.empty())
  {
    auto textVertexArray = gl::VertexArray::ref(errorTextVertices);
    textVertexArray.prepare(gl, vboManager());

    auto textShader =
      gl::ActiveShader{gl, shaderManager(), gl::Shaders::ColoredTextShader};
    textShader.set("Texture", 0);
    if (textVertexArray.setup(gl, textShader.program()))
    {
      fontManager().font(errorFont).activate(gl);
      textVertexArray.render(gl, gl::PrimType::Quads);
      textVertexArray.cleanup(gl, textShader.program());
      fontManager().font(errorFont).deactivate(gl);
    }
  }
}

void ModelBrowserView::renderSpritePreviews(
  gl::Gl& gl, Layout& layout, const float y, const float height)
{
  using Vertex = gl::VertexTypes::P2UV2::Vertex;

  auto shader = gl::ActiveShader{gl, shaderManager(), gl::Shaders::MaterialBrowserShader};
  shader.set("ApplyTinting", false);
  shader.set("Material", 0);
  shader.set("Brightness", 1.0f);

  for (const auto& group : layout.groups())
  {
    if (!group.intersectsY(y, height))
    {
      continue;
    }

    for (const auto& row : group.rows())
    {
      if (!row.intersectsY(y, height))
      {
        continue;
      }

      for (const auto& cell : row.cells())
      {
        const auto& item = cellData(cell);
        if (item.type != BrowserCellType::Sprite)
        {
          continue;
        }

        const auto& preview = spritePreview(item.path);
        if (!preview || preview->rgba.empty())
        {
          continue;
        }

        const auto& bounds = cell.itemBounds();
        const auto maxWidth = bounds.width - 16.0f;
        const auto maxHeight = bounds.height - 16.0f;
        if (maxWidth <= 0.0f || maxHeight <= 0.0f)
        {
          continue;
        }

        const auto previewWidth = float(preview->width);
        const auto previewHeight = float(preview->height);
        const auto scale = std::min(maxWidth / previewWidth, maxHeight / previewHeight);
        const auto imageWidth = previewWidth * scale;
        const auto imageHeight = previewHeight * scale;
        const auto left = bounds.left() + (bounds.width - imageWidth) / 2.0f;
        const auto top = bounds.top() + (bounds.height - imageHeight) / 2.0f;
        const auto right = left + imageWidth;
        const auto bottom = top + imageHeight;

        auto textureId = GLuint{0};
        gl.genTextures(1, &textureId);
        gl.bindTexture(GL_TEXTURE_2D, textureId);
        gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl.pixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl.texImage2D(
          GL_TEXTURE_2D,
          0,
          GL_RGBA,
          GLsizei(preview->width),
          GLsizei(preview->height),
          0,
          GL_RGBA,
          GL_UNSIGNED_BYTE,
          preview->rgba.data());

        auto vertices = std::vector<Vertex>{
          Vertex{vm::vec2f{left, height - (top - y)}, vm::vec2f{0, 0}},
          Vertex{vm::vec2f{left, height - (bottom - y)}, vm::vec2f{0, 1}},
          Vertex{vm::vec2f{right, height - (bottom - y)}, vm::vec2f{1, 1}},
          Vertex{vm::vec2f{right, height - (top - y)}, vm::vec2f{1, 0}},
        };

        auto vertexArray = gl::VertexArray::move(std::move(vertices));
        vertexArray.prepare(gl, vboManager());
        if (vertexArray.setup(gl, shader.program()))
        {
          vertexArray.render(gl, gl::PrimType::Quads);
          vertexArray.cleanup(gl, shader.program());
        }

        gl.bindTexture(GL_TEXTURE_2D, 0);
        gl.deleteTextures(1, &textureId);
      }
    }
  }
}

void ModelBrowserView::renderHoveredCellBounds(
  gl::Gl& gl,
  Layout& layout,
  const float y,
  const float height,
  const BrowserCellType type)
{
  if (!m_hasHover)
  {
    return;
  }

  if (m_hasSelection && m_selectedType == m_hoverType && m_selectedPath == m_hoverPath)
  {
    return;
  }

  using BoundsVertex = gl::VertexTypes::P2C4::Vertex;
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
              vm::vec2f{bounds.left() - 2.0f, height - (bounds.bottom() + 2.0f - y)},
              color);
            vertices.emplace_back(
              vm::vec2f{bounds.right() + 2.0f, height - (bounds.bottom() + 2.0f - y)},
              color);
            vertices.emplace_back(
              vm::vec2f{bounds.right() + 2.0f, height - (bounds.top() - 2.0f - y)},
              color);
          }
        }
      }
    }
  }

  if (vertices.empty())
  {
    return;
  }

  auto vertexArray = gl::VertexArray::move(std::move(vertices));
  auto shader =
    gl::ActiveShader{gl, shaderManager(), gl::Shaders::MaterialBrowserBorderShader};

  vertexArray.prepare(gl, vboManager());
  if (vertexArray.setup(gl, shader.program()))
  {
    vertexArray.render(gl, gl::PrimType::Quads);
    vertexArray.cleanup(gl, shader.program());
  }
}

void ModelBrowserView::renderSelectedCellBounds(
  gl::Gl& gl,
  Layout& layout,
  const float y,
  const float height,
  const BrowserCellType type)
{
  if (!m_hasSelection)
  {
    return;
  }

  using BoundsVertex = gl::VertexTypes::P2C4::Vertex;
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
            if (
              item.type != type || item.type != m_selectedType
              || item.path != m_selectedPath)
            {
              continue;
            }

            const auto& bounds = cell.itemBounds();
            vertices.emplace_back(
              vm::vec2f{bounds.left() - 2.0f, height - (bounds.top() - 2.0f - y)}, color);
            vertices.emplace_back(
              vm::vec2f{bounds.left() - 2.0f, height - (bounds.bottom() + 2.0f - y)},
              color);
            vertices.emplace_back(
              vm::vec2f{bounds.right() + 2.0f, height - (bounds.bottom() + 2.0f - y)},
              color);
            vertices.emplace_back(
              vm::vec2f{bounds.right() + 2.0f, height - (bounds.top() - 2.0f - y)},
              color);
          }
        }
      }
    }
  }

  if (vertices.empty())
  {
    return;
  }

  auto vertexArray = gl::VertexArray::move(std::move(vertices));
  auto shader =
    gl::ActiveShader{gl, shaderManager(), gl::Shaders::MaterialBrowserBorderShader};

  vertexArray.prepare(gl, vboManager());
  if (vertexArray.setup(gl, shader.program()))
  {
    vertexArray.render(gl, gl::PrimType::Quads);
    vertexArray.cleanup(gl, shader.program());
  }
}

void ModelBrowserView::renderFolders(
  gl::Gl& gl, Layout& layout, const float y, const float height)
{
  using Vertex = gl::VertexTypes::P2::Vertex;
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
            vertices.emplace_back(
              vm::vec2f{bounds.left(), height - (bounds.bottom() - y)});
            vertices.emplace_back(
              vm::vec2f{bounds.right(), height - (bounds.bottom() - y)});
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
    gl::ActiveShader{gl, shaderManager(), gl::Shaders::VaryingPUniformCShader};
  shader.set(
    "Color", RgbaF{pref(Preferences::BrowserGroupBackgroundColor).to<RgbF>(), 0.35f});

  auto vertexArray = gl::VertexArray::move(std::move(vertices));
  vertexArray.prepare(gl, vboManager());
  if (vertexArray.setup(gl, shader.program()))
  {
    vertexArray.render(gl, gl::PrimType::Quads);
    vertexArray.cleanup(gl, shader.program());
  }

  renderHoveredCellBounds(gl, layout, y, height, BrowserCellType::Folder);
  renderSelectedCellBounds(gl, layout, y, height, BrowserCellType::Folder);

  ensureFolderIconTexture(gl);
  if (m_folderIconTextureId == 0)
  {
    return;
  }

  using IconVertex = gl::VertexTypes::P2UV2::Vertex;
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
    gl::ActiveShader{gl, shaderManager(), gl::Shaders::MaterialBrowserShader};
  iconShader.set("ApplyTinting", false);
  iconShader.set("Material", 0);
  iconShader.set("Brightness", 0.8f);

  gl.activeTexture(GL_TEXTURE0);
  gl.bindTexture(GL_TEXTURE_2D, m_folderIconTextureId);

  auto iconVertexArray = gl::VertexArray::move(std::move(iconVertices));
  iconVertexArray.prepare(gl, vboManager());
  if (iconVertexArray.setup(gl, iconShader.program()))
  {
    iconVertexArray.render(gl, gl::PrimType::Quads);
    iconVertexArray.cleanup(gl, iconShader.program());
  }

  gl.bindTexture(GL_TEXTURE_2D, 0);
}

void ModelBrowserView::renderModels(
  gl::Gl& gl,
  Layout& layout,
  const float y,
  const float height,
  render::Transformation& transformation)
{
  gl.frontFace(GL_CW);

  auto& entityModelManager = m_map.entityModelManager();
  entityModelManager.prepare(gl, vboManager());

  auto shader = gl::ActiveShader{gl, shaderManager(), gl::Shaders::EntityModelShader};
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
            modelRenderer->prepare(gl, vboManager());

            shader.set("Orientation", int(mdl::Orientation::Oriented));

            auto bounds = vm::bbox3f{8.0f};
            if (const auto* frame = entityModelManager.frame(spec))
            {
              bounds = frame->bounds();
            }

            const auto center = bounds.center();
            const auto transform = vm::translation_matrix(center)
                                   * vm::rotation_matrix(m_rotation)
                                   * vm::translation_matrix(-center);

            const auto itemTrans = itemTransformation(cell, y, height, bounds, transform);
            shader.set("ModelMatrix", itemTrans);

            const auto multMatrix =
              render::MultiplyModelMatrix{transformation, itemTrans};

            auto renderFunc = gl::DefaultMaterialRenderFunc{
              pref(Preferences::TextureMinFilter), pref(Preferences::TextureMagFilter)};
            modelRenderer->render(gl, shader.program(), renderFunc);
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
  const auto& item = cellData(cell);
  switch (item.type)
  {
  case BrowserCellType::Model:
    return QString{"model:"} + pathAsGenericQString(item.path);
  case BrowserCellType::Sprite:
    return QString{"sprite:"} + pathAsGenericQString(item.path);
  case BrowserCellType::Sound:
    return QString{"sound:"} + pathAsGenericQString(item.path);
  case BrowserCellType::Folder:
    return "";
  }

  return "";
}

QString ModelBrowserView::tooltip(const Cell& cell)
{
  const auto& item = cellData(cell);
  if (item.type == BrowserCellType::Folder)
  {
    const auto folderAbs =
      item.path.empty() ? m_rootFolderPath : (m_rootFolderPath / item.path);
    return QString::fromStdString(folderAbs.generic_string());
  }
  return QString::fromStdString(item.path.generic_string());
}

const BrowserCellData& ModelBrowserView::cellData(const Cell& cell) const
{
  return cell.itemAs<BrowserCellData>();
}

} // namespace tb::ui
