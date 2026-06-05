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

#include "ui/PythonPluginManagerDialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "ui/FileDialogDefaultDir.h"
#include "ui/QPathUtils.h"
#include "ui/QStyleUtils.h"
#include "ui/ViewConstants.h"
#include "ui/python/PythonPluginManager.h"

namespace tb::ui
{
namespace
{
constexpr auto DialogMinWidth = 860;
constexpr auto DialogMinHeight = 520;
constexpr auto ButtonColumnWidth = 92;

QString pluginStatusText(const PythonPluginStatus status)
{
  switch (status)
  {
  case PythonPluginStatus::NotLoaded:
    return QObject::tr("Ready");
  case PythonPluginStatus::Loaded:
    return QObject::tr("Loaded");
  case PythonPluginStatus::Failed:
    return QObject::tr("Failed");
  }
  return QObject::tr("Unknown");
}

void configureList(QListWidget* list)
{
  list->setAlternatingRowColors(true);
  list->setSelectionMode(QAbstractItemView::SingleSelection);
}

void configureActionButton(QPushButton* button)
{
  button->setMinimumWidth(ButtonColumnWidth);
}

QGroupBox* createGroupBox(const QString& title, QLayout* contentLayout)
{
  auto* groupBox = new QGroupBox{title};
  contentLayout->setContentsMargins(
    LayoutConstants::WideHMargin,
    LayoutConstants::WideVMargin,
    LayoutConstants::WideHMargin,
    LayoutConstants::WideVMargin);
  contentLayout->setSpacing(LayoutConstants::WideVMargin);
  groupBox->setLayout(contentLayout);
  return groupBox;
}
} // namespace

PythonPluginManagerDialog::PythonPluginManagerDialog(QWidget* parent)
  : QDialog{parent}
{
  setWindowTitle(tr("Python Plugin Manager"));
  setMinimumSize(DialogMinWidth, DialogMinHeight);
  createGui();
  loadPluginPaths();
  reloadPluginStatus();
}

void PythonPluginManagerDialog::createGui()
{
  auto* introLabel = new QLabel{tr("Manage Python v2 manifest plugin directories.")};
  setInfoStyle(introLabel);

  m_pluginPathList = new QListWidget{};
  configureList(m_pluginPathList);
  m_pluginPathList->setSelectionMode(QAbstractItemView::ExtendedSelection);

  m_installButton = new QPushButton{tr("Install...")};
  m_removeButton = new QPushButton{tr("Remove")};
  m_clearButton = new QPushButton{tr("Clear")};
  m_refreshButton = new QPushButton{tr("Refresh")};

  for (auto* button : {m_installButton, m_removeButton, m_clearButton, m_refreshButton})
  {
    configureActionButton(button);
  }

  connect(m_installButton, &QPushButton::clicked, this, [this]() {
    const auto pathStr = QFileDialog::getExistingDirectory(
      this,
      tr("Install Python Plugin from Directory"),
      fileDialogDefaultDirectory(FileDialogDir::Map));

    if (!pathStr.isEmpty())
    {
      updateFileDialogDefaultDirectoryWithDirectory(FileDialogDir::Map, pathStr);
      addPluginPath(pathStr);
    }
  });

  connect(m_removeButton, &QPushButton::clicked, this, [this]() {
    const auto items = m_pluginPathList->selectedItems();
    for (auto* item : items)
    {
      delete m_pluginPathList->takeItem(m_pluginPathList->row(item));
    }
    savePluginPaths();
    reloadPluginStatus();
  });

  connect(m_clearButton, &QPushButton::clicked, this, [this]() {
    m_pluginPathList->clear();
    savePluginPaths();
    reloadPluginStatus();
  });

  connect(m_refreshButton, &QPushButton::clicked, this, [this]() {
    savePluginPaths();
    reloadPluginStatus();
  });

  auto* pathButtonLayout = new QVBoxLayout{};
  pathButtonLayout->setContentsMargins(0, 0, 0, 0);
  pathButtonLayout->setSpacing(LayoutConstants::MediumVMargin);
  pathButtonLayout->addWidget(m_installButton);
  pathButtonLayout->addWidget(m_removeButton);
  pathButtonLayout->addWidget(m_clearButton);
  pathButtonLayout->addWidget(m_refreshButton);
  pathButtonLayout->addStretch(1);

  auto* pathLayout = new QHBoxLayout{};
  pathLayout->setContentsMargins(0, 0, 0, 0);
  pathLayout->setSpacing(LayoutConstants::WideHMargin);
  pathLayout->addWidget(m_pluginPathList, 1);
  pathLayout->addLayout(pathButtonLayout);

  m_searchBox = new QLineEdit{};
  m_searchBox->setPlaceholderText(tr("Search plugins"));
  m_showIssuesOnlyCheckBox = new QCheckBox{tr("Only show issues")};

  connect(m_searchBox, &QLineEdit::textChanged, this, [this]() { reloadPluginStatus(); });
  connect(m_showIssuesOnlyCheckBox, &QCheckBox::toggled, this, [this]() {
    reloadPluginStatus();
  });

  auto* filterLayout = new QHBoxLayout{};
  filterLayout->setContentsMargins(0, 0, 0, 0);
  filterLayout->setSpacing(LayoutConstants::WideHMargin);
  filterLayout->addWidget(m_searchBox, 1);
  filterLayout->addWidget(m_showIssuesOnlyCheckBox);

  m_pluginStatusList = new QListWidget{};
  configureList(m_pluginStatusList);

  m_pluginDetails = new QTextEdit{};
  m_pluginDetails->setReadOnly(true);

  connect(m_pluginStatusList, &QListWidget::currentItemChanged, this, [this]() {
    updatePluginDetails();
  });

  auto* statusLayout = new QVBoxLayout{};
  statusLayout->addLayout(filterLayout);
  statusLayout->addWidget(m_pluginStatusList, 2);
  statusLayout->addWidget(m_pluginDetails, 1);

  auto* contentLayout = new QHBoxLayout{};
  contentLayout->setContentsMargins(0, 0, 0, 0);
  contentLayout->setSpacing(LayoutConstants::WideHMargin);
  contentLayout->addWidget(createGroupBox(tr("Plugin Directories"), pathLayout), 1);
  contentLayout->addWidget(createGroupBox(tr("Detected Plugins"), statusLayout), 1);

  auto* buttonBox = new QDialogButtonBox{QDialogButtonBox::Close};
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin);
  layout->setSpacing(LayoutConstants::WideVMargin);
  layout->addWidget(introLabel);
  layout->addLayout(contentLayout, 1);
  layout->addWidget(buttonBox);
  setLayout(layout);
}

