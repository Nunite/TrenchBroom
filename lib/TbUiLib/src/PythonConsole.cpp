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

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QCompleter>
#include <QEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSplitter>
#include <QStringListModel>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
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

QStringList completionCandidatesForContext(const QString& baseQualifier)
{
  if (
    baseQualifier.compare(QStringLiteral("doc"), Qt::CaseInsensitive) == 0
    || baseQualifier.compare(
         QStringLiteral("tb2.current_document()"), Qt::CaseInsensitive)
         == 0)
  {
    return {
      QStringLiteral("selection"),
      QStringLiteral("entities"),
      QStringLiteral("transaction"),
      QStringLiteral("path"),
      QStringLiteral("name"),
      QStringLiteral("is_modified"),
      QStringLiteral("nodes"),
      QStringLiteral("layer"),
      QStringLiteral("layers")};
  }

  if (
    baseQualifier.compare(QStringLiteral("sel"), Qt::CaseInsensitive) == 0
    || baseQualifier.compare(QStringLiteral("doc.selection"), Qt::CaseInsensitive)
         == 0
    || baseQualifier.compare(QStringLiteral("selection"), Qt::CaseInsensitive)
         == 0)
  {
    return {
      QStringLiteral("brushes"),
      QStringLiteral("entities"),
      QStringLiteral("brush_faces"),
      QStringLiteral("translate"),
      QStringLiteral("rotate"),
      QStringLiteral("scale"),
      QStringLiteral("duplicate"),
      QStringLiteral("chamfer_vertices"),
      QStringLiteral("chamfer_edges"),
      QStringLiteral("set"),
      QStringLiteral("clear"),
      QStringLiteral("empty"),
      QStringLiteral("count")};
  }

  if (
    baseQualifier.compare(QStringLiteral("brush"), Qt::CaseInsensitive) == 0
    || baseQualifier.compare(QStringLiteral("b"), Qt::CaseInsensitive) == 0
    || baseQualifier.compare(QStringLiteral("b2"), Qt::CaseInsensitive) == 0
    || baseQualifier.compare(
         QStringLiteral("first_brush"), Qt::CaseInsensitive)
         == 0)
  {
    return {
      QStringLiteral("faces"),
      QStringLiteral("bounds"),
      QStringLiteral("center"),
      QStringLiteral("material"),
      QStringLiteral("id"),
      QStringLiteral("entity")};
  }

  if (
    baseQualifier.compare(QStringLiteral("face"), Qt::CaseInsensitive) == 0
    || baseQualifier.compare(QStringLiteral("f"), Qt::CaseInsensitive) == 0)
  {
    return {
      QStringLiteral("material"),
      QStringLiteral("vertices"),
      QStringLiteral("offset"),
      QStringLiteral("scale"),
      QStringLiteral("rotation"),
      QStringLiteral("plane"),
      QStringLiteral("set_material"),
      QStringLiteral("set_offset"),
      QStringLiteral("set_scale"),
      QStringLiteral("set_rotation")};
  }

  if (
    baseQualifier.compare(QStringLiteral("ent"), Qt::CaseInsensitive) == 0
    || baseQualifier.compare(QStringLiteral("entity"), Qt::CaseInsensitive) == 0
    || baseQualifier.compare(QStringLiteral("e"), Qt::CaseInsensitive) == 0)
  {
    return {
      QStringLiteral("classname"),
      QStringLiteral("origin"),
      QStringLiteral("get"),
      QStringLiteral("set"),
      QStringLiteral("remove"),
      QStringLiteral("properties"),
      QStringLiteral("brushes"),
      QStringLiteral("id")};
  }

  if (
    baseQualifier.compare(QStringLiteral("Vec3"), Qt::CaseInsensitive) == 0
    || baseQualifier.compare(QStringLiteral("vec"), Qt::CaseInsensitive) == 0
    || baseQualifier.compare(QStringLiteral("v"), Qt::CaseInsensitive) == 0)
  {
    return {
      QStringLiteral("x"),
      QStringLiteral("y"),
      QStringLiteral("z"),
      QStringLiteral("length"),
      QStringLiteral("normalized"),
      QStringLiteral("dot"),
      QStringLiteral("cross")};
  }

  if (
    baseQualifier.compare(QStringLiteral("Plane"), Qt::CaseInsensitive) == 0
    || baseQualifier.compare(QStringLiteral("plane"), Qt::CaseInsensitive) == 0
    || baseQualifier.compare(QStringLiteral("p"), Qt::CaseInsensitive) == 0)
  {
    return {
      QStringLiteral("normal"),
      QStringLiteral("dist"),
      QStringLiteral("distance_to")};
  }

  if (
    baseQualifier.compare(QStringLiteral("tb2"), Qt::CaseInsensitive) == 0
    || baseQualifier.compare(QStringLiteral("tb"), Qt::CaseInsensitive) == 0)
  {
    return {
      QStringLiteral("current_document"),
      QStringLiteral("create_brush"),
      QStringLiteral("create_plugin_panel"),
      QStringLiteral("selected_brushes"),
      QStringLiteral("selectedBrushes"),
      QStringLiteral("selected_entities"),
      QStringLiteral("selectedEntities"),
      QStringLiteral("selected_faces"),
      QStringLiteral("selectedFaces"),
      QStringLiteral("translate"),
      QStringLiteral("rotate"),
      QStringLiteral("scale"),
      QStringLiteral("duplicate"),
      QStringLiteral("delete_selection"),
      QStringLiteral("deleteSelection"),
      QStringLiteral("deselect_all"),
      QStringLiteral("deselectAll"),
      QStringLiteral("execute_action"),
      QStringLiteral("list_actions"),
      QStringLiteral("Vec3"),
      QStringLiteral("Plane"),
      QStringLiteral("Document"),
      QStringLiteral("Selection"),
      QStringLiteral("Brush"),
      QStringLiteral("Face"),
      QStringLiteral("Entity")};
  }

  return {
    QStringLiteral("doc"),
    QStringLiteral("sel"),
    QStringLiteral("selected_brushes"),
    QStringLiteral("selectedBrushes"),
    QStringLiteral("selected_entities"),
    QStringLiteral("selectedEntities"),
    QStringLiteral("selected_faces"),
    QStringLiteral("selectedFaces"),
    QStringLiteral("translate"),
    QStringLiteral("rotate"),
    QStringLiteral("scale"),
    QStringLiteral("duplicate"),
    QStringLiteral("delete_selection"),
    QStringLiteral("deleteSelection"),
    QStringLiteral("deselect_all"),
    QStringLiteral("deselectAll"),
    QStringLiteral("create_brush"),
    QStringLiteral("execute_action"),
    QStringLiteral("list_actions"),
    QStringLiteral("Vec3"),
    QStringLiteral("Plane"),
    QStringLiteral("tb2"),
    QStringLiteral("and"),
    QStringLiteral("as"),
    QStringLiteral("break"),
    QStringLiteral("continue"),
    QStringLiteral("def"),
    QStringLiteral("del"),
    QStringLiteral("elif"),
    QStringLiteral("else"),
    QStringLiteral("except"),
    QStringLiteral("False"),
    QStringLiteral("finally"),
    QStringLiteral("for"),
    QStringLiteral("from"),
    QStringLiteral("if"),
    QStringLiteral("import"),
    QStringLiteral("in"),
    QStringLiteral("is"),
    QStringLiteral("None"),
    QStringLiteral("not"),
    QStringLiteral("or"),
    QStringLiteral("pass"),
    QStringLiteral("raise"),
    QStringLiteral("return"),
    QStringLiteral("True"),
    QStringLiteral("try"),
    QStringLiteral("while"),
    QStringLiteral("with"),
    QStringLiteral("yield"),
    QStringLiteral("print"),
    QStringLiteral("len"),
    QStringLiteral("range"),
    QStringLiteral("enumerate"),
    QStringLiteral("zip"),
    QStringLiteral("list"),
    QStringLiteral("dict"),
    QStringLiteral("set"),
    QStringLiteral("tuple"),
    QStringLiteral("int"),
    QStringLiteral("float"),
    QStringLiteral("str"),
    QStringLiteral("bool"),
    QStringLiteral("sum"),
    QStringLiteral("min"),
    QStringLiteral("max"),
    QStringLiteral("abs"),
    QStringLiteral("all"),
    QStringLiteral("any"),
    QStringLiteral("isinstance"),
    QStringLiteral("type")};
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

  setupCompleter();

  m_runButton->setObjectName("PythonConsole_Run");
  m_runButton->setText(tr("Run"));
  m_runButton->setToolTip(tr("Run current command (Ctrl+Enter)"));
  m_runButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
  m_runButton->setAutoRaise(true);
  m_runButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_runButton->setEnabled(false);
  connect(
    m_runButton, &QToolButton::clicked, this, &PythonConsole::executeCurrentInput);

  m_clearButton->setObjectName("PythonConsole_Clear");
  m_clearButton->setText(tr("Clear"));
  m_clearButton->setToolTip(tr("Clear console output"));
  m_clearButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
  m_clearButton->setAutoRaise(true);
  m_clearButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  connect(m_clearButton, &QToolButton::clicked, this, &Console::clear);

  connect(
    m_input,
    &QPlainTextEdit::textChanged,
    this,
    &PythonConsole::updateRunButtonEnabled);

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

void PythonConsole::setCommandExecutor(
  std::function<void(const std::string&)> executor)
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

QCompleter* PythonConsole::completer() const
{
  return m_completer;
}

void PythonConsole::setupCompleter()
{
  m_completer = new QCompleter{this};
  m_completer->setWidget(m_input);
  m_completer->setCompletionMode(QCompleter::PopupCompletion);
  m_completer->setCaseSensitivity(Qt::CaseInsensitive);
  m_completer->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
  m_completionModel = new QStringListModel{m_completer};
  m_completer->setModel(m_completionModel);

  connect(
    m_completer,
    QOverload<const QString&>::of(&QCompleter::activated),
    this,
    &PythonConsole::insertCompletion);
}

std::pair<QString, QString> PythonConsole::completionContextUnderCursor() const
{
  const auto cursor = m_input->textCursor();
  const auto blockText = cursor.block().text();
  const auto posInBlock = cursor.positionInBlock();
  const auto lineToCursor = blockText.left(posInBlock);

  auto prefixStart = posInBlock;
  while (prefixStart > 0
         && (lineToCursor[prefixStart - 1].isLetterOrNumber()
             || lineToCursor[prefixStart - 1] == '_'))
  {
    --prefixStart;
  }
  const auto prefix = lineToCursor.mid(prefixStart);

  auto beforePrefix = lineToCursor.left(prefixStart).trimmed();
  if (beforePrefix.endsWith('.'))
  {
    beforePrefix.chop(1);
    beforePrefix = beforePrefix.trimmed();

    auto baseStart = beforePrefix.length();
    while (baseStart > 0
           && (beforePrefix[baseStart - 1].isLetterOrNumber()
               || beforePrefix[baseStart - 1] == '_'
               || beforePrefix[baseStart - 1] == '.'))
    {
      --baseStart;
    }
    const auto baseQualifier = beforePrefix.mid(baseStart);
    return {baseQualifier, prefix};
  }

  return {QString{}, prefix};
}

void PythonConsole::updateCompleter(const bool explicitTrigger)
{
  if (!m_completer || !m_completionModel)
  {
    return;
  }

  const auto [baseQualifier, prefix] = completionContextUnderCursor();

  if (!explicitTrigger && prefix.isEmpty() && baseQualifier.isEmpty())
  {
    if (m_completer->popup() && m_completer->popup()->isVisible())
    {
      m_completer->popup()->hide();
    }
    return;
  }

  auto candidates = completionCandidatesForContext(baseQualifier);
  candidates.sort(Qt::CaseInsensitive);
  m_completionModel->setStringList(candidates);

  m_completer->setCompletionPrefix(prefix);

  if (m_completer->completionCount() == 0)
  {
    if (m_completer->popup() && m_completer->popup()->isVisible())
    {
      m_completer->popup()->hide();
    }
    return;
  }

  auto cr = m_input->cursorRect();
  cr.setWidth(
    m_completer->popup()->sizeHintForColumn(0)
    + m_completer->popup()->verticalScrollBar()->sizeHint().width() + 24);
  m_completer->complete(cr);
}

void PythonConsole::insertCompletion(const QString& completion)
{
  if (completion.isEmpty())
  {
    return;
  }

  const auto [baseQualifier, prefix] = completionContextUnderCursor();
  auto cursor = m_input->textCursor();
  cursor.movePosition(
    QTextCursor::Left, QTextCursor::KeepAnchor, prefix.length());
  cursor.insertText(completion);
  m_input->setTextCursor(cursor);
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

  // 1. Ctrl+Enter always executes the current input
  if (
    (key == Qt::Key_Return || key == Qt::Key_Enter)
    && modifiers.testFlag(Qt::ControlModifier))
  {
    if (m_completer && m_completer->popup()->isVisible())
    {
      m_completer->popup()->hide();
    }
    executeCurrentInput();
    return true;
  }

  // 2. Completer handling when popup is visible
  if (m_completer && m_completer->popup()->isVisible())
  {
    switch (key)
    {
    case Qt::Key_Enter:
    case Qt::Key_Return:
    case Qt::Key_Tab:
      if (m_completer->popup()->currentIndex().isValid())
      {
        insertCompletion(
          m_completer->completionModel()
            ->data(m_completer->popup()->currentIndex(), Qt::DisplayRole)
            .toString());
      }
      else if (m_completer->currentCompletion().length() > 0)
      {
        insertCompletion(m_completer->currentCompletion());
      }
      m_completer->popup()->hide();
      return true;

    case Qt::Key_Escape:
      m_completer->popup()->hide();
      return true;

    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
      return false;

    default:
      break;
    }
  }

  // 3. Explicit completion triggers: Ctrl+Space or Tab (when not at beginning of line)
  if (
    (key == Qt::Key_Space && modifiers.testFlag(Qt::ControlModifier))
    || (key == Qt::Key_Tab && modifiers == Qt::NoModifier))
  {
    updateCompleter(true);
    return true;
  }

  // 4. History navigation (when completer is not visible)
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

  // 5. Automatic completion trigger on character typing
  const auto text = keyEvent->text();
  if (
    !text.isEmpty()
    && (text[0].isLetterOrNumber() || text[0] == '_' || text[0] == '.'))
  {
    QTimer::singleShot(0, this, [this]() {
      updateCompleter(false);
    });
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
