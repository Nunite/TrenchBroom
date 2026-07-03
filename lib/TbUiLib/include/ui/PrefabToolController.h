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

#include "ui/ToolController.h"

#include "vm/bbox.h"
#include "vm/vec.h"

#include <filesystem>
#include <memory>
#include <string>

namespace tb::ui
{
class PrefabTool;
}

namespace tb::render
{
class ObjectRenderer;
}

namespace tb::ui
{
vm::vec3d prefabCenterPlacementDelta(
  const vm::bbox3d& bounds, const vm::vec3d& targetPoint);

class PrefabToolController : public ToolController
{
private:
  PrefabTool& m_tool;
  mutable std::unique_ptr<render::ObjectRenderer> m_previewRenderer;
  mutable size_t m_previewRendererVersion = 0;

protected:
  explicit PrefabToolController(PrefabTool& tool);
  PrefabTool& prefabTool() const;

public:
  ~PrefabToolController() override;

private:
  Tool& tool() override;
  const Tool& tool() const override;

  bool shouldAcceptDrop(
    const InputState& inputState, const std::string& payload) const override;
  std::unique_ptr<DropTracker> acceptDrop(
    const InputState& inputState, const std::string& payload) override;
  void render(
    const InputState& inputState,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch) override;

  virtual std::unique_ptr<DropTracker> createDropTracker(
    std::filesystem::path prefabPath) const = 0;
  void validatePreviewRenderer() const;
};

class PrefabToolController2D : public PrefabToolController
{
public:
  explicit PrefabToolController2D(PrefabTool& tool);

private:
  std::unique_ptr<DropTracker> createDropTracker(
    std::filesystem::path prefabPath) const override;
};

class PrefabToolController3D : public PrefabToolController
{
public:
  explicit PrefabToolController3D(PrefabTool& tool);

private:
  std::unique_ptr<DropTracker> createDropTracker(
    std::filesystem::path prefabPath) const override;
};

} // namespace tb::ui
