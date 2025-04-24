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
#include "ui/CurveGenInspector.h"
#include "ui/Selection.h" // 确保加上这一行

namespace tb::ui
{

CurveGenInspector::CurveGenInspector(
  std::weak_ptr<MapDocument> document, GLContextManager& contextManager, QWidget* parent)
  : TabBookPage{parent}
  , m_document{document} // 新增
{
  createGui(document, contextManager);
}

void CurveGenInspector::createGui(
  std::weak_ptr<MapDocument> document, GLContextManager& contextManager)
{
  m_splitter = new Splitter{Qt::Vertical};
  m_splitter->setObjectName("CurveGenInspector");

  m_contentView = new QTextEdit{};
  m_contentView->setReadOnly(true);
  m_splitter->addWidget(m_contentView); // 新增：添加显示控件

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(m_splitter, 1);
  setLayout(layout);

  restoreWindowState(m_splitter);
}

void CurveGenInspector::updateSelectionContent(const Selection& selection)
{
    auto doc = m_document.lock();
    if (!doc || !m_contentView) return; // 防止空指针

    QString content;
    if (doc->hasSelectedNodes()) {
        content = QString::fromStdString(doc->serializeSelectedNodes());
    } else if (doc->hasSelectedBrushFaces()) {
        content = QString::fromStdString(doc->serializeSelectedBrushFaces());
    } else {
        content = tr("无选中内容");
    }
    m_contentView->setPlainText(content);
}

CurveGenInspector::~CurveGenInspector()
{
  saveWindowState(m_splitter);
}

} // namespace tb::ui
