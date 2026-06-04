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

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QWidget>

#ifdef slots
#undef slots
#endif

#include "ui/python/PythonPluginPanel.h"
#include "ui/python/PythonUtils.h"

#include <Python.h>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
struct PythonInterpreter
{
  PythonInterpreter()
  {
    if (!Py_IsInitialized())
    {
      Py_Initialize();
    }
  }
};

struct PyObjectHandle
{
  PyObject* value = nullptr;

  explicit PyObjectHandle(PyObject* object = nullptr)
    : value{object}
  {
  }

  ~PyObjectHandle() { Py_XDECREF(value); }

  PyObjectHandle(const PyObjectHandle&) = delete;
  PyObjectHandle& operator=(const PyObjectHandle&) = delete;

  PyObjectHandle(PyObjectHandle&& other) noexcept
    : value{std::exchange(other.value, nullptr)}
  {
  }

  PyObjectHandle& operator=(PyObjectHandle&& other) noexcept
  {
    if (this != &other)
    {
      Py_XDECREF(value);
      value = std::exchange(other.value, nullptr);
    }
    return *this;
  }

  PyObject* get() const { return value; }
};

struct PythonPluginPanelFixture
{
  PythonInterpreter interpreter;
  PyObjectHandle module;
  QWidget container;
  PyObjectHandle panel;

  PythonPluginPanelFixture()
    : module{PyModule_New("tb_plugin_panel_test")}
  {
    REQUIRE(module.get() != nullptr);
    REQUIRE(initPluginPanelType(module.get()));
    panel = PyObjectHandle{createPluginPanelObject(&container)};
    REQUIRE(panel.get() != nullptr);
  }
};

PyObjectHandle callMethod(PyObject* object, const char* methodName)
{
  return PyObjectHandle{PyObject_CallMethod(object, methodName, nullptr)};
}

void clearPythonError()
{
  if (PyErr_Occurred())
  {
    PyErr_Clear();
  }
}

QString pluginPanelObjectName(const QString& prefix, const QString& key)
{
  return QStringLiteral("tb_py_panel_%1_%2").arg(prefix, key);
}

PyObjectHandle makeStringList(std::initializer_list<const char*> values)
{
  auto result = PyObjectHandle{PyList_New(static_cast<Py_ssize_t>(values.size()))};
  REQUIRE(result.get() != nullptr);

  auto index = Py_ssize_t{0};
  for (const auto* value : values)
  {
    PyList_SET_ITEM(result.get(), index++, toPyString(value));
  }

  return result;
}

PyObjectHandle makeRows(std::initializer_list<std::initializer_list<const char*>> rows)
{
  auto result = PyObjectHandle{PyList_New(static_cast<Py_ssize_t>(rows.size()))};
  REQUIRE(result.get() != nullptr);

  auto rowIndex = Py_ssize_t{0};
  for (const auto row : rows)
  {
    auto rowObject = makeStringList(row);
    PyList_SET_ITEM(result.get(), rowIndex++, rowObject.get());
    rowObject.value = nullptr;
  }

  return result;
}
} // namespace

