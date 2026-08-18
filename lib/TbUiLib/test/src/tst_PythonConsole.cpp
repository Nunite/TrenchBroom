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

  input->setPlainText(QStringLiteral("2 + 2"));
  QTest::keyClick(input, Qt::Key_Return, Qt::ControlModifier);
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
}

} // namespace tb::ui
