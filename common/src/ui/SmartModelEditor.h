/*
 Copyright (C) 2023 Lws

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

#include "ui/SmartPropertyEditor.h"

#include <memory>

class QLineEdit;
class QAbstractButton;
class QWidget;

namespace tb::ui
{
class MapDocument;

class SmartModelEditor : public SmartPropertyEditor
{
  Q_OBJECT
private:
  QLineEdit* m_pathLineEdit = nullptr;
  QAbstractButton* m_browseButton = nullptr;

public:
  explicit SmartModelEditor(std::weak_ptr<MapDocument> document, QWidget* parent = nullptr);

private slots:
  void browseFile();

private:
  void doUpdateVisual(const std::vector<mdl::EntityNodeBase*>& nodes) override;
};

} // namespace tb::ui 