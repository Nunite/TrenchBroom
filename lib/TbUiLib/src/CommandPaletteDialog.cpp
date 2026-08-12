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

#include "ui/CommandPaletteDialog.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QStringList>
#include <QVBoxLayout>

#include "base/PreferenceManager.h"
#include "ui/Action.h"
#include "ui/ActionExecutionContext.h"
#include "ui/ActionManager.h"
#include "ui/QStyleUtils.h"

#include <algorithm>

namespace tb::ui
{

namespace
{

QString makeDisplayPath(QString path)
{
  if (path.startsWith("Menu/"))
  {
    path.remove(0, QStringLiteral("Menu/").size());
  }
  path.replace("/", " > ");
  return path;
}

bool matchesFilter(const CommandPaletteDialog::Entry& entry, const QString& filter)
{
  const auto terms = filter.split(" ", Qt::SkipEmptyParts);
  return std::ranges::all_of(terms, [&](const auto& term) {
    return entry.filterText.contains(term, Qt::CaseInsensitive);
  });
}

} // namespace

CommandPaletteDialog::CommandPaletteDialog(
  const ActionManager& actionManager,
  const ActionExecutionContext& context,
  const std::filesystem::path& excludedActionPath,
  QWidget* parent)
  : QDialog{parent}
  , m_searchBox{new QLineEdit{this}}
  , m_actionList{new QListWidget{this}}
{
  setWindowTitle(tr("Command Palette"));
  setModal(true);
  resize(720, 480);

  const auto& actions = actionManager.actionsMap();
  m_entries.reserve(actions.size());
  for (const auto& [path, action] : actions)
  {
    if (path == excludedActionPath || !action.enabled(context))
    {
      continue;
    }

    const auto preferencePath = QString::fromStdString(path.generic_string());
    auto shortcutLabels = QStringList{};
    for (const auto& keySequence : pref(action.preference()))
    {
      shortcutLabels.push_back(keySequence.toString(QKeySequence::NativeText));
    }
    const auto shortcut = shortcutLabels.join(", ");
    const auto displayPath = makeDisplayPath(preferencePath);
    auto filterText =
      QStringList{action.label(), displayPath, preferencePath, shortcut}.join(" ");

    m_entries.push_back(Entry{
      action.label(), displayPath, preferencePath, shortcut, std::move(filterText)});
  }

  std::ranges::sort(m_entries, [](const auto& lhs, const auto& rhs) {
    const auto labelCompare = QString::compare(lhs.label, rhs.label, Qt::CaseInsensitive);
    if (labelCompare != 0)
    {
      return labelCompare < 0;
    }
    return QString::compare(lhs.preferencePath, rhs.preferencePath, Qt::CaseInsensitive)
           < 0;
  });

  m_searchBox->setObjectName("CommandPalette_SearchBox");
  m_searchBox->setPlaceholderText(tr("Search commands..."));
  m_searchBox->installEventFilter(this);

  m_actionList->setObjectName("CommandPalette_ActionList");
  m_actionList->setSelectionMode(QAbstractItemView::SingleSelection);
  m_actionList->setAlternatingRowColors(true);
  m_actionList->installEventFilter(this);

  auto* hintLabel = new QLabel{tr("Press Enter to run the selected command.")};
  setInfoStyle(hintLabel);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(8);
  layout->addWidget(m_searchBox);
  layout->addWidget(m_actionList, 1);
  layout->addWidget(hintLabel);
  setLayout(layout);

  connect(
    m_searchBox, &QLineEdit::textChanged, this, &CommandPaletteDialog::reloadActions);
  connect(
    m_searchBox,
    &QLineEdit::returnPressed,
    this,
    &CommandPaletteDialog::acceptCurrentAction);
  connect(
    m_actionList,
    &QListWidget::itemActivated,
    this,
    &CommandPaletteDialog::acceptCurrentAction);
  connect(
    m_actionList,
    &QListWidget::itemDoubleClicked,
    this,
    &CommandPaletteDialog::acceptCurrentAction);

  reloadActions();
}

const std::optional<std::filesystem::path>& CommandPaletteDialog::selectedActionPath()
  const
{
  return m_selectedActionPath;
}

bool CommandPaletteDialog::eventFilter(QObject* target, QEvent* event)
{
  if (
    (target == m_searchBox || target == m_actionList)
    && event->type() == QEvent::KeyPress)
  {
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    switch (keyEvent->key())
    {
    case Qt::Key_Up:
      selectRelativeRow(-1);
      return true;
    case Qt::Key_Down:
      selectRelativeRow(1);
      return true;
    case Qt::Key_PageUp:
      selectRelativeRow(-10);
      return true;
    case Qt::Key_PageDown:
      selectRelativeRow(10);
      return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      acceptCurrentAction();
      return true;
    case Qt::Key_Escape:
      reject();
      return true;
    default:
      break;
    }
  }

  return QDialog::eventFilter(target, event);
}

void CommandPaletteDialog::reloadActions()
{
  m_actionList->clear();

  const auto filter = m_searchBox->text();
  for (const auto& entry : m_entries)
  {
    if (!matchesFilter(entry, filter))
    {
      continue;
    }

    auto text = entry.label;
    if (!entry.shortcut.isEmpty())
    {
      text += QStringLiteral("    ");
      text += entry.shortcut;
    }
    text += QStringLiteral("\n");
    text += entry.displayPath;

    auto* item = new QListWidgetItem{text, m_actionList};
    item->setData(Qt::UserRole, entry.preferencePath);
    item->setToolTip(entry.preferencePath);
  }

  if (m_actionList->count() == 0)
  {
    auto* item = new QListWidgetItem{tr("No matching commands"), m_actionList};
    item->setFlags(Qt::NoItemFlags);
  }
  else
  {
    m_actionList->setCurrentRow(0);
  }
}

void CommandPaletteDialog::selectRelativeRow(const int offset)
{
  const auto count = m_actionList->count();
  if (count <= 0)
  {
    return;
  }

  const auto currentRow = std::max(0, m_actionList->currentRow());
  const auto nextRow = std::clamp(currentRow + offset, 0, count - 1);
  m_actionList->setCurrentRow(nextRow);
}

void CommandPaletteDialog::acceptCurrentAction()
{
  auto* item = m_actionList->currentItem();
  if (item == nullptr)
  {
    return;
  }

  const auto actionPath = item->data(Qt::UserRole).toString();
  if (actionPath.isEmpty())
  {
    return;
  }

  m_selectedActionPath = std::filesystem::path{actionPath.toStdString()};
  accept();
}

} // namespace tb::ui
