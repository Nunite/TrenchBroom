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
#include "ui/AssetBrowserModel.h"
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
class QShowEvent;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QTimer;

namespace tb::ui
{
class AppController;
class MapDocument;

class ModelBrowser : public QWidget
{
  Q_OBJECT
private:
  MapDocument& m_document;

  QStackedWidget* m_pathStack = nullptr;
  QWidget* m_breadcrumbBar = nullptr;
  QHBoxLayout* m_breadcrumbLayout = nullptr;
  QToolButton* m_savePrefabButton = nullptr;
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
  AssetWriteTimes m_lastWriteTimes;
  std::unordered_map<std::filesystem::path, QTreeWidgetItem*, kdl::path_hash>
    m_folderTreeItems;

  NotifierConnection m_notifierConnection;
  NotifierConnection m_mapNotifierConnection;
  bool m_assetRefreshPending = true;
  bool m_assetRefreshQueued = false;

public:
  ModelBrowser(
    AppController& appController, MapDocument& document, QWidget* parent = nullptr);
  ~ModelBrowser() override;

protected:
  bool eventFilter(QObject* obj, QEvent* event) override;

private:
  void showEvent(QShowEvent* event) override;

  void createGui(AppController& appController);
  void bindEvents();
  void connectObservers();
  void connectMapObservers();

  void rebuildBreadcrumbBar();
  void showPathEditor();
  void showBreadcrumbBar();
  void updateFolderEdit();
  void updateSavePrefabButton();
  void saveSelectionAsPrefab();
  void renamePrefab(std::filesystem::path prefabPath);
  void deletePrefab(std::filesystem::path prefabPath);
  void setFolderPath(std::filesystem::path folderPath);
  void setCurrentFolderPath(std::filesystem::path currentFolderPath);
  void markAssetsDirty();
  void ensureAssetsLoaded();
  void scheduleAssetRefresh();
  std::optional<std::vector<BrowserAsset>> scanAssets() const;
  void reloadModels();
  void rebuildFolderTree();

  void documentWasLoaded();
  void modsDidChange();

  void setWatchedDirectory();
  void scheduleRescan();
  void rescanWatchedDirectory();
};

} // namespace tb::ui
