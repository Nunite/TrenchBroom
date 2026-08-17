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

#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QtTest/QTest>

#include "ui/PythonConsole.h"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("PythonConsole")
{
  auto console = PythonConsole{};
  auto* input = console.findChild<QPlainTextEdit*>("PythonConsole_Input");
  auto* runButton = console.findChild<QPushButton*>("PythonConsole_Run");
  auto* output = console.findChild<QTextEdit*>("Console_TextView");

  REQUIRE(input != nullptr);
  REQUIRE(runButton != nullptr);
  REQUIRE(output != nullptr);
  CHECK(input->isReadOnly() == false);
  CHECK(runButton->isEnabled() == false);

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

  input->setPlainText(QStringLiteral("draft"));
  QTest::keyClick(input, Qt::Key_Up);
  CHECK(input->toPlainText() == QStringLiteral("value = 41\nvalue + 1"));
  QTest::keyClick(input, Qt::Key_Down);
  CHECK(input->toPlainText() == QStringLiteral("draft"));

  input->setPlainText(QStringLiteral("2 + 2"));
  QTest::keyClick(input, Qt::Key_Return, Qt::ControlModifier);
  REQUIRE(commands.size() == 2u);
  CHECK(commands.back() == "2 + 2");
}

} // namespace tb::ui