void PythonPluginManagerDialog::loadPluginPaths()
{
  m_pluginPathList->clear();

  const auto paths = QString::fromStdString(pref(Preferences::PythonPluginDirectories))
                       .split('|', Qt::SkipEmptyParts);
  for (const auto& path : paths)
  {
    new QListWidgetItem{path, m_pluginPathList};
  }
}

void PythonPluginManagerDialog::addPluginPath(const QString& path)
{
  for (auto i = 0; i < m_pluginPathList->count(); ++i)
  {
    if (m_pluginPathList->item(i)->text() == path)
    {
      m_pluginPathList->setCurrentRow(i);
      return;
    }
  }

  new QListWidgetItem{path, m_pluginPathList};
  savePluginPaths();
  reloadPluginStatus();
}

void PythonPluginManagerDialog::savePluginPaths()
{
  auto paths = QStringList{};
  for (auto i = 0; i < m_pluginPathList->count(); ++i)
  {
    paths << m_pluginPathList->item(i)->text();
  }

  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::PythonPluginDirectories, paths.join('|').toStdString());
  prefs.saveChanges();
}

void PythonPluginManagerDialog::reloadPluginStatus()
{
  m_pluginStatusList->clear();
  m_pluginDetails->clear();

  auto paths = QStringList{};
  for (auto i = 0; i < m_pluginPathList->count(); ++i)
  {
    paths << m_pluginPathList->item(i)->text();
  }

  auto manager = PythonPluginManager{};
  manager.reload(splitPythonPluginDirectories(paths.join('|').toStdString()));

  const auto searchText = m_searchBox->text().trimmed().toLower();
  const auto showIssuesOnly = m_showIssuesOnlyCheckBox->isChecked();

  for (const auto& plugin : manager.plugins())
  {
    const auto name = QString::fromStdString(plugin.manifest.name);
    const auto id = QString::fromStdString(plugin.manifest.id);
    const auto description = QString::fromStdString(plugin.manifest.description);
    const auto author = QString::fromStdString(plugin.manifest.author);
    const auto version = QString::fromStdString(plugin.manifest.version);
    const auto directory = pathAsQString(plugin.manifest.directory);
    const auto entry = pathAsQString(plugin.manifest.entry);
    const auto status = pluginStatusText(plugin.status);
    const auto error = QString::fromStdString(plugin.error);
    const auto haystack =
      QStringList{name, id, description, author, version, directory, entry, status, error}
        .join(QLatin1Char(' '))
        .toLower();

    if (!searchText.isEmpty() && !haystack.contains(searchText))
    {
      continue;
    }
    if (showIssuesOnly && error.isEmpty())
    {
      continue;
    }

    const auto title = description.isEmpty()
                         ? tr("%1  %2  (%3)").arg(name, version, status)
                         : tr("%1  %2  (%3)\n%4").arg(name, version, status, description);
    auto* item = new QListWidgetItem{title, m_pluginStatusList};
    item->setData(Qt::UserRole, id);
    item->setData(Qt::UserRole + 1, name);
    item->setData(Qt::UserRole + 2, version);
    item->setData(Qt::UserRole + 3, directory);
    item->setData(Qt::UserRole + 4, entry);
    item->setData(Qt::UserRole + 5, status);
    item->setData(Qt::UserRole + 6, error);
    item->setData(Qt::UserRole + 7, description);
    item->setData(Qt::UserRole + 8, author);
  }

  for (const auto& error : manager.errors())
  {
    const auto path = pathAsQString(error.path);
    const auto message = QString::fromStdString(error.message);
    const auto haystack =
      QStringList{tr("Manifest error"), path, message}.join(QLatin1Char(' ')).toLower();

    if (!searchText.isEmpty() && !haystack.contains(searchText))
    {
      continue;
    }

    auto* item = new QListWidgetItem{
      tr("Manifest error  (%1)\n%2").arg(tr("Failed"), path), m_pluginStatusList};
    item->setData(Qt::UserRole, QString{});
    item->setData(Qt::UserRole + 1, tr("Manifest error"));
    item->setData(Qt::UserRole + 2, QString{});
    item->setData(Qt::UserRole + 3, path);
    item->setData(Qt::UserRole + 4, QString{});
    item->setData(Qt::UserRole + 5, tr("Failed"));
    item->setData(Qt::UserRole + 6, message);
    item->setData(Qt::UserRole + 7, QString{});
    item->setData(Qt::UserRole + 8, QString{});
  }

  if (m_pluginStatusList->count() > 0)
  {
    m_pluginStatusList->setCurrentRow(0);
  }
  else
  {
    m_pluginDetails->setPlainText(
      searchText.isEmpty() && !showIssuesOnly
        ? tr("No plugin manifests found in the configured directories.")
        : tr("No plugins match the current filter."));
  }
}

