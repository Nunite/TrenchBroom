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
#include <QDateTime>
#include <QApplication>

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
  // 当按下右键时，先检查是否是Shift+右键多选面组合
  if (inputState.mouseButtonsPressed(MouseButtons::Right) && 
      inputState.modifierKeysDown(ModifierKeys::Shift)) {
    // 只有当开关启用时才跳过拦截
    if (pref(Preferences::CameraEnableShiftRightClickSelect)) {
      return false; // 不拦截Shift+右键组合，允许SelectionTool处理多选面
    }
    // 如果开关关闭，则像处理普通右键一样处理
    return true; // 强制拦截Shift+右键组合
  }
  
  // 否则，按照常规逻辑处理右键
  return (
    inputState.mouseButtonsPressed(MouseButtons::Right)
    && inputState.modifierKeysPressed(ModifierKeys::None)
    && !inputState.anyToolDragging());
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
  bool m_isWindows;

public:
  explicit LookDragTracker(render::PerspectiveCamera& camera, QWidget* widget = nullptr)
    : m_camera{camera}
    , m_widget{widget}
    , m_isWindows(QSysInfo::productType().toLower() == "windows")
  {
    if (m_widget) {
      m_center = m_widget->rect().center();
      m_lastMousePos = m_center;
      
      // 确保鼠标隐藏
      QApplication::setOverrideCursor(Qt::BlankCursor);
      m_widget->setCursor(Qt::BlankCursor);
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
        m_lastMousePos = m_center;
      }
      
      return true; // 跳过首次更新，避免视角突变
    }
    
    float hAngle = 0.0f;
    float vAngle = 0.0f;
    
    if (m_isWindows && m_widget) {
      // Windows系统使用绝对位置计算，这在Windows上工作良好
      QPoint currentPos = m_widget->mapFromGlobal(QCursor::pos());
      float dx = static_cast<float>(currentPos.x() - m_center.x());
      float dy = static_cast<float>(currentPos.y() - m_center.y());
      
      hAngle = dx * lookSpeedH(m_camera);
      vAngle = dy * lookSpeedV(m_camera);
    } else {
      // 其他系统(Linux/macOS)使用InputState的相对移动值
      hAngle = static_cast<float>(inputState.mouseDX()) * lookSpeedH(m_camera);
      vAngle = static_cast<float>(inputState.mouseDY()) * lookSpeedV(m_camera);
    }
    
    m_camera.rotate(hAngle, vAngle);
    
    // 重置鼠标位置到中心点
    if (m_widget && inputState.mouseButtonsPressed(MouseButtons::Right)) {
      QCursor::setPos(m_widget->mapToGlobal(m_center));
    }
    
    return true;
  }

  void end(const InputState&) override {
    // 恢复光标显示
    if (m_widget) {
      QApplication::restoreOverrideCursor();
      m_widget->unsetCursor();
    }
  }
  
  void cancel() override {
    // 恢复光标显示
    if (m_widget) {
      QApplication::restoreOverrideCursor();
      m_widget->unsetCursor();
    }
  }
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
  , m_rightClickStartTime(0)
  , m_rightClickStartPos(0, 0)
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
  // 如果是Shift+右键多选面组合且开关启用，直接跳过
  if (inputState.mouseButtonsPressed(MouseButtons::Right) && 
      inputState.modifierKeysDown(ModifierKeys::Shift)) {
    if (pref(Preferences::CameraEnableShiftRightClickSelect)) {
      return;
    }
    // 否则继续处理
  }

  // 记录鼠标按下时的位置和时间（用于检测短点击）
  if (inputState.mouseButtonsPressed(MouseButtons::Right) && m_widget) {
    m_rightClickStartTime = QDateTime::currentMSecsSinceEpoch();
    m_rightClickStartPos = m_widget->mapFromGlobal(QCursor::pos());
    
    // 暂时不立即激活look，在短暂延迟后或移动超过阈值时再激活
    // 我们不设置m_cursorLocked，这会在移动鼠标时激活
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
  // 由于mouseUp事件在按钮释放后触发，当前的按钮状态已经不包含该按钮
  // 假设右键被释放，但需要根据其他上下文判断
  
  // 检查是否是右键事件
  const bool isRightMouseEvent = m_rightClickStartTime > 0;
  
  // 如果是Shift+右键释放且开关启用，直接跳过
  if (isRightMouseEvent && inputState.modifierKeysDown(ModifierKeys::Shift)) {
    if (pref(Preferences::CameraEnableShiftRightClickSelect)) {
      m_rightClickStartTime = 0; // 重置状态
      return;
    }
    // 开关关闭时，继续处理
  }

  // 检查是否是短时间右键点击（用于显示上下文菜单）
  if (m_widget && isRightMouseEvent && !m_cursorLocked) {
    QPoint currentPos = m_widget->mapFromGlobal(QCursor::pos());
    if (isRightClickForContextMenu(inputState, currentPos)) {
      // 是短点击，显示上下文菜单
      showContextMenu();
    }
  }

  // 解除光标锁定
  if (m_cursorLocked) {
    releaseCursorLock();
  }
  
  // 保存偏好设置
  if (isRightMouseEvent)
  {
    auto& prefs = PreferenceManager::instance();
    if (!prefs.saveInstantly())
    {
      prefs.saveChanges();
    }
  }
  
  // 重置右键事件状态
  if (isRightMouseEvent) {
    m_rightClickStartTime = 0;
  }
}

