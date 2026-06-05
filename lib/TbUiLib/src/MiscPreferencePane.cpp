#include "ui/MiscPreferencePane.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QFileDialog>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "ui/Action.h"
#include "ui/ActionManager.h"
#include "ui/FileDialogDefaultDir.h"
#include "ui/QPathUtils.h"
#include "ui/QStyleUtils.h"
#include "ui/ViewConstants.h"
#include "ui/python/PythonPluginManager.h"
#include "ui/python/PythonPluginManifest.h"

#include <functional>
#include <sstream>

namespace tb::ui
{
namespace
{
constexpr auto ListMinHeight = 96;
constexpr auto PluginStatusMinHeight = 110;
constexpr auto ButtonColumnWidth = 86;

void configurePreferenceList(QListWidget* list)
{
  list->setMinimumHeight(ListMinHeight);
  list->setAlternatingRowColors(true);
}

void configureActionButton(QPushButton* button)
{
  button->setMinimumWidth(ButtonColumnWidth);
}

QVBoxLayout* createButtonColumn(std::initializer_list<QPushButton*> buttons)
{
  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(LayoutConstants::MediumVMargin);

  for (auto* button : buttons)
  {
    configureActionButton(button);
    layout->addWidget(button);
  }

  layout->addStretch(1);
  return layout;
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

QWidget* createTabPage(QLayout* contentLayout)
{
  auto* page = new QWidget{};
  contentLayout->setContentsMargins(
    LayoutConstants::WideHMargin,
    LayoutConstants::WideVMargin,
    LayoutConstants::WideHMargin,
    LayoutConstants::WideVMargin);
  contentLayout->setSpacing(LayoutConstants::WideVMargin);
  page->setLayout(contentLayout);
  return page;
}

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
} // namespace

MiscPreferencePane::MiscPreferencePane(QWidget* parent)
  : PreferencePane{parent}
{
  createGui();
  updateControls();
}

void MiscPreferencePane::createGui()
{
  auto* langLabel = new QLabel(tr("UI Language"));
  setInfoStyle(langLabel);
  langLabel->setToolTip(
    tr("Select the display language for the application interface. Changes will take "
       "effect after restarting the application"));

  m_englishRadioButton =
    new QRadioButton(QString::fromStdString(Preferences::languageEnglish()));
  m_chineseRadioButton =
    new QRadioButton(QString::fromStdString(Preferences::languageChinese()));

  m_languageButtonGroup = new QButtonGroup(this);
  m_languageButtonGroup->addButton(m_englishRadioButton, 0);
  m_languageButtonGroup->addButton(m_chineseRadioButton, 1);

  connect(m_englishRadioButton, &QRadioButton::clicked, this, [this]() {
    auto& prefs = PreferenceManager::instance();
    prefs.set(Preferences::Language, Preferences::languageEnglish());
    emit languageChanged();
    showRestartRequiredMessage();
  });

  connect(m_chineseRadioButton, &QRadioButton::clicked, this, [this]() {
    auto& prefs = PreferenceManager::instance();
    prefs.set(Preferences::Language, Preferences::languageChinese());
    emit languageChanged();
    showRestartRequiredMessage();
  });

  auto* languageChoicesLayout = new QHBoxLayout();
  languageChoicesLayout->setContentsMargins(0, 0, 0, 0);
  languageChoicesLayout->setSpacing(LayoutConstants::WideHMargin);
  languageChoicesLayout->addWidget(m_englishRadioButton);
  languageChoicesLayout->addWidget(m_chineseRadioButton);
  languageChoicesLayout->addStretch(1);

  auto* languageLayout = new QVBoxLayout();
  languageLayout->addWidget(langLabel);
  languageLayout->addLayout(languageChoicesLayout);

  auto* languageGroupBox = createGroupBox(tr("Language"), languageLayout);

  m_pluginList = new QListWidget();
  m_pluginList->setSelectionMode(QAbstractItemView::ExtendedSelection);
  configurePreferenceList(m_pluginList);

  m_addPluginBtn = new QPushButton(tr("Add..."));
  m_removePluginBtn = new QPushButton(tr("Remove"));
  m_clearPluginsBtn = new QPushButton(tr("Clear"));
  m_reloadPluginsBtn = new QPushButton(tr("Reload"));

  connect(m_addPluginBtn, &QPushButton::clicked, this, [this]() {
    const auto pathStr = QFileDialog::getExistingDirectory(
      this,
      tr("Select Python Plugin Directory"),
      fileDialogDefaultDirectory(FileDialogDir::Map));

    if (!pathStr.isEmpty())
    {
      updateFileDialogDefaultDirectoryWithDirectory(FileDialogDir::Map, pathStr);
      addPluginPath(pathStr);
    }
  });

  connect(m_removePluginBtn, &QPushButton::clicked, this, [this]() {
    auto items = m_pluginList->selectedItems();
    for (auto* item : items)
    {
      delete m_pluginList->takeItem(m_pluginList->row(item));
    }
    savePluginPaths();
  });

  connect(m_clearPluginsBtn, &QPushButton::clicked, this, [this]() {
    m_pluginList->clear();
    savePluginPaths();
  });

  connect(m_reloadPluginsBtn, &QPushButton::clicked, this, [this]() {
    savePluginPaths();
    reloadPluginStatus();
  });

  auto* pluginBtnLayout = createButtonColumn(
    {m_addPluginBtn, m_removePluginBtn, m_clearPluginsBtn, m_reloadPluginsBtn});

  auto* pluginListLayout = new QHBoxLayout();
  pluginListLayout->setContentsMargins(0, 0, 0, 0);
  pluginListLayout->setSpacing(LayoutConstants::WideHMargin);
  pluginListLayout->addWidget(m_pluginList, 1);
  pluginListLayout->addLayout(pluginBtnLayout);

  auto* statusLabel = new QLabel{tr("Detected plugins")};
  setInfoStyle(statusLabel);

  m_pluginStatusList = new QListWidget();
  m_pluginStatusList->setMinimumHeight(PluginStatusMinHeight);
  m_pluginStatusList->setAlternatingRowColors(true);
  m_pluginStatusList->setSelectionMode(QAbstractItemView::SingleSelection);

  m_pluginDetails = new QTextEdit();
  m_pluginDetails->setReadOnly(true);
  m_pluginDetails->setMinimumHeight(PluginStatusMinHeight);

  connect(m_pluginStatusList, &QListWidget::currentItemChanged, this, [this]() {
    updatePluginDetails();
  });

  auto* pluginDirectoriesLayout = new QVBoxLayout();
  pluginDirectoriesLayout->addLayout(pluginListLayout);

  auto* pluginStatusLayout = new QVBoxLayout();
  pluginStatusLayout->addWidget(statusLabel);
  pluginStatusLayout->addWidget(m_pluginStatusList, 1);
  pluginStatusLayout->addWidget(m_pluginDetails, 1);

  auto* pluginColumnsLayout = new QHBoxLayout();
  pluginColumnsLayout->setContentsMargins(0, 0, 0, 0);
  pluginColumnsLayout->setSpacing(LayoutConstants::WideHMargin);
  pluginColumnsLayout->addWidget(
    createGroupBox(tr("Directories"), pluginDirectoriesLayout), 1);
  pluginColumnsLayout->addWidget(
    createGroupBox(tr("Detected Plugins"), pluginStatusLayout), 1);

  m_prefixWorldspawnOnCopyCheckBox =
    new QCheckBox(tr("Prefix worldspawn header on copy"));

  m_pieMenuActionList = new QListWidget();
  m_pieMenuActionList->setSelectionMode(QAbstractItemView::SingleSelection);
  m_pieMenuActionList->setDragDropMode(QAbstractItemView::InternalMove);
  m_pieMenuActionList->setDefaultDropAction(Qt::MoveAction);
  configurePreferenceList(m_pieMenuActionList);

  m_addActionBtn = new QPushButton(tr("Add"));
  m_removeActionBtn = new QPushButton(tr("Remove"));
  m_clearActionsBtn = new QPushButton(tr("Clear"));

  m_pieMenu = new QMenu(this);

  connect(m_addActionBtn, &QPushButton::clicked, this, [this]() {
    QPoint pos = m_addActionBtn->mapToGlobal(QPoint(0, m_addActionBtn->height()));
    m_pieMenu->exec(pos);
  });

  connect(m_removeActionBtn, &QPushButton::clicked, this, [this]() {
    QList<QListWidgetItem*> items = m_pieMenuActionList->selectedItems();
    if (!items.isEmpty())
    {
      delete m_pieMenuActionList->takeItem(m_pieMenuActionList->row(items.first()));
      savePieMenuActions();
    }
  });

  connect(m_clearActionsBtn, &QPushButton::clicked, this, [this]() {
    m_pieMenuActionList->clear();
    savePieMenuActions();
  });

  // Save on reorder
  connect(m_pieMenuActionList->model(), &QAbstractItemModel::rowsMoved, this, [this]() {
    savePieMenuActions();
  });

  auto& actionManager = ActionManager::instance();
  // Sort actions by path for consistent menu structure
  std::map<std::filesystem::path, const Action*> sortedActions;
  for (const auto& [path, action] : actionManager.actionsMap())
  {
    if (!action.label().isEmpty())
    {
      sortedActions[path] = &action;
    }
  }

  struct MenuNode
  {
    std::map<std::string, MenuNode> subMenus;
    std::vector<std::pair<QString, QString>> actions;
  };

  MenuNode root;
  for (const auto& [path, action] : sortedActions)
  {
    MenuNode* currentNode = &root;
    auto it = path.begin();
    auto end = path.end();

    if (it == end)
      continue;

    for (; it != end; ++it)
    {
      auto nextIt = it;
      ++nextIt;
      bool isLeaf = (nextIt == end);

      std::string component = it->string();

      if (isLeaf)
      {
        currentNode->actions.emplace_back(
          action->label(), QString::fromStdString(path.string()));
      }
      else
      {
        currentNode = &currentNode->subMenus[component];
      }
    }
  }

  // Recursive lambda to build menu
  std::function<void(QMenu*, const MenuNode&)> buildMenu = [&](
                                                             QMenu* menu,
                                                             const MenuNode& node) {
    // Add Submenus first
    for (const auto& [name, subNode] : node.subMenus)
    {
      QMenu* subMenu = menu->addMenu(QString::fromStdString(name));
      buildMenu(subMenu, subNode);
    }

    // Add Actions
    if (node.actions.size() <= 15)
    {
      for (const auto& [label, path] : node.actions)
      {
        QAction* qAction = menu->addAction(label);
        qAction->setData(path);
        connect(qAction, &QAction::triggered, this, [this, qAction]() {
          addPieMenuAction(qAction->text(), qAction->data().toString());
        });
      }
    }
    else
    {
      // Use QListWidget for large lists to support scrolling
      auto* listWidget = new QListWidget();
      listWidget->setFrameShape(QFrame::NoFrame);
      listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

      for (const auto& [label, path] : node.actions)
      {
        QListWidgetItem* item = new QListWidgetItem(label, listWidget);
        item->setData(Qt::UserRole, path);
      }

      // Calculate height
      int rowHeight = listWidget->sizeHintForRow(0);
      if (rowHeight <= 0)
        rowHeight = 25;
      int totalHeight =
        static_cast<int>(node.actions.size()) * rowHeight + 2 * listWidget->frameWidth();
      int maxHeight = 300;

      listWidget->setMinimumWidth(250);
      listWidget->setFixedHeight(std::min(totalHeight, maxHeight));

      connect(listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        addPieMenuAction(item->text(), item->data(Qt::UserRole).toString());
        m_pieMenu->close();
      });

      QWidgetAction* widgetAction = new QWidgetAction(menu);
      widgetAction->setDefaultWidget(listWidget);
      menu->addAction(widgetAction);
    }
  };

