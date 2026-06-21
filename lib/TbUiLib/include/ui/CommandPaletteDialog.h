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

#pragma once

#include <QDialog>
#include <QString>

#include <filesystem>
#include <optional>
#include <vector>

class QEvent;
class QLineEdit;
class QListWidget;

namespace tb::ui
{

class ActionExecutionContext;
class ActionManager;

class CommandPaletteDialog : public QDialog
{
  Q_OBJECT
public:
  struct Entry
  {
    QString label;
    QString displayPath;
    QString preferencePath;
    QString shortcut;
    QString filterText;
  };

private:
  std::vector<Entry> m_entries;
  QLineEdit* m_searchBox = nullptr;
  QListWidget* m_actionList = nullptr;
  std::optional<std::filesystem::path> m_selectedActionPath;

public:
  CommandPaletteDialog(
    const ActionManager& actionManager,
    const ActionExecutionContext& context,
    const std::filesystem::path& excludedActionPath,
    QWidget* parent = nullptr);

  const std::optional<std::filesystem::path>& selectedActionPath() const;

protected:
  bool eventFilter(QObject* target, QEvent* event) override;

private:
  void reloadActions();
  void selectRelativeRow(int offset);
  void acceptCurrentAction();
};

} // namespace tb::ui
