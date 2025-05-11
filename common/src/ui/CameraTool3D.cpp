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

#include "CameraTool3D.h"

#include "PreferenceManager.h"
#include "Preferences.h"
#include "mdl/Hit.h"
#include "mdl/HitFilter.h"
#include "mdl/ModelUtils.h"
#include "mdl/PickResult.h"
#include "render/PerspectiveCamera.h"
#include "ui/GestureTracker.h"
#include "ui/InputState.h"

#include "vm/intersection.h"
#include "vm/plane.h"
#include "vm/scalar.h"
#include "vm/vec.h"

#include <QWindow>

namespace tb::ui
{
namespace
{

bool shouldMove(const InputState& inputState)
{
  return (
    inputState.mouseButtonsPressed(MouseButtons::None)
    && inputState.checkModifierKeys(
      ModifierKeyPressed::No, ModifierKeyPressed::No, ModifierKeyPressed::DontCare));
}

bool shouldLook(const InputState& inputState)
{
  return (
    inputState.mouseButtonsPressed(MouseButtons::Right)
    && inputState.modifierKeysPressed(ModifierKeys::None));
}

bool shouldPan(const InputState& inputState)
{
  return (
    inputState.mouseButtonsPressed(MouseButtons::Middle)
    && (inputState.modifierKeysPressed(ModifierKeys::None) || inputState.modifierKeysPressed(ModifierKeys::Alt)));
}

bool shouldOrbit(const InputState& inputState)
{
  return (
    inputState.mouseButtonsPressed(MouseButtons::Right)
    && inputState.modifierKeysPressed(ModifierKeys::Alt));
}

bool shouldAdjustFlySpeed(const InputState& inputState)
{
  return (
    inputState.mouseButtonsPressed(MouseButtons::Right)
    && inputState.checkModifierKeys(
      ModifierKeyPressed::No, ModifierKeyPressed::No, ModifierKeyPressed::No));
}

float adjustSpeedToZoom(const render::PerspectiveCamera& camera, const float speed)
{
  return speed * vm::min(1.0f, camera.zoomedFov() / camera.fov());
}

float lookSpeedH(const render::PerspectiveCamera& camera)
{
  auto speed = pref(Preferences::CameraLookSpeed) / -50.0f;
  if (pref(Preferences::CameraLookInvertH))
  {
    speed *= -1.0f;
  }
  return adjustSpeedToZoom(camera, speed);
}

float lookSpeedV(const render::PerspectiveCamera& camera)
{
  auto speed = pref(Preferences::CameraLookSpeed) / -50.0f;
  if (pref(Preferences::CameraLookInvertV))
  {
    speed *= -1.0f;
  }
  return adjustSpeedToZoom(camera, speed);
}

float panSpeedH(const render::PerspectiveCamera& camera)
{
  auto speed = pref(Preferences::CameraPanSpeed);
  if (pref(Preferences::CameraPanInvertH))
  {
    speed *= -1.0f;
  }
  return adjustSpeedToZoom(camera, speed);
}

float panSpeedV(const render::PerspectiveCamera& camera)
{
  auto speed = pref(Preferences::CameraPanSpeed);
  if (pref(Preferences::CameraPanInvertV))
  {
    speed *= -1.0f;
  }
  return adjustSpeedToZoom(camera, speed);
}

float moveSpeed(const render::PerspectiveCamera& camera, const bool altMode)
{
  auto speed = pref(Preferences::CameraMoveSpeed) * 20.0f;
  if (altMode && pref(Preferences::CameraAltMoveInvert))
  {
    speed *= -1.0f;
  }
  return adjustSpeedToZoom(camera, speed);
}

class OrbitDragTracker : public GestureTracker
{
private:
  render::PerspectiveCamera& m_camera;
  vm::vec3f m_orbitCenter;
  bool m_firstUpdate = true;

public:
  OrbitDragTracker(render::PerspectiveCamera& camera, const vm::vec3f& orbitCenter)
    : m_camera{camera}
    , m_orbitCenter{orbitCenter}
  {
  }

  void mouseScroll(const InputState& inputState) override
  {
    const auto factor = pref(Preferences::CameraMouseWheelInvert) ? -1.0f : 1.0f;
    const auto scrollDist = inputState.scrollY();

    const auto orbitPlane = vm::plane3f{m_orbitCenter, m_camera.direction()};
    if (const auto hit = vm::intersect_ray_plane(m_camera.viewRay(), orbitPlane))
    {
      const auto maxDistance = vm::max(*hit - 32.0f, 0.0f);
      const auto distance =
        vm::min(factor * scrollDist * moveSpeed(m_camera, false), maxDistance);

      m_camera.moveBy(distance * m_camera.direction());
    }
  }