  buildMenu(m_pieMenu, root);

  auto* buttonsLayout =
    createButtonColumn({m_addActionBtn, m_removeActionBtn, m_clearActionsBtn});

  auto* pieMenuLayout = new QHBoxLayout();
  pieMenuLayout->setContentsMargins(0, 0, 0, 0);
  pieMenuLayout->setSpacing(LayoutConstants::WideHMargin);
  pieMenuLayout->addWidget(m_pieMenuActionList, 1);
  pieMenuLayout->addLayout(buttonsLayout);

  auto* editorLayout = new QVBoxLayout();
  editorLayout->addWidget(m_prefixWorldspawnOnCopyCheckBox);
  auto* editorGroupBox = createGroupBox(tr("Editor"), editorLayout);

  auto* generalLayout = new QVBoxLayout();
  generalLayout->addWidget(languageGroupBox);
  generalLayout->addWidget(editorGroupBox);
  generalLayout->addStretch(1);

  auto* pluginPageLayout = new QVBoxLayout();
  pluginPageLayout->addLayout(pluginColumnsLayout, 1);

  auto* pieMenuPageLayout = new QVBoxLayout();
  pieMenuPageLayout->addWidget(createGroupBox(tr("Pie Menu Actions"), pieMenuLayout), 1);

  auto* tabs = new QTabWidget{};
  tabs->addTab(createTabPage(generalLayout), tr("General"));
  tabs->addTab(createTabPage(pluginPageLayout), tr("Python Plugins"));
  tabs->addTab(createTabPage(pieMenuPageLayout), tr("Pie Menu"));

