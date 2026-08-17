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

#include "ui/PythonConsole.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QWidget>

#include "ui/FixedWidthFont.h"

#include <algorithm>
#include <utility>

namespace tb::ui
{
namespace
{
constexpr auto MaxHistorySize = size_t{100u};

std::string utf8String(const QString& string)
{
  const auto utf8 = string.toUtf8();
  return {utf8.constData(), static_cast<size_t>(utf8.size())};
}

QString formatPrompt(const QString& source)
{
  const auto lines = source.split('\n', Qt::KeepEmptyParts);
  auto result = QStringLiteral(">>> ") + lines.front();
  for (auto i = 1; i < lines.size(); ++i)
  {
    result += QStringLiteral("\n... ") + lines[i];
  }
  return result;
}
} // namespace

PythonConsole::PythonConsole(QWidget* parent)
  : Console{parent}
  , m_input{new QPlainTextEdit{this}}
  , m_runButton{new QPushButton{tr("Run"), this}}
{
  auto* inputBar = new QWidget{this};
  inputBar->setObjectName("PythonConsole_InputBar");

  auto* prompt = new QLabel{QStringLiteral(">>>"), inputBar};
  prompt->setObjectName("PythonConsole_Prompt");
  prompt->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
  prompt->setFont(Fonts::fixedWidthFont());

  m_input->setObjectName("PythonConsole_Input");
  m_input->setAccessibleName(tr("Python command input"));
  m_input->setFont(Fonts::fixedWidthFont());
  m_input->setLineWrapMode(QPlainTextEdit::NoWrap);
  m_input->setTabChangesFocus(false);
  m_input->setFixedHeight(54);
  m_input->installEventFilter(this);

  m_runButton->setObjectName("PythonConsole_Run");
  m_runButton->setEnabled(false);
  m_runButton->setFixedWidth(58);
  connect(m_runButton, &QPushButton::clicked, this, &PythonConsole::executeCurrentInput);
  connect(
    m_input, &QPlainTextEdit::textChanged, this, &PythonConsole::updateRunButtonEnabled);

  auto* inputLayout = new QHBoxLayout{inputBar};
  inputLayout->setContentsMargins(7, 6, 7, 6);
  inputLayout->setSpacing(6);
  inputLayout->addWidget(prompt);
  inputLayout->addWidget(m_input, 1);
  inputLayout->addWidget(m_runButton, 0, Qt::AlignVCenter);

  layout()->addWidget(inputBar);
}

void PythonConsole::setCommandExecutor(std::function<void(const std::string&)> executor)
{
  m_commandExecutor = std::move(executor);
  updateRunButtonEnabled();
}

void PythonConsole::executeCurrentInput()
{
  const auto sourceText = m_input->toPlainText();
  if (sourceText.trimmed().isEmpty() || !m_commandExecutor)
  {
    return;
  }

  const auto source = utf8String(sourceText);
  info() << utf8String(formatPrompt(sourceText));

  if (m_history.empty() || m_history.back() != source)
  {
    m_history.push_back(source);
    if (m_history.size() > MaxHistorySize)
    {
      m_history.erase(m_history.begin());
    }
  }
  m_historyIndex = m_history.size();
  m_historyDraft.clear();
  m_input->clear();

  m_commandExecutor(source);
  m_input->setFocus();
}

bool PythonConsole::eventFilter(QObject* watched, QEvent* event)
{
  if (watched != m_input || event->type() != QEvent::KeyPress)
  {
    return Console::eventFilter(watched, event);
  }

  auto* keyEvent = static_cast<QKeyEvent*>(event);
  const auto key = keyEvent->key();
  const auto modifiers = keyEvent->modifiers();
  if (
    (key == Qt::Key_Return || key == Qt::Key_Enter)
    && modifiers.testFlag(Qt::ControlModifier))
  {
    executeCurrentInput();
    return true;
  }

  if (
    modifiers == Qt::NoModifier && key == Qt::Key_Up
    && m_input->textCursor().blockNumber() == 0)
  {
    showPreviousHistoryEntry();
    return true;
  }

  const auto cursor = m_input->textCursor();
  if (
    modifiers == Qt::NoModifier && key == Qt::Key_Down
    && cursor.blockNumber() == m_input->document()->blockCount() - 1)
  {
    showNextHistoryEntry();
    return true;
  }

  return Console::eventFilter(watched, event);
}

void PythonConsole::updateRunButtonEnabled()
{
  m_runButton->setEnabled(
    m_commandExecutor && !m_input->toPlainText().trimmed().isEmpty());
}

void PythonConsole::showPreviousHistoryEntry()
{
  if (m_history.empty() || m_historyIndex == 0u)
  {
    return;
  }
  if (m_historyIndex == m_history.size())
  {
    m_historyDraft = utf8String(m_input->toPlainText());
  }
  --m_historyIndex;
  showHistoryEntry(m_history[m_historyIndex]);
}

void PythonConsole::showNextHistoryEntry()
{
  if (m_historyIndex >= m_history.size())
  {
    return;
  }
  ++m_historyIndex;
  showHistoryEntry(
    m_historyIndex < m_history.size() ? m_history[m_historyIndex] : m_historyDraft);
}

void PythonConsole::showHistoryEntry(const std::string& command)
{
  m_input->setPlainText(QString::fromUtf8(command));
  auto cursor = m_input->textCursor();
  cursor.movePosition(QTextCursor::End);
  m_input->setTextCursor(cursor);
}

} // namespace tb::ui
