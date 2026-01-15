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

#include "ModelBrowser.h"

#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QStackedWidget>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "io/ResourceUtils.h"
#include "io/DiskIO.h"
#include "io/PathInfo.h"
#include "io/FileSystem.h"
#include "io/PathMatcher.h"
#include "io/PathQt.h"
#include "io/TraversalMode.h"
#include "mdl/Game.h"
#include "mdl/Map.h"
#include "mdl/Map_World.h"
#include "mdl/EntityModelManager.h"
#include "ui/ModelBrowserView.h"
#include "ui/QtUtils.h"

#include "kdl/ranges/to.h"
#include "kdl/path_utils.h"

#include <algorithm>
#include <ranges>

namespace tb::ui
{

ModelBrowser::ModelBrowser(mdl::Map& map, GLContextManager& contextManager, QWidget* parent)
  : QWidget{parent}
  , m_map{map}
{
  createGui(contextManager);
  bindEvents();
  connectObservers();
  setFolderPath(std::filesystem::path{"models"});
}

ModelBrowser::~ModelBrowser()
{
  if (m_fileSystemWatcher)
  {
    delete std::exchange(m_fileSystemWatcher, nullptr);
  }
}

void ModelBrowser::createGui(GLContextManager& contextManager)
{
  m_pathStack = new QStackedWidget{};

  m_breadcrumbBar = new QWidget{};
  m_breadcrumbLayout = new QHBoxLayout{};
  m_breadcrumbLayout->setContentsMargins(6, 0, 0, 0);
  m_breadcrumbLayout->setSpacing(4);
  m_breadcrumbBar->setLayout(m_breadcrumbLayout);
  m_breadcrumbBar->installEventFilter(this);

  m_folderEdit = new QLineEdit{};
  m_folderEdit->installEventFilter(this);

  m_pathStack->addWidget(m_breadcrumbBar);
  m_pathStack->addWidget(m_folderEdit);
  m_pathStack->setCurrentWidget(m_breadcrumbBar);

  m_searchBox = createSearchBox();

  auto* controlsLayout = new QVBoxLayout{};
  controlsLayout->setContentsMargins(0, 0, 0, 0);
  controlsLayout->setSpacing(0);
  controlsLayout->addWidget(m_pathStack, 0);
  controlsLayout->addWidget(m_searchBox, 0);

  auto* controls = new QWidget{};
  controls->setLayout(controlsLayout);

  m_folderTree = new QTreeWidget{};
  m_folderTree->setHeaderHidden(true);
  m_folderTree->setUniformRowHeights(true);
  m_folderTree->setIconSize(QSize{16, 16});

  m_scrollBar = new QScrollBar{Qt::Vertical};
  m_view = new ModelBrowserView{m_scrollBar, contextManager, m_map};
  m_view->setSearchText(m_searchBox->text());

  auto* browserLayout = new QHBoxLayout{};
  browserLayout->setContentsMargins(0, 0, 0, 0);
  browserLayout->setSpacing(0);
  browserLayout->addWidget(m_view, 1);
  browserLayout->addWidget(m_scrollBar, 0);

  auto* browser = new QWidget{};
  browser->setLayout(browserLayout);

  auto* splitter = new QSplitter{Qt::Horizontal};
  splitter->setChildrenCollapsible(false);
  splitter->addWidget(m_folderTree);
  splitter->addWidget(browser);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(controls, 0);
  layout->addWidget(splitter, 1);
  setLayout(layout);

  m_fileSystemWatcher = new QFileSystemWatcher{this};
  m_rescanTimer = new QTimer{this};
  m_rescanTimer->setSingleShot(true);
}

void ModelBrowser::bindEvents()
{
  connect(m_searchBox, &QLineEdit::textChanged, this, [&](const QString& text) {
    if (m_view)
    {
      m_view->setSearchText(text);
    }
  });

  connect(m_folderEdit, &QLineEdit::editingFinished, this, [&]() {
    if (!m_folderEdit->isModified())
    {
      showBreadcrumbBar();
      return;
    }

    const auto typedPath = std::filesystem::path{m_folderEdit->text().toStdString()}.lexically_normal();
    if (typedPath.empty() || typedPath == std::filesystem::path{"."})
    {
      setCurrentFolderPath(std::filesystem::path{});
      showBreadcrumbBar();
      return;
    }

    const auto rootPath = m_folderPath.lexically_normal();
    if (!rootPath.empty())
    {
      if (typedPath == rootPath)
      {
        setCurrentFolderPath(std::filesystem::path{});
        showBreadcrumbBar();
        return;
      }

      if (kdl::path_has_prefix(typedPath, rootPath))
      {
        setCurrentFolderPath(typedPath.lexically_relative(rootPath));
        showBreadcrumbBar();
        return;
      }
    }

    setFolderPath(typedPath);
    showBreadcrumbBar();
  });

  connect(
    m_fileSystemWatcher,
    &QFileSystemWatcher::directoryChanged,
    this,
    [&]() { scheduleRescan(); });

  connect(m_rescanTimer, &QTimer::timeout, this, [&]() { rescanWatchedDirectory(); });

  connect(m_folderTree, &QTreeWidget::currentItemChanged, this, [&](QTreeWidgetItem* current, QTreeWidgetItem*) {
    if (!current)
    {
      return;
    }
    const auto rel = current->data(0, Qt::UserRole).toString();
    setCurrentFolderPath(std::filesystem::path{rel.toStdString()});
  });

  connect(m_view, &ModelBrowserView::folderActivated, this, [&](const QString& folderPath) {
    setCurrentFolderPath(std::filesystem::path{folderPath.toStdString()});
  });
}

void ModelBrowser::connectObservers()
{
  m_notifierConnection +=
    m_map.mapWasCreatedNotifier.connect(this, &ModelBrowser::mapWasCreated);
  m_notifierConnection +=
    m_map.mapWasLoadedNotifier.connect(this, &ModelBrowser::mapWasLoaded);
  m_notifierConnection += m_map.modsDidChangeNotifier.connect(this, &ModelBrowser::modsDidChange);
}

void ModelBrowser::mapWasCreated(mdl::Map&)
{
  reloadModels();
  setWatchedDirectory();
}

void ModelBrowser::mapWasLoaded(mdl::Map&)
{
  reloadModels();
  setWatchedDirectory();
}

void ModelBrowser::modsDidChange()
{
  if (m_folderPath != std::filesystem::path{"models"})
  {
    setFolderPath(std::filesystem::path{"models"});
    return;
  }

  reloadModels();
  setWatchedDirectory();
}

bool ModelBrowser::eventFilter(QObject* obj, QEvent* event)
{
  if (obj == m_breadcrumbBar && event->type() == QEvent::MouseButtonDblClick)
  {
    showPathEditor();
    return true;
  }

  if (obj == m_folderEdit && event->type() == QEvent::KeyPress)
  {
    const auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->key() == Qt::Key_Escape)
    {
      showBreadcrumbBar();
      return true;
    }
  }

