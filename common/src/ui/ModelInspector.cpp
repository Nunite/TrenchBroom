/*
 Copyright (C) 2026

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

#include "ModelInspector.h"

#include <QVBoxLayout>

#include "mdl/Map.h"
#include "ui/ModelBrowser.h"

namespace tb::ui
{

ModelInspector::ModelInspector(
  mdl::Map& map, GLContextManager& contextManager, QWidget* parent)
  : TabBookPage{parent}
{
  createGui(map, contextManager);
}

ModelInspector::~ModelInspector() = default;

void ModelInspector::createGui(mdl::Map& map, GLContextManager& contextManager)
{
  m_modelBrowser = new ModelBrowser{map, contextManager};

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_modelBrowser, 1);
  setLayout(layout);
}

} // namespace tb::ui

