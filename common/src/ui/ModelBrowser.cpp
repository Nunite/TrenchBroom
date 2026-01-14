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

#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include "io/DiskIO.h"
#include "io/PathInfo.h"
#include "io/FileSystem.h"
#include "io/PathMatcher.h"
#include "io/PathQt.h"
#include "io/TraversalMode.h"
#include "mdl/Game.h"
#include "mdl/Map.h"
#include "mdl/EntityModelManager.h"
#include "ui/ModelBrowserView.h"
#include "ui/QtUtils.h"

#include "kdl/ranges/to.h"
#include "kdl/path_utils.h"

#include <ranges>

namespace tb::ui
{

ModelBrowser::ModelBrowser(mdl::Map& map, GLContextManager& contextManager, QWidget* parent)
  : QWidget{parent}
  , m_map{map}
{
  createGui(contextManager);
  bindEvents();
  setFolderPath(std::filesystem::path{"progs"});
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
  m_folderEdit = new QLineEdit{};
  m_browseButton = new QPushButton{tr("Browse")};
  m_reloadButton = new QPushButton{tr("Reload")};

  auto* controlsLayout = new QHBoxLayout{};
  controlsLayout->setContentsMargins(0, 0, 0, 0);
  controlsLayout->addWidget(m_folderEdit, 1);
  controlsLayout->addWidget(m_browseButton, 0);
  controlsLayout->addWidget(m_reloadButton, 0);

  auto* controls = new QWidget{};
  controls->setLayout(controlsLayout);

  m_scrollBar = new QScrollBar{Qt::Vertical};
  m_view = new ModelBrowserView{m_scrollBar, contextManager, m_map};

  auto* browserLayout = new QHBoxLayout{};
  browserLayout->setContentsMargins(0, 0, 0, 0);
  browserLayout->setSpacing(0);
  browserLayout->addWidget(m_view, 1);
  browserLayout->addWidget(m_scrollBar, 0);

  auto* browser = new QWidget{};
  browser->setLayout(browserLayout);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(controls, 0);
  layout->addWidget(browser, 1);
  setLayout(layout);

  m_fileSystemWatcher = new QFileSystemWatcher{this};
  m_rescanTimer = new QTimer{this};
  m_rescanTimer->setSingleShot(true);
}

void ModelBrowser::bindEvents()
{
  connect(m_folderEdit, &QLineEdit::editingFinished, this, [&]() {
    setFolderPath(std::filesystem::path{m_folderEdit->text().toStdString()});
  });

  connect(m_reloadButton, &QPushButton::clicked, this, [&]() { reloadModels(); });

  connect(m_browseButton, &QPushButton::clicked, this, [&]() {
    const auto* game = m_map.game();
    const auto startDir =
      game ? io::pathAsQString(game->gamePath()) : QString{};
    const auto folder = QFileDialog::getExistingDirectory(this, tr("Select Model Folder"), startDir);
    if (folder.isEmpty())
    {
      return;
    }

    if (!game)
    {
      setFolderPath(std::filesystem::path{folder.toStdString()});
      return;
    }

    const auto gamePath = io::Disk::fixPath(game->gamePath());
    const auto defaultSearchRoot =
      io::Disk::fixPath(gamePath / game->config().fileSystemConfig.searchPath);
    const auto absFolder = io::Disk::fixPath(std::filesystem::path{folder.toStdString()});

    if (kdl::path_has_prefix(absFolder, defaultSearchRoot))
    {
      const auto rel = absFolder.lexically_relative(defaultSearchRoot);
      setFolderPath(rel);
    }
    else
    {
      setFolderPath(absFolder);
    }
  });

  connect(
    m_fileSystemWatcher,
    &QFileSystemWatcher::directoryChanged,
    this,
    [&]() { scheduleRescan(); });

  connect(m_rescanTimer, &QTimer::timeout, this, [&]() { rescanWatchedDirectory(); });
}

void ModelBrowser::setFolderPath(std::filesystem::path folderPath)
{
  folderPath = folderPath.lexically_normal();
  if (m_folderPath == folderPath)
  {
    return;
  }

  m_folderPath = std::move(folderPath);
  m_folderEdit->setText(QString::fromStdString(m_folderPath.generic_string()));

  reloadModels();
  setWatchedDirectory();
}

void ModelBrowser::reloadModels()
{
  const auto* game = m_map.game();
  if (!game)
  {
    m_modelPaths.clear();
    m_lastWriteTimes.clear();
    m_view->setModelPaths({});
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
      m_view->setModelPaths({});
      return;
    }

    auto modelPaths = std::move(pathsResult.value());
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
    m_view->setModelPaths(m_modelPaths);
    return;
  }

  const auto& fs = game->gameFileSystem();
  auto pathsResult =
    fs.find(m_folderPath, io::TraversalMode::Recursive, io::makeExtensionPathMatcher({".mdl"}));
  if (pathsResult.is_error())
  {
    m_modelPaths.clear();
    m_lastWriteTimes.clear();
    m_view->setModelPaths({});
    return;
  }

  auto modelPaths = std::move(pathsResult.value());
  std::ranges::sort(modelPaths);

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

  m_view->setModelPaths(m_modelPaths);
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
      m_view->setModelPaths({});
      return;
    }

    auto modelPaths = std::move(pathsResult.value());
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
    m_view->setModelPaths(m_modelPaths);
    return;
  }

  const auto& fs = game->gameFileSystem();

  auto pathsResult =
    fs.find(m_folderPath, io::TraversalMode::Recursive, io::makeExtensionPathMatcher({".mdl"}));
  if (pathsResult.is_error())
  {
    m_modelPaths.clear();
    m_lastWriteTimes.clear();
    m_view->setModelPaths({});
    return;
  }

  auto modelPaths = std::move(pathsResult.value());
  std::ranges::sort(modelPaths);

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
  m_view->setModelPaths(m_modelPaths);
}

} // namespace tb::ui