  return QWidget::eventFilter(obj, event);
}

void ModelBrowser::rebuildBreadcrumbBar()
{
  if (!m_breadcrumbLayout)
  {
    return;
  }

  while (auto* item = m_breadcrumbLayout->takeAt(0))
  {
    if (auto* w = item->widget())
    {
      w->deleteLater();
    }
    delete item;
  }

  const auto addSeparator = [&]() {
    auto* sep = new QLabel{QString::fromUtf8("›")};
    sep->setEnabled(false);
    m_breadcrumbLayout->addWidget(sep, 0);
  };

  const auto addCrumb = [&](const QString& title, const std::filesystem::path& folderPath) {
    auto* button = new QToolButton{};
    button->setAutoRaise(true);
    button->setText(title);
    connect(button, &QToolButton::clicked, this, [this, folderPath]() {
      setCurrentFolderPath(folderPath);
    });
    m_breadcrumbLayout->addWidget(button, 0);
  };

  addCrumb(tr("All"), std::filesystem::path{});

  auto accumulated = std::filesystem::path{};
  for (const auto& part : m_currentFolderPath)
  {
    accumulated /= part;
    addSeparator();
    addCrumb(QString::fromStdString(part.generic_string()), accumulated);
  }

  m_breadcrumbLayout->addStretch(1);
}

