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

#include <QWidget>

#include "base/NotifierConnection.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace tb::ui
{
class ChamferTool;
class MapDocument;

class ChamferToolPage : public QWidget
{
  Q_OBJECT
private:
  MapDocument& m_document;
  ChamferTool& m_tool;

  QComboBox* m_target = nullptr;
  QDoubleSpinBox* m_distance = nullptr;
  QLabel* m_segmentsLabel = nullptr;
  QSpinBox* m_segments = nullptr;
  QLabel* m_status = nullptr;
  QPushButton* m_apply = nullptr;

  NotifierConnection m_notifierConnection;

public:
  ChamferToolPage(MapDocument& document, ChamferTool& tool, QWidget* parent = nullptr);

private:
  void createGui();
  void connectObservers();
  void targetChanged(int index);
  void applyChamfer();
  void updateGui();
};

} // namespace tb::ui
