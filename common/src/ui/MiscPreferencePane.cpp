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

#include <functional>

#include "PreferenceManager.h"
#include "Preferences.h"
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

  m_prefixWorldspawnOnCopyCheckBox =
    new QCheckBox(tr("Prefix worldspawn header on copy"));

  m_pieMenuActionButton = new QPushButton(tr("Select Action..."));
  m_pieMenu = new QMenu(this);
  
  connect(m_pieMenuActionButton, &QPushButton::clicked, this, [this]() {
      QPoint pos = m_pieMenuActionButton->mapToGlobal(QPoint(0, m_pieMenuActionButton->height()));
      m_pieMenu->exec(pos);
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
                  auto& prefs = PreferenceManager::instance();
                  prefs.set(Preferences::PieMenuAction, qAction->data().toString());
                  m_pieMenuActionButton->setText(qAction->text());
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

          connect(listWidget, &QListWidget::itemClicked, this, [this, listWidget](QListWidgetItem* item) {
              auto& prefs = PreferenceManager::instance();
              prefs.set(Preferences::PieMenuAction, item->data(Qt::UserRole).toString());
              m_pieMenuActionButton->setText(item->text());
              m_pieMenu->close();
          });

          QWidgetAction* widgetAction = new QWidgetAction(menu);
          widgetAction->setDefaultWidget(listWidget);
          menu->addAction(widgetAction);
      }
  };

  buildMenu(m_pieMenu, root);

  auto* pieMenuLabel = new QLabel(tr("Pie Menu Action"));
  auto* pieMenuLayout = new QHBoxLayout();
  pieMenuLayout->addWidget(pieMenuLabel);
  pieMenuLayout->addWidget(m_pieMenuActionButton);

  auto* miscLayout = new QVBoxLayout();
  miscLayout->setContentsMargins(0, 0, 0, 0);
  miscLayout->addWidget(m_prefixWorldspawnOnCopyCheckBox);
  miscLayout->addLayout(pieMenuLayout);

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

bool MiscPreferencePane::canResetToDefaults()
{
  return true;
}

void MiscPreferencePane::doResetToDefaults()
{
  auto& prefs = PreferenceManager::instance();
  prefs.resetToDefault(Preferences::Language);
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

  m_prefixWorldspawnOnCopyCheckBox->setChecked(
    pref(Preferences::PrefixWorldspawnHeaderOnCopy));

  QString currentPath = pref(Preferences::PieMenuAction);
  auto& actionManager = ActionManager::instance();
  const auto& actions = actionManager.actionsMap();
  auto it = actions.find(std::filesystem::path(currentPath.toStdString()));
  if (it != actions.end()) {
      m_pieMenuActionButton->setText(it->second.label());
  } else {
      m_pieMenuActionButton->setText(tr("Select Action..."));
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