void ModelBrowser::showPathEditor()
{
  if (!m_pathStack || !m_folderEdit)
  {
    return;
  }

  auto displayedPath = m_folderPath;
  if (!m_currentFolderPath.empty())
  {
    displayedPath /= m_currentFolderPath;
  }

  auto displayedText = displayedPath.generic_string();
  if (displayedText.empty())
  {
    displayedText = ".";
  }

  m_folderEdit->setText(QString::fromStdString(displayedText));
  m_folderEdit->setModified(false);
  m_pathStack->setCurrentWidget(m_folderEdit);
  m_folderEdit->setFocus();
  m_folderEdit->selectAll();
}

void ModelBrowser::showBreadcrumbBar()
{
  if (!m_pathStack || !m_breadcrumbBar)
  {
    return;
  }

  updateFolderEdit();
  m_pathStack->setCurrentWidget(m_breadcrumbBar);
}

void ModelBrowser::updateFolderEdit()
{
  rebuildBreadcrumbBar();
  if (!m_folderEdit || !m_pathStack || m_pathStack->currentWidget() == m_folderEdit)
  {
    return;
  }
}

void ModelBrowser::setFolderPath(std::filesystem::path folderPath)
{
  folderPath = folderPath.lexically_normal();
  if (m_folderPath == folderPath)
  {
    return;
  }

  m_folderPath = std::move(folderPath);
  m_currentFolderPath.clear();
  updateFolderEdit();

  reloadModels();
  setWatchedDirectory();
}

void ModelBrowser::setCurrentFolderPath(std::filesystem::path currentFolderPath)
{
  currentFolderPath = currentFolderPath.lexically_normal();
  if (currentFolderPath == std::filesystem::path{"."})
  {
    currentFolderPath.clear();
  }

  if (m_currentFolderPath == currentFolderPath)
  {
    return;
  }

  m_currentFolderPath = std::move(currentFolderPath);
  if (m_view)
  {
    m_view->setCurrentFolderPath(m_currentFolderPath);
  }

  if (m_folderTree)
  {
    if (auto it = m_folderTreeItems.find(m_currentFolderPath); it != m_folderTreeItems.end())
    {
      if (m_folderTree->currentItem() != it->second)
      {
        m_folderTree->setCurrentItem(it->second);
      }
    }
  }

  updateFolderEdit();
}

