/*
 Copyright (C) 2026 XiangXtreme

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

#include "ui/ModelInspector.h"

#include <QVBoxLayout>

#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/ModelBrowser.h"

namespace tb::ui
{

ModelInspector::ModelInspector(
  AppController& appController, MapDocument& document, QWidget* parent)
  : TabBookPage{parent}
{
  createGui(appController, document);
}

ModelInspector::~ModelInspector() = default;

void ModelInspector::createGui(AppController& appController, MapDocument& document)
{
  m_modelBrowser = new ModelBrowser{appController, document};

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_modelBrowser, 1);
  setLayout(layout);
}

} // namespace tb::ui