  auto* layout = new QVBoxLayout();
  layout->setContentsMargins(
    LayoutConstants::WideHMargin,
    LayoutConstants::WideVMargin,
    LayoutConstants::WideHMargin,
    LayoutConstants::WideVMargin);
  layout->setSpacing(LayoutConstants::WideVMargin);
  layout->addWidget(tabs, 1);
  setLayout(layout);

  connect(
    m_prefixWorldspawnOnCopyCheckBox, &QCheckBox::toggled, this, [](const bool checked) {
      auto& prefs = PreferenceManager::instance();
      prefs.set(Preferences::PrefixWorldspawnHeaderOnCopy, checked);
    });
}

void MiscPreferencePane::addPieMenuAction(const QString& label, const QString& path)
{
  auto* item = new QListWidgetItem(label, m_pieMenuActionList);
  item->setData(Qt::UserRole, path);
  savePieMenuActions();
}

void MiscPreferencePane::savePieMenuActions()
{
  QStringList paths;
  for (int i = 0; i < m_pieMenuActionList->count(); ++i)
  {
    paths << m_pieMenuActionList->item(i)->data(Qt::UserRole).toString();
  }
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::PieMenuAction, paths.join('|').toStdString());
}

void MiscPreferencePane::savePluginPaths()
{
  QStringList paths;
  for (int i = 0; i < m_pluginList->count(); ++i)
  {
    paths << m_pluginList->item(i)->text();
  }
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::PythonPluginDirectories, paths.join('|').toStdString());
  reloadPluginStatus();
}