void ModelBrowser::reloadModels()
{
  const auto* game = m_map.game();
  if (!game)
  {
    m_modelPaths.clear();
    m_lastWriteTimes.clear();
    m_currentFolderPath.clear();
    updateFolderEdit();
    rebuildFolderTree();
    m_view->setModelPaths(m_folderPath, std::vector<std::filesystem::path>{});
    m_view->setCurrentFolderPath(m_currentFolderPath);
    return;
  }

  if (m_folderPath.is_absolute())
  {
    auto pathsResult =
      io::Disk::find(
        m_folderPath,
        io::TraversalMode::Recursive,
        io::makeExtensionPathMatcher({".mdl"}));
    if (pathsResult.is_error())
    {
      m_modelPaths.clear();
      m_lastWriteTimes.clear();
      m_currentFolderPath.clear();
      updateFolderEdit();
      rebuildFolderTree();
      m_view->setModelPaths(m_folderPath, std::vector<std::filesystem::path>{});
      m_view->setCurrentFolderPath(m_currentFolderPath);
      return;
    }

    auto modelPaths = pathsResult.value();
    std::ranges::sort(modelPaths);

    auto lastWriteTimes =
      std::unordered_map<std::filesystem::path, std::filesystem::file_time_type, kdl::path_hash>{};
    lastWriteTimes.reserve(modelPaths.size());

    for (const auto& modelPath : modelPaths)
    {
      auto error = std::error_code{};
      const auto t = std::filesystem::last_write_time(modelPath, error);
      if (!error)
      {
        lastWriteTimes.emplace(modelPath, t);
      }
    }

    m_modelPaths = std::move(modelPaths);
    m_lastWriteTimes = std::move(lastWriteTimes);
    rebuildFolderTree();
    m_view->setModelPaths(m_folderPath, m_modelPaths);
    m_view->setCurrentFolderPath(m_currentFolderPath);
    return;
  }

  const auto& fs = game->gameFileSystem();
  const auto enabledMods = mdl::enabledMods(m_map);
  if (enabledMods.empty())
  {
    m_modelPaths.clear();
    m_lastWriteTimes.clear();
    m_currentFolderPath.clear();
    updateFolderEdit();
    rebuildFolderTree();
    m_view->setModelPaths(m_folderPath, std::vector<std::filesystem::path>{});
    m_view->setCurrentFolderPath(m_currentFolderPath);
    return;
  }

  const auto modRoots = enabledMods | std::views::transform([&](const auto& mod) {
                          return (game->gamePath() / std::filesystem::path{mod}).lexically_normal();
                        })
                        | kdl::ranges::to<std::vector>();

  auto pathsResult =
    fs.find(m_folderPath, io::TraversalMode::Recursive, io::makeExtensionPathMatcher({".mdl"}));
  if (pathsResult.is_error())
  {
    m_modelPaths.clear();
    m_lastWriteTimes.clear();
    m_currentFolderPath.clear();
    updateFolderEdit();
    rebuildFolderTree();
    m_view->setModelPaths(m_folderPath, std::vector<std::filesystem::path>{});
    m_view->setCurrentFolderPath(m_currentFolderPath);
    return;
  }

  auto modelPaths = pathsResult.value();
  std::ranges::sort(modelPaths);

  auto filteredModelPaths = std::vector<std::filesystem::path>{};
  filteredModelPaths.reserve(modelPaths.size());
  for (const auto& modelPath : modelPaths)
  {
    if (auto absPathResult = fs.makeAbsolute(modelPath); !absPathResult.is_error())
    {
      const auto absPath = absPathResult.value().lexically_normal();
      auto found = false;
      for (const auto& modRoot : modRoots)
      {
        if (kdl::path_has_prefix(absPath, modRoot))
        {
          found = true;
          break;
        }
      }
      if (found)
      {
        filteredModelPaths.push_back(modelPath);
      }
    }
  }

  modelPaths = std::move(filteredModelPaths);

  auto lastWriteTimes =
    std::unordered_map<std::filesystem::path, std::filesystem::file_time_type, kdl::path_hash>{};
  lastWriteTimes.reserve(modelPaths.size());

  for (const auto& modelPath : modelPaths)
  {
    if (auto absPathResult = fs.makeAbsolute(modelPath); !absPathResult.is_error())
    {
      const auto& absPath = absPathResult.value();
      auto error = std::error_code{};
      const auto t = std::filesystem::last_write_time(absPath, error);
      if (!error)
      {
        lastWriteTimes.emplace(modelPath, t);
      }
    }
  }

  m_modelPaths = std::move(modelPaths);
  m_lastWriteTimes = std::move(lastWriteTimes);

  rebuildFolderTree();
  m_view->setModelPaths(m_folderPath, m_modelPaths);
  m_view->setCurrentFolderPath(m_currentFolderPath);
}

void ModelBrowser::rebuildFolderTree()
{
  if (!m_folderTree)
  {
    return;
  }

  auto blocker = QSignalBlocker{m_folderTree};

  m_folderTree->clear();
  m_folderTreeItems.clear();

  const auto folderIcon = io::loadSVGIcon(std::filesystem::path{"Map_folder.svg"});

  auto rootTitle = m_folderPath.filename().empty() ? m_folderPath : m_folderPath.filename();
  auto rootTitleText = rootTitle.generic_string();
  if (rootTitleText.empty())
  {
    rootTitleText = ".";
  }

  auto* rootItem = new QTreeWidgetItem{m_folderTree, QStringList{QString::fromStdString(rootTitleText)}};
  rootItem->setData(0, Qt::UserRole, QString{});
  rootItem->setIcon(0, folderIcon);
  m_folderTreeItems.emplace(std::filesystem::path{}, rootItem);

  for (const auto& modelPath : m_modelPaths)
  {
    if (modelPath.empty())
    {
      continue;
    }

    if (!m_folderPath.empty() && !kdl::path_has_prefix(modelPath, m_folderPath))
    {
      continue;
    }

    const auto relModelPath =
      m_folderPath.empty() ? modelPath : modelPath.lexically_relative(m_folderPath);
    const auto relFolderPath = relModelPath.parent_path();

    auto* parentItem = rootItem;
    auto accumulatedPath = std::filesystem::path{};
    for (const auto& part : relFolderPath)
    {
      accumulatedPath /= part;
      if (m_folderTreeItems.contains(accumulatedPath))
      {
        parentItem = m_folderTreeItems[accumulatedPath];
        continue;
      }

      auto* item = new QTreeWidgetItem{parentItem, QStringList{QString::fromStdString(part.generic_string())}};
      item->setData(0, Qt::UserRole, QString::fromStdString(accumulatedPath.generic_string()));
      item->setIcon(0, folderIcon);
      parentItem = item;
      m_folderTreeItems.emplace(accumulatedPath, item);
    }
  }

  m_folderTree->expandItem(rootItem);

  if (auto it = m_folderTreeItems.find(m_currentFolderPath); it != m_folderTreeItems.end())
  {
    m_folderTree->setCurrentItem(it->second);
  }
  else
  {
    m_currentFolderPath.clear();
    m_folderTree->setCurrentItem(rootItem);
    if (m_view)
    {
      m_view->setCurrentFolderPath(m_currentFolderPath);
    }
    updateFolderEdit();
  }
}

