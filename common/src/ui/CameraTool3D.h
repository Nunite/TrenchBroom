/*
 Copyright (C) 2010 Kristian Duske

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

#include "ui/Tool.h"
#include "ui/ToolController.h"

#include <memory>
#include <QWidget>
#include <QPoint>

namespace tb::render
{
class PerspectiveCamera;
}

namespace tb::ui
{
class GestureTracker;

class CameraTool3D : public ToolController, public Tool
{
private:
  render::PerspectiveCamera& m_camera;
  QWidget* m_widget = nullptr;
  bool m_cursorLocked = false;
  QPoint m_center;
  
  // 右键点击跟踪
  qint64 m_rightClickStartTime;
  QPoint m_rightClickStartPos;

public:
  CameraTool3D(render::PerspectiveCamera& camera, QWidget* widget = nullptr);
  void releaseCursorLock();

private:
  Tool& tool() override;
  const Tool& tool() const override;

  void mouseDown(const InputState& inputState) override;
  void mouseScroll(const InputState& inputState) override;
  void mouseUp(const InputState& inputState) override;
  void mouseMove(const InputState& inputState) override;

  std::unique_ptr<GestureTracker> acceptMouseDrag(const InputState& inputState) override;

  bool isRightClickForContextMenu(const QPoint& currentPos) const;
  void showContextMenu();

  bool cancel() override;
};

} // namespace tb::ui