void MiscPreferencePane::addPluginPath(const QString& path)
{
  new QListWidgetItem(path, m_pluginList);
  savePluginPaths();
}

void MiscPreferencePane::reloadPluginStatus()
{
  if (m_pluginStatusList == nullptr || m_pluginDetails == nullptr)
  {
    return;
  }

  m_pluginStatusList->clear();
  m_pluginDetails->clear();

  QStringList paths;
  for (int i = 0; i < m_pluginList->count(); ++i)
  {
    paths << m_pluginList->item(i)->text();
  }

  auto manager = PythonPluginManager{};
  manager.reload(splitPythonPluginDirectories(paths.join('|').toStdString()));

  for (const auto& plugin : manager.plugins())
  {
    const auto title = tr("%1  %2  (%3)")
                         .arg(QString::fromStdString(plugin.manifest.name))
                         .arg(QString::fromStdString(plugin.manifest.version))
                         .arg(pluginStatusText(plugin.status));
    auto* item = new QListWidgetItem{title, m_pluginStatusList};
    item->setData(Qt::UserRole, QString::fromStdString(plugin.manifest.id));
    item->setData(Qt::UserRole + 1, QString::fromStdString(plugin.manifest.name));
    item->setData(Qt::UserRole + 2, QString::fromStdString(plugin.manifest.version));
    item->setData(Qt::UserRole + 3, pathAsQString(plugin.manifest.directory));
    item->setData(Qt::UserRole + 4, pathAsQString(plugin.manifest.entry));
    item->setData(Qt::UserRole + 5, pluginStatusText(plugin.status));
    item->setData(Qt::UserRole + 6, QString::fromStdString(plugin.error));
  }

  for (const auto& error : manager.errors())
  {
    auto* item = new QListWidgetItem{
      tr("Manifest error  (%1)").arg(tr("Failed")), m_pluginStatusList};
    item->setData(Qt::UserRole, QString{});
    item->setData(Qt::UserRole + 1, tr("Manifest error"));
    item->setData(Qt::UserRole + 2, QString{});
    item->setData(Qt::UserRole + 3, pathAsQString(error.path));
    item->setData(Qt::UserRole + 4, QString{});
    item->setData(Qt::UserRole + 5, tr("Failed"));
    item->setData(Qt::UserRole + 6, QString::fromStdString(error.message));
  }

  if (m_pluginStatusList->count() > 0)
  {
    m_pluginStatusList->setCurrentRow(0);
  }
  else
  {
    m_pluginDetails->setPlainText(
      tr("No plugin manifests found in the configured directories."));
  }
}

