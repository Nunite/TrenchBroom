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

#include <QString>

#include <optional>

class QApplication;

namespace tb::ui
{

/**
 * Installs the application proxy style, palette, and expanded stylesheet.
 * Returns false if the stylesheet could not be loaded or expanded.
 */
bool installApplicationStyle(
  QApplication& app,
  const std::optional<QString>& themeOverride = std::nullopt,
  QString* error = nullptr);

} // namespace tb::ui