  bool update(const InputState& inputState) override
  {
    if (m_firstUpdate) {
      m_firstUpdate = false;
      return true; // 跳过首次更新，避免视角突变
    }
    
    const auto hAngle = static_cast<float>(inputState.mouseDX()) * lookSpeedH(m_camera);
    const auto vAngle = static_cast<float>(inputState.mouseDY()) * lookSpeedV(m_camera);
    m_camera.orbit(m_orbitCenter, hAngle, vAngle);
    return true;
  }

  void end(const InputState&) override {}
  void cancel() override {}
};

class LookDragTracker : public GestureTracker
{
private:
  render::PerspectiveCamera& m_camera;
  bool m_firstUpdate = true;
  QWidget* m_widget = nullptr;
  QPoint m_center;
  QPoint m_lastMousePos;

public:
  explicit LookDragTracker(render::PerspectiveCamera& camera, QWidget* widget = nullptr)
    : m_camera{camera}
    , m_widget{widget}
  {
    if (m_widget) {
      m_center = m_widget->rect().center();
      m_lastMousePos = m_center;
    }
  }

  void mouseScroll(const InputState& inputState) override
  {
    if (shouldAdjustFlySpeed(inputState))
    {
      const auto factor = pref(Preferences::CameraMouseWheelInvert) ? -1.0f : 1.0f;
      const auto scrollDist = inputState.scrollY();

      const auto speed = pref(Preferences::CameraFlyMoveSpeed);
      // adjust speed by 5% of the current speed per scroll line
      const auto deltaSpeed = factor * speed * 0.05f * scrollDist;
      const auto newSpeed = vm::clamp(
        speed + deltaSpeed,
        Preferences::MinCameraFlyMoveSpeed,
        Preferences::MaxCameraFlyMoveSpeed);

      // prefs are only changed when releasing RMB
      auto& prefs = PreferenceManager::instance();
      prefs.set(Preferences::CameraFlyMoveSpeed, newSpeed);
    }
  }

  bool update(const InputState& inputState) override
  {
    if (m_firstUpdate) {
      m_firstUpdate = false;
      
      // 初始化鼠标位置并立即重置
      if (m_widget && inputState.mouseButtonsPressed(MouseButtons::Right)) {
        QCursor::setPos(m_widget->mapToGlobal(m_center));
        const_cast<InputState&>(inputState).mouseMove(float(m_center.x()), float(m_center.y()), 0.0f, 0.0f);
      }
      
      return true;
    }
    
    // 获取当前鼠标位置
    QPoint currentPos = m_widget ? m_widget->mapFromGlobal(QCursor::pos()) : QPoint();
    
    // 计算鼠标移动
    float dx = m_widget ? static_cast<float>(currentPos.x() - m_center.x()) : static_cast<float>(inputState.mouseDX());
    float dy = m_widget ? static_cast<float>(currentPos.y() - m_center.y()) : static_cast<float>(inputState.mouseDY());
    
    // 应用旋转
    const auto hAngle = dx * lookSpeedH(m_camera);
    const auto vAngle = dy * lookSpeedV(m_camera);
    m_camera.rotate(hAngle, vAngle);
    
    // 每次更新后都重置鼠标位置到中心点
    if (m_widget && inputState.mouseButtonsPressed(MouseButtons::Right)) {
      QCursor::setPos(m_widget->mapToGlobal(m_center));
    }
    
    return true;
  }

  void end(const InputState&) override {}
  void cancel() override {}
};

class PanDragTracker : public GestureTracker
{
private:
  render::PerspectiveCamera& m_camera;

public:
  explicit PanDragTracker(render::PerspectiveCamera& camera)
    : m_camera{camera}
  {
  }