bool CameraTool3D::isRightClickForContextMenu(const InputState& /* inputState */, const QPoint& currentPos) const {
  // 判断是否为短时间的点击（小于300毫秒）
  const qint64 clickDuration = QDateTime::currentMSecsSinceEpoch() - m_rightClickStartTime;
  if (clickDuration > 300) return false;
  
  // 判断是否为小距离的点击（小于5像素）
  const int moveDistance = (currentPos - m_rightClickStartPos).manhattanLength();
  if (moveDistance > 5) return false;
  
  return true;
}

void CameraTool3D::showContextMenu() {
  // 使用工具链中的showPopupMenu
  if (m_widget) {
    QMetaObject::invokeMethod(m_widget, "showPopupMenuLater", Qt::QueuedConnection);
  }
}

std::unique_ptr<GestureTracker> CameraTool3D::acceptMouseDrag(
  const InputState& inputState)
{
  using namespace mdl::HitFilters;

  // 如果是Shift+右键多选面组合且开关启用，直接返回nullptr，交给SelectionTool处理
  if (inputState.mouseButtonsPressed(MouseButtons::Right) && 
      inputState.modifierKeysDown(ModifierKeys::Shift)) {
    if (pref(Preferences::CameraEnableShiftRightClickSelect)) {
      return nullptr;
    }
    // 开关关闭时，继续处理
  }

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
      
    // 恢复光标显示
    QApplication::restoreOverrideCursor();
    m_widget->unsetCursor();
    m_cursorLocked = false;
  }
}

void CameraTool3D::mouseMove(const InputState& inputState)
{
  // 如果是Shift+右键多选面组合且开关启用，直接跳过
  if (inputState.mouseButtonsPressed(MouseButtons::Right) && 
      inputState.modifierKeysDown(ModifierKeys::Shift)) {
    if (pref(Preferences::CameraEnableShiftRightClickSelect)) {
      return;
    }
    // 开关关闭时，继续处理
  }

  // 如果右键被按下，鼠标移动超过阈值，且尚未激活look锁定
  if (inputState.mouseButtonsPressed(MouseButtons::Right) && 
      m_widget && 
      !m_cursorLocked) {
    
    QPoint currentPos = m_widget->mapFromGlobal(QCursor::pos());
    // 检查鼠标移动距离是否超过阈值(10像素)
    int moveDistance = (currentPos - m_rightClickStartPos).manhattanLength();
    
    if (moveDistance > 10) {
      // 已经移动超过阈值，这是拖动操作而不是点击，激活look功能
      
      // 捕获鼠标
      if (m_widget->windowHandle()) {
        m_widget->windowHandle()->setMouseGrabEnabled(true);
      } else {
        m_widget->grabMouse();
      }
      
      // 隐藏光标 - 使用更直接的方式
      QApplication::setOverrideCursor(Qt::BlankCursor);
      m_widget->setCursor(Qt::BlankCursor);
      
      // 记录中心点并重置
      m_center = m_widget->rect().center();
      QCursor::setPos(m_widget->mapToGlobal(m_center));
      m_cursorLocked = true;
      // 同步InputState的鼠标参考点，消除初始delta跳变
      const_cast<InputState&>(inputState).mouseMove(float(m_center.x()), float(m_center.y()), 0.0f, 0.0f);
    }
  }
}

} // namespace tb::ui
