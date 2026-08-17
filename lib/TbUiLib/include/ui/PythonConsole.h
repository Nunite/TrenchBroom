/*
 Copyright (C) 2026 Lws

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

#include <functional>
#include <string>
#include <vector>

class QEvent;
class QLabel;
class QPlainTextEdit;
class QToolButton;
class QWidget;

namespace tb::ui
{

class PythonConsole : public Console
{
private:
  QPlainTextEdit* m_input = nullptr;
  QLabel* m_prompt = nullptr;
  QToolButton* m_runButton = nullptr;
  QToolButton* m_clearButton = nullptr;
  std::function<void(const std::string&)> m_commandExecutor;
  std::vector<std::string> m_history;
  size_t m_historyIndex = 0u;
  std::string m_historyDraft;
  NotifierConnection m_notifierConnection;

public:
  explicit PythonConsole(QWidget* parent = nullptr);

  QWidget* createTabBarPage(QWidget* parent = nullptr) override;
  void setCommandExecutor(std::function<void(const std::string&)> executor);
  void executeCurrentInput();

private:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void updateFont();
  void updateInputHeight();
  void updateRunButtonEnabled();
  void showPreviousHistoryEntry();
  void showNextHistoryEntry();
  void showHistoryEntry(const std::string& command);
};

} // namespace tb::ui