TEST_CASE("PythonPluginPanel")
{
  auto fixture = PythonPluginPanelFixture{};
  auto* panel = fixture.panel.get();
  auto& container = fixture.container;

  SECTION("adds and updates named labels")
  {
    REQUIRE(
      PyObjectHandle{
        PyObject_CallMethod(panel, "add_label_named", "ss", "status", "Ready")}
        .get()
      != nullptr);

    auto* label = container.findChild<QLabel*>(pluginPanelObjectName("label", "status"));
    REQUIRE(label != nullptr);
    CHECK(label->text() == "Ready");

    const auto setResult = PyObjectHandle{
      PyObject_CallMethod(panel, "set_label_text", "ss", "status", "Running")};
    REQUIRE(setResult.get() != nullptr);
    CHECK(PyObject_IsTrue(setResult.get()) == 1);
    CHECK(label->text() == "Running");

    const auto missingResult = PyObjectHandle{
      PyObject_CallMethod(panel, "set_label_text", "ss", "missing", "Ignored")};
    REQUIRE(missingResult.get() != nullptr);
    CHECK(PyObject_IsTrue(missingResult.get()) == 0);
  }

  SECTION("adds numeric and text fields")
  {
    REQUIRE(
      PyObjectHandle{
        PyObject_CallMethod(panel, "add_int_field", "ssi", "count", "Count", 12)}
        .get()
      != nullptr);
    REQUIRE(
      PyObjectHandle{
        PyObject_CallMethod(panel, "add_float_field", "ssd", "ratio", "Ratio", 1.25)}
        .get()
      != nullptr);
    REQUIRE(
      PyObjectHandle{
        PyObject_CallMethod(panel, "add_text_field", "sss", "name", "Name", "start")}
        .get()
      != nullptr);

    auto* intField =
      container.findChild<QSpinBox*>(pluginPanelObjectName("int", "count"));
    auto* floatField =
      container.findChild<QDoubleSpinBox*>(pluginPanelObjectName("float", "ratio"));
    auto* textField =
      container.findChild<QLineEdit*>(pluginPanelObjectName("text", "name"));

    REQUIRE(intField != nullptr);
    REQUIRE(floatField != nullptr);
    REQUIRE(textField != nullptr);
    CHECK(intField->value() == 12);
    CHECK(floatField->value() == 1.25);
    CHECK(textField->text() == "start");

    REQUIRE(
      PyObjectHandle{PyObject_CallMethod(panel, "set_int_field", "si", "count", 42)}.get()
      != nullptr);
    REQUIRE(
      PyObjectHandle{PyObject_CallMethod(panel, "set_float_field", "sd", "ratio", 2.5)}
        .get()
      != nullptr);
    REQUIRE(
      PyObjectHandle{
        PyObject_CallMethod(panel, "set_text_field", "ss", "name", "updated")}
        .get()
      != nullptr);

    auto intValue =
      PyObjectHandle{PyObject_CallMethod(panel, "get_int_field", "s", "count")};
    auto floatValue =
      PyObjectHandle{PyObject_CallMethod(panel, "get_float_field", "s", "ratio")};
    auto textValue =
      PyObjectHandle{PyObject_CallMethod(panel, "get_text_field", "s", "name")};

    REQUIRE(intValue.get() != nullptr);
    REQUIRE(floatValue.get() != nullptr);
    REQUIRE(textValue.get() != nullptr);
    CHECK(PyLong_AsLong(intValue.get()) == 42);
    CHECK(PyFloat_AsDouble(floatValue.get()) == 2.5);
    CHECK(PyUnicode_AsUTF8(textValue.get()) == std::string{"updated"});
  }

  SECTION("adds checkbox and combo box controls")
  {
    const auto addCheckBox = PyObjectHandle{
      PyObject_CallMethod(panel, "add_checkbox", "ssi", "enabled", "Enabled", 1)};
    REQUIRE(addCheckBox.get() != nullptr);

    auto items = PyObjectHandle{PyList_New(3)};
    REQUIRE(items.get() != nullptr);
    PyList_SET_ITEM(items.get(), 0, toPyString("one"));
    PyList_SET_ITEM(items.get(), 1, toPyString("two"));
    PyList_SET_ITEM(items.get(), 2, toPyString("three"));

    const auto addCombo = PyObjectHandle{PyObject_CallMethod(
      panel, "add_combo_box", "ssOi", "mode", "Mode", items.get(), 1)};
    REQUIRE(addCombo.get() != nullptr);

    auto* checkBox =
      container.findChild<QCheckBox*>(pluginPanelObjectName("checkbox", "enabled"));
    auto* combo = container.findChild<QComboBox*>(pluginPanelObjectName("combo", "mode"));

    REQUIRE(checkBox != nullptr);
    REQUIRE(combo != nullptr);
    CHECK(checkBox->isChecked());
    CHECK(combo->count() == 3);
    CHECK(combo->currentIndex() == 1);
    CHECK(combo->currentText() == "two");

    REQUIRE(
      PyObjectHandle{PyObject_CallMethod(panel, "set_checkbox", "si", "enabled", 0)}.get()
      != nullptr);
    REQUIRE(
      PyObjectHandle{PyObject_CallMethod(panel, "set_combo_box_index", "si", "mode", 2)}
        .get()
      != nullptr);

    auto checkValue =
      PyObjectHandle{PyObject_CallMethod(panel, "get_checkbox", "s", "enabled")};
    auto comboIndex =
      PyObjectHandle{PyObject_CallMethod(panel, "get_combo_box_index", "s", "mode")};
    auto comboText =
      PyObjectHandle{PyObject_CallMethod(panel, "get_combo_box_text", "s", "mode")};

    REQUIRE(checkValue.get() != nullptr);
    REQUIRE(comboIndex.get() != nullptr);
    REQUIRE(comboText.get() != nullptr);
    CHECK_FALSE(checkBox->isChecked());
    CHECK(combo->currentIndex() == 2);
    CHECK(combo->currentText() == "three");
    CHECK(PyObject_IsTrue(checkValue.get()) == 0);
    CHECK(PyLong_AsLong(comboIndex.get()) == 2);
    CHECK(PyUnicode_AsUTF8(comboText.get()) == std::string{"three"});
  }

  SECTION("toggles widget enabled and visible state by key")
  {
    REQUIRE(
      PyObjectHandle{
        PyObject_CallMethod(panel, "add_text_field", "sss", "name", "Name", "start")}
        .get()
      != nullptr);

    auto* textField =
      container.findChild<QLineEdit*>(pluginPanelObjectName("text", "name"));
    REQUIRE(textField != nullptr);
    CHECK(textField->isEnabled());
    CHECK_FALSE(textField->isHidden());

    REQUIRE(
      PyObjectHandle{PyObject_CallMethod(panel, "set_widget_enabled", "si", "name", 0)}
        .get()
      != nullptr);
    REQUIRE(
      PyObjectHandle{PyObject_CallMethod(panel, "set_widget_visible", "si", "name", 0)}
        .get()
      != nullptr);

    CHECK_FALSE(textField->isEnabled());
    CHECK(textField->isHidden());

    REQUIRE(
      PyObjectHandle{PyObject_CallMethod(panel, "set_widget_enabled", "si", "name", 1)}
        .get()
      != nullptr);
    REQUIRE(
      PyObjectHandle{PyObject_CallMethod(panel, "set_widget_visible", "si", "name", 1)}
        .get()
      != nullptr);

    CHECK(textField->isEnabled());
    CHECK_FALSE(textField->isHidden());
  }

  SECTION("adds and updates text areas")
  {
    REQUIRE(
      PyObjectHandle{
        PyObject_CallMethod(panel, "add_text_area", "sss", "notes", "Notes", "start")}
        .get()
      != nullptr);

    auto* textArea =
      container.findChild<QPlainTextEdit*>(pluginPanelObjectName("text_area", "notes"));
    REQUIRE(textArea != nullptr);
    CHECK(textArea->toPlainText() == "start");

    REQUIRE(
      PyObjectHandle{
        PyObject_CallMethod(panel, "set_text_area", "ss", "notes", "updated")}
        .get()
      != nullptr);

    auto value =
      PyObjectHandle{PyObject_CallMethod(panel, "get_text_area", "s", "notes")};
    REQUIRE(value.get() != nullptr);
    CHECK(textArea->toPlainText() == "updated");
    CHECK(PyUnicode_AsUTF8(value.get()) == std::string{"updated"});
  }

  SECTION("adds list widgets and invokes selection callbacks")
  {
    auto callbackRows = PyObjectHandle{PyList_New(0)};
    REQUIRE(callbackRows.get() != nullptr);

    auto globals = PyObjectHandle{PyDict_New()};
    REQUIRE(globals.get() != nullptr);
    REQUIRE(PyDict_SetItemString(globals.get(), "rows", callbackRows.get()) == 0);

    auto runResult = PyObjectHandle{PyRun_String(
      "def on_select(row):\n"
      "    rows.append(row)\n"
      "\n",
      Py_file_input,
      globals.get(),
      globals.get())};
    if (runResult.get() == nullptr)
    {
      clearPythonError();
    }
    REQUIRE(runResult.get() != nullptr);

    auto* callback = PyDict_GetItemString(globals.get(), "on_select");
    REQUIRE(callback != nullptr);

    auto items = makeStringList({"one", "two", "three"});
    REQUIRE(
      PyObjectHandle{PyObject_CallMethod(
                       panel, "add_list_widget", "sOO", "items", items.get(), callback)}
        .get()
      != nullptr);

    auto* list =
      container.findChild<QListWidget*>(pluginPanelObjectName("list", "items"));
    REQUIRE(list != nullptr);
    REQUIRE(list->count() == 3);
    CHECK(list->item(0)->text() == "one");
    CHECK(list->item(2)->text() == "three");

    list->setCurrentRow(2);

    auto* rows = PyDict_GetItemString(globals.get(), "rows");
    REQUIRE(rows != nullptr);
    REQUIRE(PyList_Size(rows) == 1);
    CHECK(PyLong_AsLong(PyList_GetItem(rows, 0)) == 2);
  }

  SECTION("adds and updates table widgets")
  {
    auto columns = makeStringList({"Name", "Value"});
    auto rows = makeRows({{"alpha", "1"}, {"beta", "2"}});

    REQUIRE(
      PyObjectHandle{
        PyObject_CallMethod(
          panel, "add_table_widget", "sOOi", "table", columns.get(), rows.get(), 80)}
        .get()
      != nullptr);

    auto* table =
      container.findChild<QTableWidget*>(pluginPanelObjectName("table", "table"));
    REQUIRE(table != nullptr);
    CHECK(table->columnCount() == 2);
    CHECK(table->rowCount() == 2);
    CHECK(table->horizontalHeaderItem(0)->text() == "Name");
    CHECK(table->item(0, 0)->text() == "alpha");
    CHECK(table->item(1, 1)->text() == "2");
    CHECK(table->minimumHeight() == 80);

    auto updatedRows = makeRows({{"gamma", "3"}});
    REQUIRE(
      PyObjectHandle{PyObject_CallMethod(
                       panel, "set_table_widget_rows", "sO", "table", updatedRows.get())}
        .get()
      != nullptr);

    CHECK(table->rowCount() == 1);
    CHECK(table->item(0, 0)->text() == "gamma");
    CHECK(table->item(0, 1)->text() == "3");
  }

  SECTION("adds and updates tree widgets")
  {
    auto headers = makeStringList({"Label", "Kind"});
    auto rows = makeRows({{"root", "folder"}});

    REQUIRE(
      PyObjectHandle{
        PyObject_CallMethod(
          panel, "add_tree_widget", "sOOi", "tree", headers.get(), rows.get(), 90)}
        .get()
      != nullptr);

    auto* tree = container.findChild<QTreeWidget*>(pluginPanelObjectName("tree", "tree"));
    REQUIRE(tree != nullptr);
    CHECK(tree->columnCount() == 2);
    CHECK(tree->topLevelItemCount() == 1);
    CHECK(tree->topLevelItem(0)->text(0) == "root");
    CHECK(tree->minimumHeight() == 90);

    REQUIRE(
      PyObjectHandle{
        PyObject_CallMethod(panel, "add_tree_node", "sss", "tree", "child-id", "Child")}
        .get()
      != nullptr);
    REQUIRE(tree->topLevelItemCount() == 2);
    CHECK(tree->topLevelItem(1)->text(0) == "Child");
    CHECK(tree->topLevelItem(1)->data(0, Qt::UserRole).toString() == "child-id");

    auto updatedRows = makeRows({{"updated", "file"}});
    REQUIRE(
      PyObjectHandle{PyObject_CallMethod(
                       panel, "set_tree_widget_items", "sO", "tree", updatedRows.get())}
        .get()
      != nullptr);
    REQUIRE(tree->topLevelItemCount() == 1);
    CHECK(tree->topLevelItem(0)->text(0) == "updated");

    REQUIRE(
      PyObjectHandle{PyObject_CallMethod(panel, "clear_tree_items", "s", "tree")}.get()
      != nullptr);
    CHECK(tree->topLevelItemCount() == 0);
  }

  SECTION("adds html views and color fields")
  {
    REQUIRE(
      PyObjectHandle{
        PyObject_CallMethod(
          panel, "add_html_view", "ssi", "help", "<a href='tb:test'>Open</a>", 32)}
        .get()
      != nullptr);
    auto* htmlView =
      container.findChild<QTextBrowser*>(pluginPanelObjectName("html_view", "help"));
    REQUIRE(htmlView != nullptr);
    CHECK(htmlView->toHtml().contains("tb:test"));
    CHECK(htmlView->minimumHeight() <= 32);

    auto color = PyObjectHandle{Py_BuildValue("(iii)", 12, 34, 56)};
    REQUIRE(color.get() != nullptr);
    REQUIRE(
      PyObjectHandle{PyObject_CallMethod(
                       panel, "add_color_field", "ssO", "accent", "Accent", color.get())}
        .get()
      != nullptr);

    auto* colorButton =
      container.findChild<QPushButton*>(pluginPanelObjectName("color", "accent"));
    REQUIRE(colorButton != nullptr);
    CHECK(colorButton->text() == "12, 34, 56");

    auto colorValue =
      PyObjectHandle{PyObject_CallMethod(panel, "get_color_field", "s", "accent")};
    REQUIRE(colorValue.get() != nullptr);
    REQUIRE(PyTuple_Size(colorValue.get()) == 3);
    CHECK(PyLong_AsLong(PyTuple_GetItem(colorValue.get(), 0)) == 12);
    CHECK(PyLong_AsLong(PyTuple_GetItem(colorValue.get(), 1)) == 34);
    CHECK(PyLong_AsLong(PyTuple_GetItem(colorValue.get(), 2)) == 56);
  }

  SECTION("invokes button callbacks")
  {
    auto callbackCount = PyObjectHandle{PyLong_FromLong(0)};
    REQUIRE(callbackCount.get() != nullptr);

    PyObjectHandle globals{PyDict_New()};
    REQUIRE(globals.get() != nullptr);
    REQUIRE(PyDict_SetItemString(globals.get(), "count", callbackCount.get()) == 0);

    auto runResult = PyObjectHandle{PyRun_String(
      "def on_click():\n"
      "    global count\n"
      "    count += 1\n"
      "\n",
      Py_file_input,
      globals.get(),
      globals.get())};
    if (runResult.get() == nullptr)
    {
      clearPythonError();
    }
    REQUIRE(runResult.get() != nullptr);

    auto* callback = PyDict_GetItemString(globals.get(), "on_click");
    REQUIRE(callback != nullptr);

    REQUIRE(
      PyObjectHandle{
        PyObject_CallMethod(panel, "add_button_callback", "sO", "Run", callback)}
        .get()
      != nullptr);

    auto* button = container.findChild<QPushButton*>();
    REQUIRE(button != nullptr);

    button->click();

    auto* count = PyDict_GetItemString(globals.get(), "count");
    REQUIRE(count != nullptr);
    CHECK(PyLong_AsLong(count) == 1);
  }

  SECTION("clears panel content")
  {
    REQUIRE(
      PyObjectHandle{PyObject_CallMethod(panel, "add_label", "s", "Transient")}.get()
      != nullptr);
    REQUIRE(container.layout() != nullptr);
    REQUIRE(container.layout()->count() == 1);

    REQUIRE(callMethod(panel, "clear").get() != nullptr);
    CHECK(container.layout()->count() == 0);
  }
}

} // namespace tb::ui
