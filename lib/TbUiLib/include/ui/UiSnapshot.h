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

class QWidget;

namespace tb::ui
{

struct UiSnapshotOptions
{
  QString outputPath;
  QString target;
  QString theme;
  QString scaleFactor;
};

/**
 * Captures the widget to a PNG and writes a JSON manifest next to it.
 *
 * The capture is rejected when it is transparent or visually uniform. This gives
 * automated UI acceptance a basic non-blank rendering guard before visual review.
 */
bool saveUiSnapshot(
  QWidget& widget, const UiSnapshotOptions& options, QString* error = nullptr);

} // namespace tb::ui
