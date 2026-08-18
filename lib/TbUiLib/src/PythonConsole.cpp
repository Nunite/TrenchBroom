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
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSizePolicy>
#include <QSplitter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/FixedWidthFont.h"

#include <algorithm>
#include <utility>

namespace tb::ui
{
namespace
{
constexpr auto MaxHistorySize = size_t{100u};

QFont consoleFont()
{
  auto font = Fonts::fixedWidthFont();
  const auto requestedFamily =
    QString::fromStdString(pref(Preferences::PythonConsoleFontFamily)).trimmed();
  for (const auto& availableFamily : QFontDatabase::families())
  {
    if (
      availableFamily.compare(requestedFamily, Qt::CaseInsensitive) == 0
      && QFontDatabase::isFixedPitch(availableFamily))
    {
      font.setFamily(availableFamily);
      break;
    }
  }
  font.setStyleHint(QFont::TypeWriter);
  font.setFixedPitch(true);
  font.setPointSize(std::clamp(
    pref(Preferences::PythonConsoleFontSize),
    Preferences::MinPythonConsoleFontSize,
    Preferences::MaxPythonConsoleFontSize));
  return font;
}

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
  , m_runButton{new QToolButton{this}}
  , m_clearButton{new QToolButton{this}}
{
  textView()->setObjectName("PythonConsole_Output");

  if (auto* baseLayout = layout())
  {
    baseLayout->removeWidget(textView());
  }

  m_splitter = new QSplitter{Qt::Horizontal, this};
  m_splitter->setObjectName("PythonConsole_Splitter");

  auto* leftPane = new QWidget{m_splitter};
  leftPane->setObjectName("PythonConsole_LeftPane");
  auto* leftLayout = new QVBoxLayout{leftPane};
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(0);
  leftLayout->addWidget(textView());

  auto* rightPane = new QWidget{m_splitter};
  rightPane->setObjectName("PythonConsole_RightPane");
  auto* rightLayout = new QVBoxLayout{rightPane};
  rightLayout->setContentsMargins(8, 4, 8, 8);
  rightLayout->setSpacing(4);

  m_prompt = new QLabel{tr("Script Editor (Ctrl+Enter to run)"), rightPane};
  m_prompt->setObjectName("PythonConsole_Prompt");
  m_prompt->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  m_input->setObjectName("PythonConsole_Input");
  m_input->setAccessibleName(tr("Python command input"));
  m_input->setLineWrapMode(QPlainTextEdit::NoWrap);
  m_input->setTabChangesFocus(false);
  m_input->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_input->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_input->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_input->installEventFilter(this);

  m_runButton->setObjectName("PythonConsole_Run");
  m_runButton->setText(tr("Run"));
  m_runButton->setToolTip(tr("Run current command (Ctrl+Enter)"));
  m_runButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
  m_runButton->setAutoRaise(true);
  m_runButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_runButton->setEnabled(false);
  connect(m_runButton, &QToolButton::clicked, this, &PythonConsole::executeCurrentInput);

  m_clearButton->setObjectName("PythonConsole_Clear");
  m_clearButton->setText(tr("Clear"));
  m_clearButton->setToolTip(tr("Clear console output"));
  m_clearButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
  m_clearButton->setAutoRaise(true);
  m_clearButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  connect(m_clearButton, &QToolButton::clicked, this, &Console::clear);

  connect(
    m_input, &QPlainTextEdit::textChanged, this, &PythonConsole::updateRunButtonEnabled);

  rightLayout->addWidget(m_prompt);
  rightLayout->addWidget(m_input, 1);

  m_splitter->addWidget(leftPane);
  m_splitter->addWidget(rightPane);
  m_splitter->setStretchFactor(0, 1);
  m_splitter->setStretchFactor(1, 1);
  m_splitter->setSizes({420, 380});

  layout()->addWidget(m_splitter);

  auto& prefs = PreferenceManager::instance();
  m_notifierConnection +=
    prefs.preferenceDidChangeNotifier.connect([this](const auto& path) {
      if (
        path == Preferences::PythonConsoleFontFamily.path
        || path == Preferences::PythonConsoleFontSize.path)
      {
        updateFont();
      }
    });
  updateFont();
}

QWidget* PythonConsole::createTabBarPage(QWidget* parent)
{
  auto* actions = new QWidget{parent};
  actions->setObjectName("PythonConsole_TabActions");

  auto* actionsLayout = new QHBoxLayout{actions};
  actionsLayout->setContentsMargins(0, 0, 0, 0);
  actionsLayout->setSpacing(2);
  actionsLayout->addStretch(1);
  actionsLayout->addWidget(m_runButton, 0, Qt::AlignVCenter);
  actionsLayout->addWidget(m_clearButton, 0, Qt::AlignVCenter);

  return actions;
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

void PythonConsole::updateFont()
{
  const auto font = consoleFont();
  textView()->setFont(font);
  textView()->document()->setDefaultFont(font);
  m_prompt->setFont(font);
  m_input->setFont(font);
  m_input->document()->setDefaultFont(font);
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
