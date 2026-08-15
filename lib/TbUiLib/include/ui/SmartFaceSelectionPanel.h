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

#pragma once

#include <QFrame>

#include "mdl/BrushFaceHandle.h"
#include "mdl/ModelUtils.h"

#include <functional>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QToolButton;
class QWidget;

namespace tb::ui
{

enum class SmartFaceSelectionOperation
{
  Replace,
  Add,
  Remove
};

class SmartFaceSelectionPanel : public QFrame
{
  Q_OBJECT
private:
  std::vector<mdl::BrushFaceHandle> m_initialSelection;
  std::vector<mdl::BrushFaceHandle> m_candidates;

  QToolButton* m_collapseButton = nullptr;
  QLabel* m_countLabel = nullptr;
  QWidget* m_controls = nullptr;
  QComboBox* m_mode = nullptr;
  QComboBox* m_operation = nullptr;
  QDoubleSpinBox* m_angle = nullptr;
  QDoubleSpinBox* m_gap = nullptr;

  std::function<void()> m_parametersDidChange;
  std::function<void()> m_confirm;
  std::function<void()> m_cancel;

public:
  SmartFaceSelectionPanel(
    std::vector<mdl::BrushFaceHandle> initialSelection,
    std::function<void()> parametersDidChange,
    std::function<void()> confirm,
    std::function<void()> cancel,
    QWidget* parent = nullptr);

  const std::vector<mdl::BrushFaceHandle>& initialSelection() const;
  const std::vector<mdl::BrushFaceHandle>& candidates() const;
  void setCandidates(std::vector<mdl::BrushFaceHandle> candidates);

  mdl::SmartFaceSelectionOptions options() const;
  SmartFaceSelectionOperation operation() const;

  bool expanded() const;
  void setExpanded(bool expanded);

private:
  void createGui();
  void updateCount();
};

} // namespace tb::ui
