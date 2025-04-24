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

#include <memory>
#include <QWidget>
#include <QTextEdit>
#include "ui/Selection.h"      // 确保包含 Selection
#include "ui/Splitter.h"       // 确保包含 Splitter
#include "ui/TabBook.h"    // 确保包含 TabBookPage

namespace tb::ui {

class GLContextManager;
class MapDocument;

class CurveGenInspector : public TabBookPage {
    Q_OBJECT
public:
    CurveGenInspector(std::weak_ptr<MapDocument> document, GLContextManager& contextManager, QWidget* parent = nullptr);
    ~CurveGenInspector();

public slots:
    void updateSelectionContent(const Selection& selection);

private:
    std::weak_ptr<MapDocument> m_document;
    Splitter* m_splitter = nullptr;
    QTextEdit* m_contentView = nullptr;

    void createGui(std::weak_ptr<MapDocument> document, GLContextManager& contextManager);
};

} // namespace tb::ui
