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

#include <filesystem>
#include <vector>

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

namespace tb::ui
{

class GLContextManager;

struct ModelCellData
{
  std::filesystem::path modelPath;
};

class ModelBrowserView : public CellView
{
  Q_OBJECT
private:
  static constexpr auto CameraPosition = vm::vec3f{256.0f, 0.0f, 0.0f};
  static constexpr auto CameraDirection = vm::vec3f{-1, 0, 0};
  static constexpr auto CameraUp = vm::vec3f{0, 0, 1};

  mdl::Map& m_map;
  vm::quatf m_rotation;
  std::vector<std::filesystem::path> m_modelPaths;

  NotifierConnection m_notifierConnection;

public:
  ModelBrowserView(
    QScrollBar* scrollBar, GLContextManager& contextManager, mdl::Map& map);
  ~ModelBrowserView() override;

  void setModelPaths(std::vector<std::filesystem::path> modelPaths);

private:
  void resourcesWereProcessed(const std::vector<mdl::ResourceId>& resources);

  void doInitLayout(Layout& layout) override;
  void doReloadLayout(Layout& layout) override;

  void doClear() override;
  void doRender(Layout& layout, float y, float height) override;
  bool shouldRenderFocusIndicator() const override;
  const Color& getBackgroundColor() override;

  void renderModels(
    Layout& layout, float y, float height, render::Transformation& transformation);

  vm::mat4x4f itemTransformation(
    const Cell& cell,
    float y,
    float height,
    const vm::bbox3f& bounds,
    const vm::mat4x4f& transform) const;

  QString tooltip(const Cell& cell) override;

  const ModelCellData& cellData(const Cell& cell) const;
};

} // namespace tb::ui
