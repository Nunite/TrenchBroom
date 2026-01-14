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

#pragma once

#include "ui/TabBook.h"

namespace tb::mdl
{
class Map;
} // namespace tb::mdl

namespace tb::ui
{
class GLContextManager;
class ModelBrowser;

class ModelInspector : public TabBookPage
{
  Q_OBJECT
private:
  ModelBrowser* m_modelBrowser = nullptr;

public:
  ModelInspector(
    mdl::Map& map, GLContextManager& contextManager, QWidget* parent = nullptr);
  ~ModelInspector() override;

private:
  void createGui(mdl::Map& map, GLContextManager& contextManager);
};

} // namespace tb::ui

