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
#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QStringList>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include "base/PreferenceManager.h"
#include "ui/Action.h"
#include "ui/ActionExecutionContext.h"
#include "ui/ActionManager.h"
#include "ui/QKeySequenceUtils.h"
#include "ui/QStringUtils.h"

#include <algorithm>

namespace tb::ui
{

namespace
{

constexpr auto LabelRole = Qt::UserRole + 1;
constexpr auto PathRole = Qt::UserRole + 2;
constexpr auto ShortcutRole = Qt::UserRole + 3;

class CommandPaletteItemDelegate : public QStyledItemDelegate
{
public:
  explicit CommandPaletteItemDelegate(QObject* parent)
    : QStyledItemDelegate{parent}
  {
    setObjectName(QStringLiteral("CommandPalette_ItemDelegate"));
  }

  void paint(
    QPainter* painter,
    const QStyleOptionViewItem& sourceOption,
    const QModelIndex& index) const override
  {
    const auto label = index.data(LabelRole).toString();
    if (label.isEmpty())
    {
      QStyledItemDelegate::paint(painter, sourceOption, index);
      return;
    }

    auto option = sourceOption;
    initStyleOption(&option, index);
    option.text.clear();

    const auto* style =
      option.widget != nullptr ? option.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &option, painter, option.widget);

    const auto contentRect = option.rect.adjusted(9, 5, -9, -5);
    if (contentRect.isEmpty())
    {
      return;
    }

    const auto path = index.data(PathRole).toString();
    const auto shortcut = index.data(ShortcutRole).toString();
    const auto primaryMetrics = QFontMetrics{option.font};

    auto secondaryFont = option.font;
    if (secondaryFont.pointSizeF() > 8.0)
    {
      secondaryFont.setPointSizeF(secondaryFont.pointSizeF() - 1.0);
    }
    const auto secondaryMetrics = QFontMetrics{secondaryFont};

    const auto firstLine = QRect{
      contentRect.left(),
      contentRect.top(),
      contentRect.width(),
      primaryMetrics.height()};
    const auto secondLine = QRect{
      contentRect.left(),
      contentRect.bottom() - secondaryMetrics.height() + 1,
      contentRect.width(),
      secondaryMetrics.height()};

    constexpr auto ColumnGap = 16;
    const auto shortcutWidth = shortcut.isEmpty()
                                 ? 0
                                 : std::min(
                                     secondaryMetrics.horizontalAdvance(shortcut),
                                     std::max(0, firstLine.width() / 2));
    const auto gap = shortcutWidth > 0 ? ColumnGap : 0;
    const auto labelWidth = std::max(0, firstLine.width() - shortcutWidth - gap);
    const auto labelRect =
      QRect{firstLine.left(), firstLine.top(), labelWidth, firstLine.height()};
    const auto shortcutRect = QRect{
      firstLine.right() - shortcutWidth + 1,
      firstLine.top(),
      shortcutWidth,
      firstLine.height()};

    const auto selected = option.state.testFlag(QStyle::State_Selected);
    const auto enabled = option.state.testFlag(QStyle::State_Enabled);
    const auto primaryRole = selected ? QPalette::HighlightedText : QPalette::Text;
    const auto primaryColor =
      option.palette.color(enabled ? QPalette::Active : QPalette::Disabled, primaryRole);
    auto secondaryColor =
      selected
        ? option.palette.color(QPalette::Active, QPalette::HighlightedText)
        : option.palette.color(
            enabled ? QPalette::Active : QPalette::Disabled, QPalette::PlaceholderText);
    if (selected)
    {
      secondaryColor.setAlpha(205);
    }

    painter->save();
    painter->setFont(option.font);
    painter->setPen(primaryColor);
    painter->drawText(
      labelRect,
      Qt::AlignLeft | Qt::AlignVCenter,
      primaryMetrics.elidedText(label, Qt::ElideRight, labelRect.width()));

    painter->setFont(secondaryFont);
    painter->setPen(secondaryColor);
    if (shortcutWidth > 0)
    {
      painter->drawText(
        shortcutRect,
        Qt::AlignRight | Qt::AlignVCenter,
        secondaryMetrics.elidedText(shortcut, Qt::ElideLeft, shortcutRect.width()));
    }
    painter->drawText(
      secondLine,
      Qt::AlignLeft | Qt::AlignVCenter,
      secondaryMetrics.elidedText(path, Qt::ElideRight, secondLine.width()));
    painter->restore();
  }

  QSize sizeHint(
    const QStyleOptionViewItem& option, const QModelIndex& index) const override
  {
    auto size = QStyledItemDelegate::sizeHint(option, index);
    const auto primaryHeight = QFontMetrics{option.font}.height();
    auto secondaryFont = option.font;
    if (secondaryFont.pointSizeF() > 8.0)
    {
      secondaryFont.setPointSizeF(secondaryFont.pointSizeF() - 1.0);
    }
    const auto secondaryHeight = QFontMetrics{secondaryFont}.height();
    size.setHeight(std::max(46, primaryHeight + secondaryHeight + 12));
    return size;
  }
};

QString makeDisplayPath(QString path)
{
  auto components = path.split('/');
  if (!components.empty() && components.front() == QStringLiteral("Menu"))
  {
    components.removeFirst();
  }
  for (auto& component : components)
  {
    component = translateUiText(component.toStdString());
  }
  return components.join(QStringLiteral(" > "));
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
  setObjectName("CommandPalette_Dialog");
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
      shortcutLabels.push_back(
        toQKeySequence(keySequence).toString(QKeySequence::NativeText));
    }
    const auto shortcut = shortcutLabels.join(", ");
    const auto displayPath = makeDisplayPath(preferencePath);
    const auto label = translateUiText(action.label());
    auto filterText = QStringList{label, displayPath, preferencePath, shortcut}.join(" ");

    m_entries.push_back(
      Entry{label, displayPath, preferencePath, shortcut, std::move(filterText)});
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
  m_actionList->setAlternatingRowColors(false);
  m_actionList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_actionList->setUniformItemSizes(true);
  m_actionList->setItemDelegate(new CommandPaletteItemDelegate{m_actionList});
  m_actionList->installEventFilter(this);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(6);
  layout->addWidget(m_searchBox);
  layout->addWidget(m_actionList, 1);
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

    auto* item = new QListWidgetItem{entry.label, m_actionList};
    item->setData(Qt::UserRole, entry.preferencePath);
    item->setData(LabelRole, entry.label);
    item->setData(PathRole, entry.displayPath);
    item->setData(ShortcutRole, entry.shortcut);
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
