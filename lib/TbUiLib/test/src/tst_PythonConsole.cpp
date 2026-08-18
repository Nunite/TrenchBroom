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

#include <QAbstractItemView>
#include <QCompleter>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTextEdit>
#include <QToolButton>
#include <QtTest/QTest>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/PythonConsole.h"

#include <algorithm>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("PythonConsole")
{
  auto console = PythonConsole{};
  auto* actions = console.createTabBarPage(&console);
  auto* input = console.findChild<QPlainTextEdit*>("PythonConsole_Input");
  auto* prompt = console.findChild<QLabel*>("PythonConsole_Prompt");
  auto* runButton = console.findChild<QToolButton*>("PythonConsole_Run");
  auto* clearButton = console.findChild<QToolButton*>("PythonConsole_Clear");
  auto* output = console.findChild<QTextEdit*>("PythonConsole_Output");

  REQUIRE(actions != nullptr);
  REQUIRE(input != nullptr);
  REQUIRE(prompt != nullptr);
  REQUIRE(runButton != nullptr);
  REQUIRE(clearButton != nullptr);
  REQUIRE(output != nullptr);
  CHECK(actions->objectName() == QStringLiteral("PythonConsole_TabActions"));
  CHECK(runButton->parentWidget() == actions);
  CHECK(clearButton->parentWidget() == actions);
  CHECK(input->isReadOnly() == false);
  const auto previousFontFamily = pref(Preferences::PythonConsoleFontFamily);
  const auto previousFontSize = pref(Preferences::PythonConsoleFontSize);
  const auto expectedFontSize = std::clamp(
    previousFontSize,
    Preferences::MinPythonConsoleFontSize,
    Preferences::MaxPythonConsoleFontSize);
  CHECK(input->font().pointSize() == expectedFontSize);
  CHECK(prompt->font() == input->font());
  CHECK(output->font() == input->font());
  CHECK(runButton->isEnabled() == false);

  auto* splitter = console.findChild<QSplitter*>("PythonConsole_Splitter");
  REQUIRE(splitter != nullptr);
  CHECK(splitter->count() == 2);

  auto commands = std::vector<std::string>{};
  console.setCommandExecutor(
    [&](const std::string& source) { commands.push_back(source); });
  CHECK_FALSE(runButton->isEnabled());

  input->setPlainText(QStringLiteral("value = 41\nvalue + 1"));
  CHECK(runButton->isEnabled());
  runButton->click();
  REQUIRE(commands.size() == 1u);
  CHECK(commands.back() == "value = 41\nvalue + 1");
  CHECK(input->toPlainText().isEmpty());
  CHECK_FALSE(runButton->isEnabled());

  QTRY_VERIFY_WITH_TIMEOUT(
    output->toPlainText().contains(QStringLiteral(">>> value = 41")), 500);
  CHECK(output->toPlainText().contains(QStringLiteral("... value + 1")));

  clearButton->click();
  CHECK(output->toPlainText().isEmpty());

  input->setPlainText(QStringLiteral("draft"));
  QTest::keyClick(input, Qt::Key_Up);
  CHECK(input->toPlainText() == QStringLiteral("value = 41\nvalue + 1"));
  QTest::keyClick(input, Qt::Key_Down);
  CHECK(input->toPlainText() == QStringLiteral("draft"));

  input->setPlainText(QStringLiteral("first line"));
  auto lineCursor = input->textCursor();
  lineCursor.movePosition(QTextCursor::End);
  input->setTextCursor(lineCursor);
  QTest::keyClick(input, Qt::Key_Return, Qt::ShiftModifier);
  CHECK(input->toPlainText() == QStringLiteral("first line\n"));

  input->setPlainText(QStringLiteral("2 + 2"));
  QTest::keyClick(input, Qt::Key_Return);
  REQUIRE(commands.size() == 2u);
  CHECK(commands.back() == "2 + 2");

  const auto newFontSize = expectedFontSize == 16 ? 17 : 16;
  setPref(Preferences::PythonConsoleFontSize, newFontSize);
  CHECK(input->font().pointSize() == newFontSize);
  CHECK(prompt->font() == input->font());
  CHECK(output->font() == input->font());
  CHECK(output->document()->defaultFont() == input->font());
  setPref(Preferences::PythonConsoleFontSize, previousFontSize);

  auto availableFontFamily = QString{};
  for (const auto& family : QFontDatabase::families())
  {
    if (
      QFontDatabase::isFixedPitch(family)
      && family.compare(QString::fromStdString(previousFontFamily), Qt::CaseInsensitive)
           != 0)
    {
      availableFontFamily = family;
      break;
    }
  }
  REQUIRE_FALSE(availableFontFamily.isEmpty());
  setPref(Preferences::PythonConsoleFontFamily, availableFontFamily.toStdString());
  CHECK(input->font().family().compare(availableFontFamily, Qt::CaseInsensitive) == 0);
  CHECK(prompt->font() == input->font());
  CHECK(output->font() == input->font());
  CHECK(output->document()->defaultFont() == input->font());
  setPref(Preferences::PythonConsoleFontFamily, previousFontFamily);

  REQUIRE(console.completer() != nullptr);

  // Test dot completion context extraction and insertion
  input->setPlainText(QStringLiteral("doc.sele"));
  auto cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  const auto [base1, prefix1] = console.completionContextUnderCursor();
  CHECK(base1 == QStringLiteral("doc"));
  CHECK(prefix1 == QStringLiteral("sele"));
  console.updateCompleter(true);
  console.insertCompletion(QStringLiteral("selection"));
  CHECK(input->toPlainText() == QStringLiteral("doc.selection"));

  // Test global shortcut context extraction and insertion
  input->setPlainText(QStringLiteral("sel.tra"));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  const auto [base2, prefix2] = console.completionContextUnderCursor();
  CHECK(base2 == QStringLiteral("sel"));
  CHECK(prefix2 == QStringLiteral("tra"));
  console.updateCompleter(true);
  console.insertCompletion(QStringLiteral("translate"));
  CHECK(input->toPlainText() == QStringLiteral("sel.translate"));

  input->setPlainText(QStringLiteral("selected_b"));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  const auto [base3, prefix3] = console.completionContextUnderCursor();
  CHECK(base3.isEmpty());
  CHECK(prefix3 == QStringLiteral("selected_b"));
  console.updateCompleter(true);
  console.insertCompletion(QStringLiteral("selected_brushes"));
  CHECK(input->toPlainText() == QStringLiteral("selected_brushes"));

  // Test deletion and auto-dismissal
  input->setPlainText(QStringLiteral(""));
  cursor = input->textCursor();
  input->setTextCursor(cursor);
  console.updateCompleter(false);
  CHECK(console.completer()->popup()->isVisible() == false);

  // Test Tab key completion workflow
  input->setPlainText(QStringLiteral("doc.sele"));
  cursor = input->textCursor();
  cursor.movePosition(QTextCursor::End);
  input->setTextCursor(cursor);
  console.updateCompleter(true);
  CHECK(console.completer()->popup()->isVisible());
  QTest::keyClick(input, Qt::Key_Tab);
  CHECK(input->toPlainText() == QStringLiteral("doc.selection"));
  CHECK(console.completer()->popup()->isVisible() == false);
}

} // namespace tb::ui
