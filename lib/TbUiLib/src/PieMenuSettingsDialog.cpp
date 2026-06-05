#include "ui/PieMenuSettingsDialog.h"

#include <QAbstractItemView>
#include <QAction>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "ui/Action.h"
#include "ui/ActionManager.h"
#include "ui/ViewConstants.h"

#include <functional>
#include <map>

namespace tb::ui
{
namespace
{
constexpr auto DialogMinWidth = 520;
constexpr auto DialogMinHeight = 420;
constexpr auto ButtonColumnWidth = 112;

void configureActionButton(QPushButton* button)
{
  button->setMinimumWidth(ButtonColumnWidth);
}

struct MenuNode
{
  std::map<std::string, MenuNode> subMenus;
  std::vector<std::pair<QString, QString>> actions;
};
} // namespace

PieMenuSettingsDialog::PieMenuSettingsDialog(QWidget* parent)
  : QDialog{parent}
{
  setWindowTitle(tr("Pie Menu Settings"));
  setMinimumSize(DialogMinWidth, DialogMinHeight);
  createGui();
  populateActionMenu();
  loadPieMenuActions();
}

void PieMenuSettingsDialog::createGui()
{
  m_actionList = new QListWidget{};
  m_actionList->setAlternatingRowColors(true);
  m_actionList->setSelectionMode(QAbstractItemView::SingleSelection);
  m_actionList->setDragDropMode(QAbstractItemView::InternalMove);
  m_actionList->setDefaultDropAction(Qt::MoveAction);

  m_addButton = new QPushButton{tr("Add...")};
  m_removeButton = new QPushButton{tr("Remove Selected")};
  m_clearButton = new QPushButton{tr("Remove All")};
  m_actionMenu = new QMenu{this};

  for (auto* button : {m_addButton, m_removeButton, m_clearButton})
  {
    configureActionButton(button);
  }

  connect(m_addButton, &QPushButton::clicked, this, [this]() {
    const auto pos = m_addButton->mapToGlobal(QPoint{0, m_addButton->height()});
    m_actionMenu->exec(pos);
  });

  connect(m_removeButton, &QPushButton::clicked, this, [this]() {
    const auto items = m_actionList->selectedItems();
    if (!items.isEmpty())
    {
      delete m_actionList->takeItem(m_actionList->row(items.first()));
      savePieMenuActions();
    }
  });

  connect(m_clearButton, &QPushButton::clicked, this, [this]() {
    m_actionList->clear();
    savePieMenuActions();
  });

  connect(m_actionList->model(), &QAbstractItemModel::rowsMoved, this, [this]() {
    savePieMenuActions();
  });

  auto* buttonLayout = new QVBoxLayout{};
  buttonLayout->setContentsMargins(0, 0, 0, 0);
  buttonLayout->setSpacing(LayoutConstants::MediumVMargin);
  buttonLayout->addWidget(m_addButton);
  buttonLayout->addWidget(m_removeButton);
  buttonLayout->addWidget(m_clearButton);
  buttonLayout->addStretch(1);

  auto* contentLayout = new QHBoxLayout{};
  contentLayout->setContentsMargins(0, 0, 0, 0);
  contentLayout->setSpacing(LayoutConstants::WideHMargin);
  contentLayout->addWidget(m_actionList, 1);
  contentLayout->addLayout(buttonLayout);

  auto* buttonBox = new QDialogButtonBox{QDialogButtonBox::Close};
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin);
  layout->setSpacing(LayoutConstants::WideVMargin);
  layout->addLayout(contentLayout, 1);
  layout->addWidget(buttonBox);
  setLayout(layout);
}

void PieMenuSettingsDialog::populateActionMenu()
{
  auto& actionManager = ActionManager::instance();
  auto sortedActions = std::map<std::filesystem::path, const Action*>{};
  for (const auto& [path, action] : actionManager.actionsMap())
  {
    if (!action.label().isEmpty())
    {
      sortedActions[path] = &action;
    }
  }

  auto root = MenuNode{};
  for (const auto& [path, action] : sortedActions)
  {
    auto* currentNode = &root;
    auto it = path.begin();
    const auto end = path.end();

    if (it == end)
    {
      continue;
    }

    for (; it != end; ++it)
    {
      auto nextIt = it;
      ++nextIt;
      const auto isLeaf = nextIt == end;
      const auto component = it->string();

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

  auto buildMenu = std::function<void(QMenu*, const MenuNode&)>{};
  buildMenu = [&](QMenu* menu, const MenuNode& node) {
    for (const auto& [name, subNode] : node.subMenus)
    {
      auto* subMenu = menu->addMenu(QString::fromStdString(name));
      buildMenu(subMenu, subNode);
    }

    if (node.actions.size() <= 15)
    {
      for (const auto& [label, path] : node.actions)
      {
        auto* action = menu->addAction(label);
        action->setData(path);
        connect(action, &QAction::triggered, this, [this, action]() {
          addPieMenuAction(action->text(), action->data().toString());
        });
      }
    }
    else
    {
      auto* listWidget = new QListWidget{};
      listWidget->setFrameShape(QFrame::NoFrame);
      listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

      for (const auto& [label, path] : node.actions)
      {
        auto* item = new QListWidgetItem{label, listWidget};
        item->setData(Qt::UserRole, path);
      }

      auto rowHeight = listWidget->sizeHintForRow(0);
      if (rowHeight <= 0)
      {
        rowHeight = 25;
      }
      const auto totalHeight =
        static_cast<int>(node.actions.size()) * rowHeight + 2 * listWidget->frameWidth();
      listWidget->setMinimumWidth(250);
      listWidget->setFixedHeight(std::min(totalHeight, 300));

      connect(listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        addPieMenuAction(item->text(), item->data(Qt::UserRole).toString());
        m_actionMenu->close();
      });

      auto* widgetAction = new QWidgetAction{menu};
      widgetAction->setDefaultWidget(listWidget);
      menu->addAction(widgetAction);
    }
  };

  buildMenu(m_actionMenu, root);
}

void PieMenuSettingsDialog::addPieMenuAction(const QString& label, const QString& path)
{
  auto* item = new QListWidgetItem{label, m_actionList};
  item->setData(Qt::UserRole, path);
  savePieMenuActions();
}

void PieMenuSettingsDialog::loadPieMenuActions()
{
  m_actionList->clear();

  const auto paths = QString::fromStdString(pref(Preferences::PieMenuAction))
                       .split('|', Qt::SkipEmptyParts);
  const auto& actions = ActionManager::instance().actionsMap();

  for (const auto& pathStr : paths)
  {
    const auto path = std::filesystem::path{pathStr.toStdString()};
    const auto it = actions.find(path);
    const auto label = it != actions.end() ? it->second.label() : tr("Unknown Action");

    auto* item = new QListWidgetItem{label, m_actionList};
    item->setData(Qt::UserRole, pathStr);
  }
}

void PieMenuSettingsDialog::savePieMenuActions()
{
  auto paths = QStringList{};
  for (auto i = 0; i < m_actionList->count(); ++i)
  {
    paths << m_actionList->item(i)->data(Qt::UserRole).toString();
  }

  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::PieMenuAction, paths.join('|').toStdString());
  prefs.saveChanges();
}

} // namespace tb::ui
