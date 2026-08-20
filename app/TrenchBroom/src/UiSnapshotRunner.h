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
#include <QStringList>

#include <functional>

class QApplication;

namespace tb::ui
{
class AppController;

struct UiSnapshotCommandLineOptions
{
  QString outputPath;
  QString theme;
  QString page;
  QString gamePath;
};

bool isSupportedUiSnapshotPage(const QString& page);

bool uiSnapshotPageRequiresMap(const QString& page);

using OpenFilesForUiSnapshot = std::function<bool(const QStringList&)>;

int runUiSnapshot(
  QApplication& app,
  AppController& appController,
  const UiSnapshotCommandLineOptions& options,
  const QStringList& fileNames,
  const OpenFilesForUiSnapshot& openFiles);

} // namespace tb::ui
