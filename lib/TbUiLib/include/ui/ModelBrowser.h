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

#include <QWidget>

#include "NotifierConnection.h"
#include "ui/ModelBrowserView.h"

#include "kd/path_hash.h"

#include <filesystem>
#include <unordered_map>
#include <vector>

class QAbstractButton;
class QEvent;
class QFileSystemWatcher;
class QHBoxLayout;
class QLineEdit;
class QScrollBar;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QTimer;

namespace tb::mdl
{
class Map;
} // namespace tb::mdl

namespace tb::ui
{
class AppController;

class ModelBrowser : public QWidget
{
  Q_OBJECT
private:
  mdl::Map& m_map;

  QStackedWidget* m_pathStack = nullptr;
  QWidget* m_breadcrumbBar = nullptr;
  QHBoxLayout* m_breadcrumbLayout = nullptr;
  QToolButton* m_reloadButton = nullptr;
  QLineEdit* m_folderEdit = nullptr;
  QLineEdit* m_searchBox = nullptr;

  QScrollBar* m_scrollBar = nullptr;
  ModelBrowserView* m_view = nullptr;
  QTreeWidget* m_folderTree = nullptr;

  QFileSystemWatcher* m_fileSystemWatcher = nullptr;
  QTimer* m_rescanTimer = nullptr;

  std::filesystem::path m_folderPath;
  std::filesystem::path m_currentFolderPath;
  std::vector<BrowserAsset> m_assets;
  std::
    unordered_map<std::filesystem::path, std::filesystem::file_time_type, kdl::path_hash>
      m_lastWriteTimes;
  std::unordered_map<std::filesystem::path, QTreeWidgetItem*, kdl::path_hash>
    m_folderTreeItems;

  NotifierConnection m_notifierConnection;

public:
  ModelBrowser(AppController& appController, mdl::Map& map, QWidget* parent = nullptr);
  ~ModelBrowser() override;

protected:
  bool eventFilter(QObject* obj, QEvent* event) override;

private:
  void createGui(AppController& appController);
  void bindEvents();
  void connectObservers();

  void rebuildBreadcrumbBar();
  void showPathEditor();
  void showBreadcrumbBar();
  void updateFolderEdit();
  void setFolderPath(std::filesystem::path folderPath);
  void setCurrentFolderPath(std::filesystem::path currentFolderPath);
  void reloadModels();
  void rebuildFolderTree();

  void mapWasCreated(mdl::Map& map);
  void mapWasLoaded(mdl::Map& map);
  void modsDidChange();

  void setWatchedDirectory();
  void scheduleRescan();
  void rescanWatchedDirectory();
};

} // namespace tb::ui