void ModelBrowser::setWatchedDirectory()
{
  if (!m_fileSystemWatcher)
  {
    return;
  }

  if (!m_fileSystemWatcher->directories().isEmpty())
  {
    m_fileSystemWatcher->removePaths(m_fileSystemWatcher->directories());
  }

  const auto* game = m_map.game();
  if (!game)
  {
    return;
  }

  if (m_folderPath.is_absolute())
  {
    if (io::Disk::pathInfo(m_folderPath) == io::PathInfo::Directory)
    {
      m_fileSystemWatcher->addPath(io::pathAsQString(m_folderPath));
    }
    return;
  }

  const auto& fs = game->gameFileSystem();
  if (mdl::enabledMods(m_map).empty())
  {
    return;
  }
  if (auto absPathResult = fs.makeAbsolute(m_folderPath); !absPathResult.is_error())
  {
    const auto& absPath = absPathResult.value();
    if (io::Disk::pathInfo(absPath) == io::PathInfo::Directory)
    {
      m_fileSystemWatcher->addPath(io::pathAsQString(absPath));
    }
  }
}

void ModelBrowser::scheduleRescan()
{
  if (m_rescanTimer)
  {
    m_rescanTimer->start(200);
  }
}

void ModelBrowser::rescanWatchedDirectory()
{
  const auto* game = m_map.game();
  if (!game)
  {
    return;
  }

  if (m_folderPath.is_absolute())
  {
    auto pathsResult =
      io::Disk::find(
        m_folderPath,
        io::TraversalMode::Recursive,
        io::makeExtensionPathMatcher({".mdl"}));
    if (pathsResult.is_error())
    {
      m_modelPaths.clear();
      m_lastWriteTimes.clear();
      m_currentFolderPath.clear();
      updateFolderEdit();
      rebuildFolderTree();
      m_view->setModelPaths(m_folderPath, std::vector<std::filesystem::path>{});
      m_view->setCurrentFolderPath(m_currentFolderPath);
      return;
    }

    auto modelPaths = pathsResult.value();
    std::ranges::sort(modelPaths);

    auto newLastWriteTimes =
      std::unordered_map<std::filesystem::path, std::filesystem::file_time_type, kdl::path_hash>{};
    newLastWriteTimes.reserve(modelPaths.size());

    auto changedPaths = std::vector<std::filesystem::path>{};

    for (const auto& modelPath : modelPaths)
    {
      auto error = std::error_code{};
      const auto t = std::filesystem::last_write_time(modelPath, error);
      if (!error)
      {
        newLastWriteTimes.emplace(modelPath, t);
        if (const auto it = m_lastWriteTimes.find(modelPath);
            it == m_lastWriteTimes.end() || it->second != t)
        {
          changedPaths.push_back(modelPath);
        }
      }
    }

    for (const auto& [oldPath, oldTime] : m_lastWriteTimes)
    {
      unused(oldTime);
      if (!newLastWriteTimes.contains(oldPath))
      {
        changedPaths.push_back(oldPath);
      }
    }

    if (changedPaths.empty() && modelPaths == m_modelPaths)
    {
      return;
    }

    for (const auto& p : changedPaths)
    {
      m_map.entityModelManager().invalidateModel(p);
    }

    m_modelPaths = std::move(modelPaths);
    m_lastWriteTimes = std::move(newLastWriteTimes);
    rebuildFolderTree();
    m_view->setModelPaths(m_folderPath, m_modelPaths);
    m_view->setCurrentFolderPath(m_currentFolderPath);
    return;
  }

  const auto& fs = game->gameFileSystem();
  const auto enabledMods = mdl::enabledMods(m_map);
  if (enabledMods.empty())
  {
    if (!m_modelPaths.empty() || !m_lastWriteTimes.empty())
    {
      for (const auto& p : m_modelPaths)
      {
        m_map.entityModelManager().invalidateModel(p);
      }
      m_modelPaths.clear();
      m_lastWriteTimes.clear();
      m_currentFolderPath.clear();
      updateFolderEdit();
      rebuildFolderTree();
      m_view->setModelPaths(m_folderPath, std::vector<std::filesystem::path>{});
      m_view->setCurrentFolderPath(m_currentFolderPath);
    }
    return;
  }

  const auto modRoots = enabledMods | std::views::transform([&](const auto& mod) {
                          return (game->gamePath() / std::filesystem::path{mod}).lexically_normal();
                        })
                        | kdl::ranges::to<std::vector>();

  auto pathsResult =
    fs.find(m_folderPath, io::TraversalMode::Recursive, io::makeExtensionPathMatcher({".mdl"}));
  if (pathsResult.is_error())
  {
    m_modelPaths.clear();
    m_lastWriteTimes.clear();
    m_currentFolderPath.clear();
    updateFolderEdit();
    rebuildFolderTree();
    m_view->setModelPaths(m_folderPath, std::vector<std::filesystem::path>{});
    m_view->setCurrentFolderPath(m_currentFolderPath);
    return;
  }

  auto modelPaths = pathsResult.value();
  std::ranges::sort(modelPaths);

  auto filteredModelPaths = std::vector<std::filesystem::path>{};
  filteredModelPaths.reserve(modelPaths.size());
  for (const auto& modelPath : modelPaths)
  {
    if (auto absPathResult = fs.makeAbsolute(modelPath); !absPathResult.is_error())
    {
      const auto absPath = absPathResult.value().lexically_normal();
      auto found = false;
      for (const auto& modRoot : modRoots)
      {
        if (kdl::path_has_prefix(absPath, modRoot))
        {
          found = true;
          break;
        }
      }
      if (found)
      {
        filteredModelPaths.push_back(modelPath);
      }
    }
  }

  modelPaths = std::move(filteredModelPaths);

  auto newLastWriteTimes =
    std::unordered_map<std::filesystem::path, std::filesystem::file_time_type, kdl::path_hash>{};
  newLastWriteTimes.reserve(modelPaths.size());

  auto changedPaths = std::vector<std::filesystem::path>{};

  for (const auto& modelPath : modelPaths)
  {
    if (auto absPathResult = fs.makeAbsolute(modelPath); !absPathResult.is_error())
    {
      const auto& absPath = absPathResult.value();
      auto error = std::error_code{};
      const auto t = std::filesystem::last_write_time(absPath, error);
      if (!error)
      {
        newLastWriteTimes.emplace(modelPath, t);
        if (const auto it = m_lastWriteTimes.find(modelPath);
            it == m_lastWriteTimes.end() || it->second != t)
        {
          changedPaths.push_back(modelPath);
        }
      }
    }
  }

  for (const auto& [oldPath, oldTime] : m_lastWriteTimes)
  {
    unused(oldTime);
    if (!newLastWriteTimes.contains(oldPath))
    {
      changedPaths.push_back(oldPath);
    }
  }

  if (changedPaths.empty() && modelPaths == m_modelPaths)
  {
    return;
  }

  for (const auto& p : changedPaths)
  {
    m_map.entityModelManager().invalidateModel(p);
  }

  m_modelPaths = std::move(modelPaths);
  m_lastWriteTimes = std::move(newLastWriteTimes);
  rebuildFolderTree();
  m_view->setModelPaths(m_folderPath, m_modelPaths);
  m_view->setCurrentFolderPath(m_currentFolderPath);
}

} // namespace tb::ui