  bool update(const InputState& inputState) override
  {
    const auto altMove = pref(Preferences::CameraEnableAltMove);
    auto delta = vm::vec3f{};
    if (altMove && inputState.modifierKeysPressed(ModifierKeys::Alt))
    {
      delta = delta
              + static_cast<float>(inputState.mouseDX()) * panSpeedH(m_camera)
                  * m_camera.right();
      delta = delta
              + static_cast<float>(inputState.mouseDY()) * -moveSpeed(m_camera, altMove)
                  * m_camera.direction();
    }
    else
    {
      delta = delta
              + static_cast<float>(inputState.mouseDX()) * panSpeedH(m_camera)
                  * m_camera.right();
      delta =
        delta
        + static_cast<float>(inputState.mouseDY()) * panSpeedV(m_camera) * m_camera.up();
    }
    m_camera.moveBy(delta);
    return true;
  }

  void end(const InputState&) override {}
  void cancel() override {}
};

} // namespace

CameraTool3D::CameraTool3D(render::PerspectiveCamera& camera, QWidget* widget)
  : ToolController{}
  , Tool{true}
  , m_camera{camera}
  , m_widget(widget)
{
}

Tool& CameraTool3D::tool()
{
  return *this;
}

const Tool& CameraTool3D::tool() const
{
  return *this;
}

void CameraTool3D::mouseDown(const InputState& inputState)
{
  // 右键按下直接激活飞行模式
  if (shouldLook(inputState) && m_widget && !m_cursorLocked) {
    // 捕获鼠标
    if (m_widget->windowHandle())
      m_widget->windowHandle()->setMouseGrabEnabled(true);
    else
      m_widget->grabMouse();
    // 隐藏光标
    m_widget->setCursor(Qt::BlankCursor);
    // 记录中心点并重置
    m_center = m_widget->rect().center();
    QCursor::setPos(m_widget->mapToGlobal(m_center));
    m_cursorLocked = true;
    // 同步InputState的鼠标参考点，消除初始delta跳变
    const_cast<InputState&>(inputState).mouseMove(float(m_center.x()), float(m_center.y()), 0.0f, 0.0f);
  }
}

void CameraTool3D::mouseScroll(const InputState& inputState)
{
  const float factor = pref(Preferences::CameraMouseWheelInvert) ? -1.0f : 1.0f;
  const bool zoom = inputState.modifierKeysPressed(ModifierKeys::Shift);
  const float scrollDist =
#ifdef __APPLE__
    inputState.modifierKeysPressed(ModifierKeys::Shift) ? inputState.scrollX()
                                                        : inputState.scrollY();
#else
    inputState.scrollY();
#endif

  if (shouldMove(inputState))
  {
    if (zoom)
    {
      const float zoomFactor = 1.0f + scrollDist / 50.0f * factor;
      m_camera.zoom(zoomFactor);
    }
    else
    {
      const auto moveDirection = pref(Preferences::CameraMoveInCursorDir)
                                   ? vm::vec3f{inputState.pickRay().direction}
                                   : m_camera.direction();
      const float distance = scrollDist * moveSpeed(m_camera, false);
      m_camera.moveBy(factor * distance * moveDirection);
    }
  }
}

void CameraTool3D::mouseUp(const InputState& inputState)
{
  if (m_cursorLocked) {
    releaseCursorLock();
  }
  if (inputState.mouseButtonsPressed(MouseButtons::Right))
  {
    auto& prefs = PreferenceManager::instance();
    if (!prefs.saveInstantly())
    {
      prefs.saveChanges();
    }
  }
}

std::unique_ptr<GestureTracker> CameraTool3D::acceptMouseDrag(
  const InputState& inputState)
{
  using namespace mdl::HitFilters;

  if (shouldOrbit(inputState))
  {
    const auto& hit =
      inputState.pickResult().first(type(mdl::nodeHitType()) && minDistance(3.0));
    const auto orbitCenter = vm::vec3f{
      hit.isMatch() ? hit.hitPoint() : m_camera.defaultPoint(inputState.pickRay())};
    return std::make_unique<OrbitDragTracker>(m_camera, orbitCenter);
  }

  if (shouldLook(inputState))
  {
    return std::make_unique<LookDragTracker>(m_camera, m_widget);
  }

  if (shouldPan(inputState))
  {
    return std::make_unique<PanDragTracker>(m_camera);
  }

  return nullptr;
}

bool CameraTool3D::cancel()
{
  return false;
}

void CameraTool3D::releaseCursorLock() {
  if (m_cursorLocked && m_widget) {
    if (m_widget->windowHandle())
      m_widget->windowHandle()->setMouseGrabEnabled(false);
    else
      m_widget->releaseMouse();
    m_widget->unsetCursor();
    m_cursorLocked = false;
  }
}

} // namespace tb::ui