void MiscPreferencePane::updatePluginDetails()
{
  if (m_pluginStatusList == nullptr || m_pluginDetails == nullptr)
  {
    return;
  }

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

  auto details = QString{};
  details += tr("Name: %1\n").arg(name);
  if (!id.isEmpty())
  {
    details += tr("ID: %1\n").arg(id);
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

bool MiscPreferencePane::canResetToDefaults()
{
  return true;
}

void MiscPreferencePane::doResetToDefaults()
{
  auto& prefs = PreferenceManager::instance();
  prefs.resetToDefault(Preferences::Language);
  prefs.resetToDefault(Preferences::PythonPluginDirectories);
  prefs.resetToDefault(Preferences::PrefixWorldspawnHeaderOnCopy);
  prefs.resetToDefault(Preferences::PieMenuAction);

  updateControls();
}

void MiscPreferencePane::updateControls()
{
  auto& prefs = PreferenceManager::instance();
  const auto& language = prefs.get(Preferences::Language);

  if (language == Preferences::languageEnglish())
  {
    m_englishRadioButton->setChecked(true);
  }
  else
  {
    m_chineseRadioButton->setChecked(true);
  }

  m_pluginList->clear();
  auto currentPaths = QString::fromStdString(pref(Preferences::PythonPluginDirectories));
  QStringList pathsList = currentPaths.split('|', Qt::SkipEmptyParts);
  for (const auto& path : pathsList)
  {
    new QListWidgetItem(path, m_pluginList);
  }
  reloadPluginStatus();

  m_prefixWorldspawnOnCopyCheckBox->setChecked(
    pref(Preferences::PrefixWorldspawnHeaderOnCopy));

  auto currentPath = QString::fromStdString(pref(Preferences::PieMenuAction));
  QStringList paths = currentPath.split('|', Qt::SkipEmptyParts);

  m_pieMenuActionList->clear();

  auto& actionManager = ActionManager::instance();
  const auto& actions = actionManager.actionsMap();

  for (const auto& pathStr : paths)
  {
    std::filesystem::path path(pathStr.toStdString());
    auto it = actions.find(path);
    QString label = (it != actions.end()) ? it->second.label() : tr("Unknown Action");

    auto* item = new QListWidgetItem(label, m_pieMenuActionList);
    item->setData(Qt::UserRole, pathStr);
  }
}

bool MiscPreferencePane::validate()
{
  return true;
}

void MiscPreferencePane::showRestartRequiredMessage()
{
  QMessageBox::information(
    this,
    tr("Restart Required"),
    tr("Language settings will take effect after applying preferences and restarting the "
       "application"),
    QMessageBox::Ok);
}

} // namespace tb::ui
