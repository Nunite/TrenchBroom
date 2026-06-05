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

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTextEdit;

namespace tb::ui
{

class PythonPluginManagerDialog : public QDialog
{
private:
  QListWidget* m_pluginPathList = nullptr;
  QPushButton* m_installButton = nullptr;
  QPushButton* m_removeButton = nullptr;
  QPushButton* m_clearButton = nullptr;
  QPushButton* m_refreshButton = nullptr;
  QLineEdit* m_searchBox = nullptr;
  QCheckBox* m_showIssuesOnlyCheckBox = nullptr;
  QListWidget* m_pluginStatusList = nullptr;
  QTextEdit* m_pluginDetails = nullptr;

public:
  explicit PythonPluginManagerDialog(QWidget* parent = nullptr);

private:
  void createGui();
  void loadPluginPaths();
  void addPluginPath(const QString& path);
  void savePluginPaths();
  void reloadPluginStatus();
  void updatePluginDetails();
};

} // namespace tb::ui
