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

#include "CurveGenInspector.h"

#include <QVBoxLayout>

#include "ui/MapDocument.h"
#include "ui/QtUtils.h"
#include "ui/Splitter.h"

namespace tb::ui
{

CurveGenInspector::CurveGenInspector(
  std::weak_ptr<MapDocument> document, GLContextManager& contextManager, QWidget* parent)
  : TabBookPage{parent}
{
  createGui(std::move(document), contextManager);
}

CurveGenInspector::~CurveGenInspector()
{
  saveWindowState(m_splitter);
}

void CurveGenInspector::createGui(
  std::weak_ptr<MapDocument> document, GLContextManager& contextManager)
{
  m_splitter = new Splitter{Qt::Vertical};
  m_splitter->setObjectName("CurveGenInspector");

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(m_splitter, 1);
  setLayout(layout);

  restoreWindowState(m_splitter);
}

} // namespace tb::ui
