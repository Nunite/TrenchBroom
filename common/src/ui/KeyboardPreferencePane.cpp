/*
 Copyright (C) 2010 Kristian Duske

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
 along with TrenchBroom.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "KeyboardPreferencePane.h"

#include <QBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTimer>

#include "ui/Actions.h"
#include "ui/KeyboardShortcutItemDelegate.h"
#include "ui/KeyboardShortcutModel.h"
#include "ui/MapDocument.h"
#include "ui/QtUtils.h"
#include "ui/ViewConstants.h"

namespace tb::ui
{

KeyboardPreferencePane::KeyboardPreferencePane(MapDocument* document, QWidget* parent)
  : PreferencePane{parent}
  , m_table{new QTableView{}}
  , m_model{new KeyboardShortcutModel{document}}
  , m_proxy{new QSortFilterProxyModel{}}
{
  m_proxy->setSourceModel(m_model);
  m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
  m_proxy->setFilterKeyColumn(2); // Filter based on the text in the Description column

  m_table->setModel(m_proxy);

  m_table->setHorizontalHeader(new QHeaderView(Qt::Horizontal));
  m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeMode::Fixed);
  m_table->horizontalHeader()->resizeSection(0, 150);
  m_table->horizontalHeader()->setSectionResizeMode(
    1, QHeaderView::ResizeMode::ResizeToContents);
  m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeMode::Stretch);

  // Tighter than default vertical row height, without the overhead of autoresizing
  m_table->verticalHeader()->setDefaultSectionSize(
    m_table->fontMetrics().lineSpacing() + 2);

  m_table->setSelectionMode(QAbstractItemView::SelectionMode::SingleSelection);
  m_table->setSelectionBehavior(QAbstractItemView::SelectionBehavior::SelectRows);

  m_table->setEditTriggers(
    QAbstractItemView::EditTrigger::SelectedClicked
    | QAbstractItemView::EditTrigger::DoubleClicked
    | QAbstractItemView::EditTrigger::EditKeyPressed);
  m_table->setItemDelegate(new KeyboardShortcutItemDelegate());

  auto* searchBox = createSearchBox();
  makeSmall(searchBox);

  auto* infoLabel = new QLabel{
    tr("Double-click an item to begin editing it. Click anywhere else to end editing.")};
  makeInfo(infoLabel);

  auto* infoAndSearchLayout = new QHBoxLayout{};
  infoAndSearchLayout->setContentsMargins(
    LayoutConstants::WideHMargin,
    LayoutConstants::MediumVMargin,
    LayoutConstants::MediumHMargin,
    LayoutConstants::MediumVMargin);
  infoAndSearchLayout->setSpacing(LayoutConstants::WideHMargin);
  infoAndSearchLayout->addWidget(infoLabel, 1);
  infoAndSearchLayout->addWidget(searchBox);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(m_table, 1);
  layout->addLayout(infoAndSearchLayout);
  setLayout(layout);

  setMinimumSize(900, 550);

  connect(searchBox, &QLineEdit::textChanged, this, [&](const QString& newText) {
    m_proxy->setFilterFixedString(newText);
  });
  
  // 确保模型对象名称设置正确，以便可以通过findChild找到
  m_model->setObjectName("KeyboardShortcutModel");
}

bool KeyboardPreferencePane::canResetToDefaults()
{
  return true;
}

void KeyboardPreferencePane::doResetToDefaults()
{
  auto& actionManager = ActionManager::instance();
  actionManager.resetAllKeySequences();
  m_model->reset();
}

void KeyboardPreferencePane::updateControls()
{
  m_table->update();
}

bool KeyboardPreferencePane::validate()
{
  if (m_model->hasConflicts())
  {
    QMessageBox::warning(
      this, "Conflicts", "Please fix all conflicting shortcuts (highlighted in red).");
    return false;
  }
  return true;
}

} // namespace tb::ui