void PythonPluginManagerDialog::updatePluginDetails()
{
  auto* item = m_pluginStatusList->currentItem();
  if (item == nullptr)
  {
    m_pluginDetails->clear();
    return;
  }

  const auto id = item->data(Qt::UserRole).toString();
  const auto name = item->data(Qt::UserRole + 1).toString();
  const auto version = item->data(Qt::UserRole + 2).toString();
  const auto directory = item->data(Qt::UserRole + 3).toString();
  const auto entry = item->data(Qt::UserRole + 4).toString();
  const auto status = item->data(Qt::UserRole + 5).toString();
  const auto error = item->data(Qt::UserRole + 6).toString();
  const auto description = item->data(Qt::UserRole + 7).toString();
  const auto author = item->data(Qt::UserRole + 8).toString();

  auto details = QString{};
  details += tr("Name: %1\n").arg(name);
  if (!id.isEmpty())
  {
    details += tr("ID: %1\n").arg(id);
  }
  if (!description.isEmpty())
  {
    details += tr("Description: %1\n").arg(description);
  }
  if (!author.isEmpty())
  {
    details += tr("Author: %1\n").arg(author);
  }
  if (!version.isEmpty())
  {
    details += tr("Version: %1\n").arg(version);
  }
  details += tr("Status: %1\n").arg(status);
  details += tr("Path: %1\n").arg(directory);
  if (!entry.isEmpty())
  {
    details += tr("Entry: %1\n").arg(entry);
  }
  if (!error.isEmpty())
  {
    details += tr("\nError:\n%1").arg(error);
  }

  m_pluginDetails->setPlainText(details);
}

} // namespace tb::ui
