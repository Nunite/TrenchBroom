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

#pragma once

#include "NotifierConnection.h"
#include "ui/CellView.h"

#include "vm/bbox.h"
#include "vm/quat.h" // IWYU pragma: keep

#include <QString>
#include <QImage>

#include <filesystem>
#include <vector>

class QEvent;
class QContextMenuEvent;
class QScrollBar;

namespace tb::mdl
{
class Map;
class ResourceId;
} // namespace tb::mdl

namespace tb::render
{
class Transformation;
} // namespace tb::render

namespace tb::gl
{
class Gl;
} // namespace tb::gl

namespace tb::ui
{

class AppController;

enum class BrowserCellType
{
  Folder,
  Model,
};

struct BrowserCellData
{
  BrowserCellType type;
  std::filesystem::path path;
};

class ModelBrowserView : public CellView
{
  Q_OBJECT
private:
  static constexpr auto CameraPosition = vm::vec3f{256.0f, 0.0f, 0.0f};
  static constexpr auto CameraDirection = vm::vec3f{-1, 0, 0};
  static constexpr auto CameraUp = vm::vec3f{0, 0, 1};
private:
  mdl::Map& m_map;
  vm::quatf m_rotation;
  QScrollBar* m_scrollBar = nullptr;
  std::filesystem::path m_rootFolderPath;
  std::vector<std::filesystem::path> m_modelPaths;
  std::filesystem::path m_currentFolderPath;
  QString m_searchText;

  NotifierConnection m_notifierConnection;
  QImage m_folderIconImage;
  GLuint m_folderIconTextureId = 0;
  bool m_hasSelection = false;
  BrowserCellType m_selectedType = BrowserCellType::Folder;
  std::filesystem::path m_selectedPath;
  bool m_hasHover = false;
  BrowserCellType m_hoverType = BrowserCellType::Folder;
  std::filesystem::path m_hoverPath;

public:
  ModelBrowserView(AppController& appController, QScrollBar* scrollBar, mdl::Map& map);
  ~ModelBrowserView() override;

  void setModelPaths(
    std::filesystem::path rootFolderPath, std::vector<std::filesystem::path> modelPaths);
  void setCurrentFolderPath(std::filesystem::path currentFolderPath);
  void setSearchText(QString searchText);

private:
  void leaveEvent(QEvent* event) override;

  void resourcesWereProcessed(const std::vector<mdl::ResourceId>& resources);

  void doInitLayout(Layout& layout) override;
  void doReloadLayout(Layout& layout) override;

  bool dndEnabled() override;
  QString dndData(const Cell& cell) override;

  void doClear() override;
  void doRender(gl::Gl& gl, Layout& layout, float y, float height) override;
  bool shouldRenderFocusIndicator() const override;
  const Color& getBackgroundColor() override;
  void doMouseMove(Layout& layout, float x, float y) override;
  void doLeftClick(Layout& layout, float x, float y) override;
  void doDoubleClick(Layout& layout, float x, float y) override;
  void doContextMenu(Layout& layout, float x, float y, QContextMenuEvent* event) override;

  void ensureFolderIconTexture(gl::Gl& gl);
  void destroyFolderIconTexture();

  void renderHoveredCellBounds(
    gl::Gl& gl, Layout& layout, float y, float height, BrowserCellType type);
  void renderSelectedCellBounds(
    gl::Gl& gl, Layout& layout, float y, float height, BrowserCellType type);
  void renderFolders(gl::Gl& gl, Layout& layout, float y, float height);
  void renderModels(
    gl::Gl& gl, Layout& layout, float y, float height, render::Transformation& transformation);

  vm::mat4x4f itemTransformation(
    const Cell& cell,
    float y,
    float height,
    const vm::bbox3f& bounds,
    const vm::mat4x4f& transform) const;

  QString tooltip(const Cell& cell) override;

  const BrowserCellData& cellData(const Cell& cell) const;

signals:
  void folderActivated(const QString& folderPath);
};

} // namespace tb::ui
