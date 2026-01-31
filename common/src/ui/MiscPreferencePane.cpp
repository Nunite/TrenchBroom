#include "MiscPreferencePane.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QPushButton>
#include <QMenu>
#include <QAction>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QListWidget>
#include <QWidgetAction>
#include <QLineEdit>
#include <QFileDialog>
#include <QHBoxLayout>

#include <functional>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "io/PathQt.h"
#include "ui/Actions.h"
#include "ui/QtUtils.h"

namespace tb::ui
{

MiscPreferencePane::MiscPreferencePane(QWidget* parent)
  : PreferencePane{parent}
{
  createGui();
  updateControls();
}

void MiscPreferencePane::createGui()
{
  auto* langLabel = new QLabel(tr("UI Language"));
  langLabel->setToolTip(tr("Select the display language for the application interface. Changes will take effect after restarting the application"));

  m_englishRadioButton = new QRadioButton(Preferences::languageEnglish());
  m_chineseRadioButton = new QRadioButton(Preferences::languageChinese());

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

  auto* languageLayout = new QVBoxLayout();
  languageLayout->setContentsMargins(0, 0, 0, 0);
  languageLayout->addWidget(langLabel);
  languageLayout->addWidget(m_englishRadioButton);
  languageLayout->addWidget(m_chineseRadioButton);

  auto* languageGroupBox = new QGroupBox(tr("Language"));
  languageGroupBox->setLayout(languageLayout);

  m_pluginList = new QListWidget();
  m_pluginList->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_pluginList->setMinimumHeight(100);

  m_addPluginBtn = new QPushButton(tr("Add..."));
  m_removePluginBtn = new QPushButton(tr("Remove"));
  m_clearPluginsBtn = new QPushButton(tr("Clear"));

  connect(m_addPluginBtn, &QPushButton::clicked, this, [this]() {
      const auto pathStr = QFileDialog::getOpenFileName(
          this,
          tr("Select Default Plugin"),
          fileDialogDefaultDirectory(FileDialogDir::Map),
          tr("Python Scripts (*.py);;All Files (*)"));
          
      if (!pathStr.isEmpty()) {
          updateFileDialogDefaultDirectoryWithFilename(FileDialogDir::Map, pathStr);
          addPluginPath(pathStr);
      }
  });

  connect(m_removePluginBtn, &QPushButton::clicked, this, [this]() {
      auto items = m_pluginList->selectedItems();
      for (auto* item : items) {
          delete m_pluginList->takeItem(m_pluginList->row(item));
      }
      savePluginPaths();
  });

  connect(m_clearPluginsBtn, &QPushButton::clicked, this, [this]() {
      m_pluginList->clear();
      savePluginPaths();
  });

  auto* pluginBtnLayout = new QVBoxLayout();
  pluginBtnLayout->addWidget(m_addPluginBtn);
  pluginBtnLayout->addWidget(m_removePluginBtn);
  pluginBtnLayout->addWidget(m_clearPluginsBtn);
  pluginBtnLayout->addStretch();

  auto* pluginListLayout = new QHBoxLayout();
  pluginListLayout->addWidget(m_pluginList);
  pluginListLayout->addLayout(pluginBtnLayout);

  auto* pluginGroupBox = new QGroupBox(tr("Default Plugins"));
  pluginGroupBox->setLayout(pluginListLayout);

  m_prefixWorldspawnOnCopyCheckBox =
    new QCheckBox(tr("Prefix worldspawn header on copy"));

  m_pieMenuActionList = new QListWidget();
  m_pieMenuActionList->setSelectionMode(QAbstractItemView::SingleSelection);
  m_pieMenuActionList->setDragDropMode(QAbstractItemView::InternalMove);
  m_pieMenuActionList->setDefaultDropAction(Qt::MoveAction);
  
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
      if (!items.isEmpty()) {
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
  for (const auto& [path, action] : actionManager.actionsMap()) {
      if (!action.label().isEmpty()) {
          sortedActions[path] = &action;
      }
  }

  struct MenuNode {
      std::map<std::string, MenuNode> subMenus;
      std::vector<std::pair<QString, QString>> actions;
  };

  MenuNode root;
  for (const auto& [path, action] : sortedActions) {
      MenuNode* currentNode = &root;
      auto it = path.begin();
      auto end = path.end();
      
      if (it == end) continue;

      for (; it != end; ++it) {
          auto nextIt = it;
          ++nextIt;
          bool isLeaf = (nextIt == end);
          
          std::string component = it->string();
          
          if (isLeaf) {
              currentNode->actions.emplace_back(action->label(), QString::fromStdString(path.string()));
          } else {
              currentNode = &currentNode->subMenus[component];
          }
      }
  }

  // Recursive lambda to build menu
  std::function<void(QMenu*, const MenuNode&)> buildMenu = 
      [&](QMenu* menu, const MenuNode& node) {
      
      // Add Submenus first
      for (const auto& [name, subNode] : node.subMenus) {
          QMenu* subMenu = menu->addMenu(QString::fromStdString(name));
          buildMenu(subMenu, subNode);
      }

      // Add Actions
      if (node.actions.size() <= 15) {
          for (const auto& [label, path] : node.actions) {
              QAction* qAction = menu->addAction(label);
              qAction->setData(path);
              connect(qAction, &QAction::triggered, this, [this, qAction]() {
                  addPieMenuAction(qAction->text(), qAction->data().toString());
              });
          }
      } else {
          // Use QListWidget for large lists to support scrolling
          auto* listWidget = new QListWidget();
          listWidget->setFrameShape(QFrame::NoFrame);
          listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
          
          for (const auto& [label, path] : node.actions) {
              QListWidgetItem* item = new QListWidgetItem(label, listWidget);
              item->setData(Qt::UserRole, path);
          }
          
          // Calculate height
          int rowHeight = listWidget->sizeHintForRow(0);
          if (rowHeight <= 0) rowHeight = 25;
          int totalHeight = static_cast<int>(node.actions.size()) * rowHeight + 2 * listWidget->frameWidth(); 
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

  auto* pieMenuLabel = new QLabel(tr("Pie Menu Actions"));
  auto* buttonsLayout = new QVBoxLayout();
  buttonsLayout->addWidget(m_addActionBtn);
  buttonsLayout->addWidget(m_removeActionBtn);
  buttonsLayout->addWidget(m_clearActionsBtn);
  buttonsLayout->addStretch();

  auto* pieMenuLayout = new QHBoxLayout();
  pieMenuLayout->addWidget(m_pieMenuActionList);
  pieMenuLayout->addLayout(buttonsLayout);

  auto* mainPieLayout = new QVBoxLayout();
  mainPieLayout->addWidget(pieMenuLabel);
  mainPieLayout->addLayout(pieMenuLayout);

  auto* miscLayout = new QVBoxLayout();
  miscLayout->setContentsMargins(0, 0, 0, 0);
  miscLayout->addWidget(pluginGroupBox);
  miscLayout->addWidget(m_prefixWorldspawnOnCopyCheckBox);
  miscLayout->addLayout(mainPieLayout);

  auto* miscGroupBox = new QGroupBox(tr("Editor"));
  miscGroupBox->setLayout(miscLayout);

  auto* layout = new QVBoxLayout();
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(languageGroupBox);
  layout->addWidget(miscGroupBox);
  layout->addStretch(1);
  setLayout(layout);

  connect(
    m_prefixWorldspawnOnCopyCheckBox,
    &QCheckBox::toggled,
    this,
    [](const bool checked) {
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
    for (int i = 0; i < m_pieMenuActionList->count(); ++i) {
        paths << m_pieMenuActionList->item(i)->data(Qt::UserRole).toString();
    }
    auto& prefs = PreferenceManager::instance();
    prefs.set(Preferences::PieMenuAction, paths.join('|'));
}

void MiscPreferencePane::savePluginPaths()
{
    QStringList paths;
    for (int i = 0; i < m_pluginList->count(); ++i) {
        paths << m_pluginList->item(i)->text();
    }
    auto& prefs = PreferenceManager::instance();
    prefs.set(Preferences::DefaultPluginPaths, paths.join('|'));
}

void MiscPreferencePane::addPluginPath(const QString& path)
{
    new QListWidgetItem(path, m_pluginList);
    savePluginPaths();
}

bool MiscPreferencePane::canResetToDefaults()
{
  return true;
}

void MiscPreferencePane::doResetToDefaults()
{
  auto& prefs = PreferenceManager::instance();
  prefs.resetToDefault(Preferences::Language);
  prefs.resetToDefault(Preferences::DefaultPluginPaths);
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
  QString currentPaths = pref(Preferences::DefaultPluginPaths);
  QStringList pathsList = currentPaths.split('|', Qt::SkipEmptyParts);
  for (const auto& path : pathsList) {
      new QListWidgetItem(path, m_pluginList);
  }

  m_prefixWorldspawnOnCopyCheckBox->setChecked(
    pref(Preferences::PrefixWorldspawnHeaderOnCopy));

  QString currentPath = pref(Preferences::PieMenuAction);
  QStringList paths = currentPath.split('|', Qt::SkipEmptyParts);
  
  m_pieMenuActionList->clear();
  
  auto& actionManager = ActionManager::instance();
  const auto& actions = actionManager.actionsMap();
  
  for (const auto& pathStr : paths) {
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
    tr("Language settings will take effect after restarting the application"),
    QMessageBox::Ok);
}

} // namespace tb::ui
