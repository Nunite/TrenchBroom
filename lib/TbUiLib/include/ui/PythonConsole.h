/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "base/NotifierConnection.h"
#include "ui/Console.h"
#include "ui/python/PythonCompletionEngine.h"

#include <functional>
#include <string>
#include <vector>

class QAbstractItemModel;
class QCompleter;
class QEvent;
class QLabel;
class QPlainTextEdit;
class QSplitter;
class QToolButton;
class QWidget;

namespace tb::ui
{

class PythonConsole : public Console
{
private:
  QSplitter* m_splitter = nullptr;
  QPlainTextEdit* m_input = nullptr;
  QLabel* m_prompt = nullptr;
  QToolButton* m_runButton = nullptr;
  QToolButton* m_clearButton = nullptr;
  QCompleter* m_completer = nullptr;
  QAbstractItemModel* m_completionModel = nullptr;
  std::function<void(const std::string&)> m_commandExecutor;
  PythonCompletionRootProvider m_completionRootProvider;
  std::vector<std::string> m_history;
  size_t m_historyIndex = 0u;
  std::string m_historyDraft;
  NotifierConnection m_notifierConnection;

public:
  explicit PythonConsole(QWidget* parent = nullptr);

  QWidget* createTabBarPage(QWidget* parent = nullptr) override;
  void setCommandExecutor(std::function<void(const std::string&)> executor);
  void setCompletionRootProvider(PythonCompletionRootProvider provider);
  void executeCurrentInput();

  QCompleter* completer() const;
  void updateCompleter(bool explicitTrigger = true);
  void insertCompletion(const QString& completion);
  void insertActiveCompletion();
  std::pair<QString, QString> completionContextUnderCursor() const;

protected:
  void logToConsole(LogLevel level, const std::string& message) override;

private:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void setupCompleter();
  void updateFont();
  void updateRunButtonEnabled();
  void showPreviousHistoryEntry();
  void showNextHistoryEntry();
  void showHistoryEntry(const std::string& command);
};

} // namespace tb::ui
