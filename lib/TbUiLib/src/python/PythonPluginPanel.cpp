#include "ui/python/PythonPluginPanel.h"
#include "ui/python/PythonTypes.h"
#include "ui/python/PythonUtils.h"
#include "Logger.h"
#include "ui/MapWindow.h"
#include "ui/Action.h"
#include "ui/ActionExecutionContext.h"
#include "ui/ActionManager.h"
#include "ui/AppController.h"
#include "ui/Inspector.h" // For InspectorPage enum if needed, though mostly used in Scripting initialization

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QTreeWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QGroupBox>
#include <QColorDialog>
#include <QMenu>
#include <QFontDatabase>
#include <QHeaderView>
#include <QTextTable>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <array>
#include <filesystem>

namespace tb::ui {

namespace {

class HtmlViewHoverHighlighter : public QObject
{
public:
  HtmlViewHoverHighlighter(QTextBrowser* browser, const QColor& color)
    : QObject(browser), m_browser(browser), m_color(color), m_currentRow(-1), m_currentTable(nullptr)
  {
    browser->setMouseTracking(true);
    if (browser->viewport())
    {
      browser->viewport()->setMouseTracking(true);
      browser->viewport()->installEventFilter(this);
    }
  }

protected:
  bool eventFilter(QObject* obj, QEvent* event) override
  {
    if (obj == m_browser->viewport())
    {
      if (event->type() == QEvent::MouseMove)
      {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        highlightRow(mouseEvent->pos());
      }
      else if (event->type() == QEvent::Leave)
      {
        clearHighlight();
      }
    }
    return QObject::eventFilter(obj, event);
  }

private:
  void highlightRow(const QPoint& pos)
  {
    QTextCursor cursor = m_browser->cursorForPosition(pos);
    QTextTable* table = cursor.currentTable();
    
    if (!table)
    {
      clearHighlight();
      return;
    }
    
    QTextTableCell cell = table->cellAt(cursor);
    if (!cell.isValid())
    {
        clearHighlight();
        return;
    }

    int row = cell.row();
    
    if (row == m_currentRow && table == m_currentTable) return;
    
    m_currentRow = row;
    m_currentTable = table;
    
    QList<QTextEdit::ExtraSelection> selections;
    int cols = table->columns();
    
    for (int c = 0; c < cols; ++c)
    {
      QTextTableCell rowCell = table->cellAt(row, c);
      if (!rowCell.isValid()) continue;

      QTextCursor cellCursor = rowCell.firstCursorPosition();
      cellCursor.setPosition(rowCell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
      
      QTextEdit::ExtraSelection sel;
      sel.cursor = cellCursor;
      sel.format.setBackground(m_color);
      sel.format.setProperty(QTextFormat::FullWidthSelection, true); 
      selections.append(sel);
    }
    
    m_browser->setExtraSelections(selections);
  }

  void clearHighlight()
  {
    if (m_currentRow != -1)
    {
      m_browser->setExtraSelections({});
      m_currentRow = -1;
      m_currentTable = nullptr;
    }
  }

  QTextBrowser* m_browser;
  QColor m_color;
  int m_currentRow;
  QTextTable* m_currentTable;
};

QString plugin_panel_object_name(const QString& prefix, const char* key)
{
  return QStringLiteral("tb_py_panel_%1_%2").arg(prefix, QString::fromUtf8(key));
}

template <typename T>
T* plugin_panel_find_widget(QWidget* container, const QString& prefix, const char* key)
{
  const auto name = plugin_panel_object_name(prefix, key);
  return container->findChild<T*>(name);
}

PyObject* plugin_panel_ensure_layout(QWidget* container)
{
  auto* layout = container->layout();
  if (layout == nullptr)
  {
    auto* v = new QVBoxLayout{};
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(4);
    container->setLayout(v);
  }
  Py_RETURN_NONE;
}

} // namespace

static PyObject* plugin_panel_clear(PyObject* self, PyObject*)
{
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  auto* layout = container->layout();
  if (layout == nullptr)
  {
    Py_RETURN_NONE;
  }
  while (auto* item = layout->takeAt(0))
  {
    if (auto* w = item->widget())
    {
      w->deleteLater();
    }
    delete item;
  }
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_add_label(PyObject* self, PyObject* args)
{
  const char* text = nullptr;
  if (!PyArg_ParseTuple(args, "s", &text))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  plugin_panel_ensure_layout(container);
  auto* layout = container->layout();
  auto* label = new QLabel{};
  label->setWordWrap(true);
  label->setText(QString::fromUtf8(text));
  layout->addWidget(label);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_add_label_named(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  const char* text = nullptr;
  if (!PyArg_ParseTuple(args, "ss", &key, &text))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  plugin_panel_ensure_layout(container);
  auto* layout = container->layout();
  auto* label = new QLabel{};
  label->setWordWrap(true);
  label->setObjectName(plugin_panel_object_name(QStringLiteral("label"), key));
  label->setText(QString::fromUtf8(text));
  layout->addWidget(label);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_set_label_text(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  const char* text = nullptr;
  if (!PyArg_ParseTuple(args, "ss", &key, &text))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  const auto name = plugin_panel_object_name(QStringLiteral("label"), key);
  auto* label = container->findChild<QLabel*>(name);
  if (label == nullptr)
  {
    Py_RETURN_FALSE;
  }
  label->setText(QString::fromUtf8(text));
  Py_RETURN_TRUE;
}

static PyObject* plugin_panel_add_int_field(PyObject* self, PyObject* args, PyObject* kwds)
{
  const char* key = nullptr;
  const char* labelText = nullptr;
  int value = 0;
  int minValue = 0;
  int maxValue = 999999;
  static char* kwlist[] = { (char*)"key", (char*)"label", (char*)"value", (char*)"min_value", (char*)"max_value", nullptr };

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "ssi|ii", kwlist, &key, &labelText, &value, &minValue, &maxValue))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  plugin_panel_ensure_layout(container);
  auto* layout = container->layout();

  auto* row = new QWidget{container};
  auto* rowLayout = new QHBoxLayout{};
  rowLayout->setContentsMargins(0, 0, 0, 0);
  rowLayout->setSpacing(6);
  row->setLayout(rowLayout);

  auto* label = new QLabel{};
  label->setText(QString::fromUtf8(labelText));

  auto* spin = new QSpinBox{};
  spin->setObjectName(plugin_panel_object_name(QStringLiteral("int"), key));
  spin->setRange(minValue, maxValue);
  spin->setValue(value);

  rowLayout->addWidget(label, 1);
  rowLayout->addWidget(spin, 0);
  layout->addWidget(row);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_add_float_field(PyObject* self, PyObject* args, PyObject* kwds)
{
  const char* key = nullptr;
  const char* labelText = nullptr;
  double value = 0.0;
  double minValue = -1.0e9;
  double maxValue = 1.0e9;
  int decimals = 3;
  double step = 1.0;
  static char* kwlist[] = { (char*)"key", (char*)"label", (char*)"value", (char*)"min_value", (char*)"max_value", (char*)"decimals", (char*)"step", nullptr };

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "ssd|ddid", kwlist, &key, &labelText, &value, &minValue, &maxValue, &decimals, &step))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  plugin_panel_ensure_layout(container);
  auto* layout = container->layout();

  auto* row = new QWidget{container};
  auto* rowLayout = new QHBoxLayout{};
  rowLayout->setContentsMargins(0, 0, 0, 0);
  rowLayout->setSpacing(6);
  row->setLayout(rowLayout);

  auto* label = new QLabel{};
  label->setText(QString::fromUtf8(labelText));

  auto* spin = new QDoubleSpinBox{};
  spin->setObjectName(plugin_panel_object_name(QStringLiteral("float"), key));
  spin->setDecimals(decimals);
  spin->setRange(minValue, maxValue);
  spin->setSingleStep(step);
  spin->setValue(value);

  rowLayout->addWidget(label, 1);
  rowLayout->addWidget(spin, 0);
  layout->addWidget(row);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_get_int_field(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  if (!PyArg_ParseTuple(args, "s", &key))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  const auto name = plugin_panel_object_name(QStringLiteral("int"), key);
  auto* spin = container->findChild<QSpinBox*>(name);
  if (spin == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such int field");
    return nullptr;
  }
  return PyLong_FromLong(static_cast<long>(spin->value()));
}

static PyObject* plugin_panel_get_float_field(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  if (!PyArg_ParseTuple(args, "s", &key))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  const auto name = plugin_panel_object_name(QStringLiteral("float"), key);
  auto* spin = container->findChild<QDoubleSpinBox*>(name);
  if (spin == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such float field");
    return nullptr;
  }
  return PyFloat_FromDouble(spin->value());
}

static PyObject* plugin_panel_add_text_field(PyObject* self, PyObject* args, PyObject* kwds)
{
  const char* key = nullptr;
  const char* labelText = nullptr;
  const char* value = nullptr;
  const char* placeholder = nullptr;
  static char* kwlist[] = { (char*)"key", (char*)"label", (char*)"value", (char*)"placeholder", nullptr };

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "ss|ss", kwlist, &key, &labelText, &value, &placeholder))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  plugin_panel_ensure_layout(container);
  auto* layout = container->layout();

  auto* row = new QWidget{container};
  auto* rowLayout = new QHBoxLayout{};
  rowLayout->setContentsMargins(0, 0, 0, 0);
  rowLayout->setSpacing(6);
  row->setLayout(rowLayout);

  auto* edit = new QLineEdit{};
  edit->setObjectName(plugin_panel_object_name(QStringLiteral("text"), key));
  if (value != nullptr)
  {
    edit->setText(QString::fromUtf8(value));
  }
  if (placeholder != nullptr)
  {
    edit->setPlaceholderText(QString::fromUtf8(placeholder));
  }

  if (labelText && *labelText)
  {
    auto* label = new QLabel{};
    label->setText(QString::fromUtf8(labelText));
    rowLayout->addWidget(label, 0);
    rowLayout->addWidget(edit, 1);
  }
  else
  {
    rowLayout->addWidget(edit, 1);
  }

  layout->addWidget(row);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_get_text_field(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  if (!PyArg_ParseTuple(args, "s", &key))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  const auto name = plugin_panel_object_name(QStringLiteral("text"), key);
  auto* edit = container->findChild<QLineEdit*>(name);
  if (edit == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such text field");
    return nullptr;
  }
  return toPyString(edit->text().toStdString());
}

static PyObject* plugin_panel_set_int_field(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  int value = 0;
  if (!PyArg_ParseTuple(args, "si", &key, &value))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  auto* spin = plugin_panel_find_widget<QSpinBox>(container, QStringLiteral("int"), key);
  if (spin == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such int field");
    return nullptr;
  }
  spin->setValue(value);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_set_float_field(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  double value = 0.0;
  if (!PyArg_ParseTuple(args, "sd", &key, &value))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  auto* spin = plugin_panel_find_widget<QDoubleSpinBox>(container, QStringLiteral("float"), key);
  if (spin == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such float field");
    return nullptr;
  }
  spin->setValue(value);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_set_text_field(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  const char* value = nullptr;
  if (!PyArg_ParseTuple(args, "ss", &key, &value))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  auto* edit = plugin_panel_find_widget<QLineEdit>(container, QStringLiteral("text"), key);
  if (edit == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such text field");
    return nullptr;
  }
  edit->setText(QString::fromUtf8(value));
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_set_checkbox(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  int value = 0;
  if (!PyArg_ParseTuple(args, "si", &key, &value))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  auto* checkBox = plugin_panel_find_widget<QCheckBox>(container, QStringLiteral("checkbox"), key);
  if (checkBox == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such checkbox");
    return nullptr;
  }
  checkBox->setChecked(value != 0);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_set_combo_box_index(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  int index = 0;
  if (!PyArg_ParseTuple(args, "si", &key, &index))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  auto* combo = plugin_panel_find_widget<QComboBox>(container, QStringLiteral("combo"), key);
  if (combo == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such combo box");
    return nullptr;
  }
  if (index < 0 || index >= combo->count())
  {
    PyErr_SetString(PyExc_ValueError, "combo box index out of range");
    return nullptr;
  }
  combo->setCurrentIndex(index);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_set_combo_box_items(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  PyObject* items = nullptr;
  int index = 0;
  if (!PyArg_ParseTuple(args, "sO|i", &key, &items, &index))
  {
    return nullptr;
  }
  if (!PyList_Check(items))
  {
    PyErr_SetString(PyExc_TypeError, "Expected list of strings for items");
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  auto* combo = plugin_panel_find_widget<QComboBox>(container, QStringLiteral("combo"), key);
  if (combo == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such combo box");
    return nullptr;
  }

  combo->blockSignals(true);
  combo->clear();
  const auto size = PyList_Size(items);
  for (Py_ssize_t i = 0; i < size; ++i)
  {
    auto* item = PyList_GetItem(items, i);
    const char* itemStr = PyUnicode_AsUTF8(item);
    if (itemStr)
    {
      combo->addItem(QString::fromUtf8(itemStr));
    }
  }
  combo->blockSignals(false);
  if (combo->count() > 0)
  {
    const auto boundedIndex = std::clamp(index, 0, combo->count() - 1);
    combo->setCurrentIndex(boundedIndex);
  }
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_set_list_widget_items(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  PyObject* items = nullptr;
  if (!PyArg_ParseTuple(args, "sO", &key, &items))
  {
    return nullptr;
  }
  if (!PyList_Check(items))
  {
    PyErr_SetString(PyExc_TypeError, "Expected list of strings for items");
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  auto* listWidget = plugin_panel_find_widget<QListWidget>(container, QStringLiteral("list"), key);
  if (listWidget == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such list widget");
    return nullptr;
  }

  listWidget->blockSignals(true);
  listWidget->clear();
  const auto size = PyList_Size(items);
  for (Py_ssize_t i = 0; i < size; ++i)
  {
    auto* item = PyList_GetItem(items, i);
    const char* itemStr = PyUnicode_AsUTF8(item);
    if (itemStr == nullptr)
    {
      return nullptr;
    }
    listWidget->addItem(QString::fromUtf8(itemStr));
  }
  listWidget->blockSignals(false);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_set_html_view(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  const char* html = nullptr;
  if (!PyArg_ParseTuple(args, "ss", &key, &html))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  auto* browser = plugin_panel_find_widget<QTextBrowser>(container, QStringLiteral("html_view"), key);
  if (browser == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such html view");
    return nullptr;
  }
  browser->setHtml(QString::fromUtf8(html));
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_set_widget_enabled(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  int enabled = 0;
  if (!PyArg_ParseTuple(args, "si", &key, &enabled))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();

  const std::array<QString, 15> prefixes = {
    QStringLiteral("label"),
    QStringLiteral("int"),
    QStringLiteral("float"),
    QStringLiteral("text"),
    QStringLiteral("text_area"),
    QStringLiteral("checkbox"),
    QStringLiteral("combo"),
    QStringLiteral("list"),
    QStringLiteral("html_view"),
    QStringLiteral("color"),
    QStringLiteral("group"),
    QStringLiteral("row"),
    QStringLiteral("column"),
    QStringLiteral("table"),
    QStringLiteral("tree"),
  };

  QWidget* found = nullptr;
  for (const auto& prefix : prefixes)
  {
    found = container->findChild<QWidget*>(plugin_panel_object_name(prefix, key));
    if (found != nullptr)
    {
      break;
    }
  }
  if (found == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such widget");
    return nullptr;
  }
  found->setEnabled(enabled != 0);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_set_widget_visible(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  int visible = 0;
  if (!PyArg_ParseTuple(args, "si", &key, &visible))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();

  const std::array<QString, 15> prefixes = {
    QStringLiteral("label"),
    QStringLiteral("int"),
    QStringLiteral("float"),
    QStringLiteral("text"),
    QStringLiteral("text_area"),
    QStringLiteral("checkbox"),
    QStringLiteral("combo"),
    QStringLiteral("list"),
    QStringLiteral("html_view"),
    QStringLiteral("color"),
    QStringLiteral("group"),
    QStringLiteral("row"),
    QStringLiteral("column"),
    QStringLiteral("table"),
    QStringLiteral("tree"),
  };

  QWidget* found = nullptr;
  for (const auto& prefix : prefixes)
  {
    found = container->findChild<QWidget*>(plugin_panel_object_name(prefix, key));
    if (found != nullptr)
    {
      break;
    }
  }
  if (found == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such widget");
    return nullptr;
  }
  found->setVisible(visible != 0);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_add_group(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  const char* title = nullptr;
  if (!PyArg_ParseTuple(args, "ss", &key, &title))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  plugin_panel_ensure_layout(container);
  auto* layout = container->layout();

  auto* groupBox = new QGroupBox{QString::fromUtf8(title), container};
  groupBox->setObjectName(plugin_panel_object_name(QStringLiteral("group"), key));
  auto* v = new QVBoxLayout{};
  v->setContentsMargins(6, 6, 6, 6);
  v->setSpacing(4);
  groupBox->setLayout(v);
  layout->addWidget(groupBox);

  return createPluginPanelObject(groupBox);
}

static PyObject* plugin_panel_add_row(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  if (!PyArg_ParseTuple(args, "s", &key))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  plugin_panel_ensure_layout(container);
  auto* layout = container->layout();

  auto* row = new QWidget{container};
  row->setObjectName(plugin_panel_object_name(QStringLiteral("row"), key));
  auto* h = new QHBoxLayout{};
  h->setContentsMargins(0, 0, 0, 0);
  h->setSpacing(6);
  row->setLayout(h);
  layout->addWidget(row);

  return createPluginPanelObject(row);
}

static PyObject* plugin_panel_add_column(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  if (!PyArg_ParseTuple(args, "s", &key))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  plugin_panel_ensure_layout(container);
  auto* layout = container->layout();

  auto* col = new QWidget{container};
  col->setObjectName(plugin_panel_object_name(QStringLiteral("column"), key));
  auto* v = new QVBoxLayout{};
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(4);
  col->setLayout(v);
  layout->addWidget(col);

  return createPluginPanelObject(col);
}

static PyObject* plugin_panel_add_text_area(PyObject* self, PyObject* args, PyObject* kwds)
{
  const char* key = nullptr;
  const char* labelText = nullptr;
  const char* value = nullptr;
  int height = 120;
  const char* placeholder = nullptr;
  
  static char* kwlist[] = { (char*)"key", (char*)"label", (char*)"value", (char*)"height", (char*)"placeholder", nullptr };

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "ss|ziz", kwlist, &key, &labelText, &value, &height, &placeholder))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  plugin_panel_ensure_layout(container);
  auto* layout = container->layout();

  auto* row = new QWidget{container};
  auto* rowLayout = new QVBoxLayout{};
  rowLayout->setContentsMargins(0, 0, 0, 0);
  rowLayout->setSpacing(4);
  row->setLayout(rowLayout);

  if (labelText && *labelText)
  {
    auto* label = new QLabel{};
    label->setText(QString::fromUtf8(labelText));
    rowLayout->addWidget(label, 0);
  }

  auto* edit = new QPlainTextEdit{};
  edit->setObjectName(plugin_panel_object_name(QStringLiteral("text_area"), key));
  if (value != nullptr)
  {
    edit->setPlainText(QString::fromUtf8(value));
  }
  if (placeholder != nullptr)
  {
    edit->setPlaceholderText(QString::fromUtf8(placeholder));
  }
  if (height > 0)
  {
    edit->setMinimumHeight(height);
  }
  edit->setTabChangesFocus(true);
  rowLayout->addWidget(edit, 1);

  layout->addWidget(row);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_get_text_area(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  if (!PyArg_ParseTuple(args, "s", &key))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  auto* edit = plugin_panel_find_widget<QPlainTextEdit>(container, QStringLiteral("text_area"), key);
  if (edit == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such text area");
    return nullptr;
  }
  return toPyString(edit->toPlainText().toStdString());
}

static PyObject* plugin_panel_set_text_area(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  const char* value = nullptr;
  if (!PyArg_ParseTuple(args, "ss", &key, &value))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  auto* edit = plugin_panel_find_widget<QPlainTextEdit>(container, QStringLiteral("text_area"), key);
  if (edit == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such text area");
    return nullptr;
  }
  edit->setPlainText(QString::fromUtf8(value));
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_add_table_widget(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  PyObject* columns = nullptr;
  PyObject* rows = nullptr;
  int height = 200;
  PyObject* callbackObj = nullptr;

  if (!PyArg_ParseTuple(args, "sOO|iO", &key, &columns, &rows, &height, &callbackObj))
  {
    return nullptr;
  }
  if (!PyList_Check(columns) || !PyList_Check(rows))
  {
    PyErr_SetString(PyExc_TypeError, "Expected list for columns and rows");
    return nullptr;
  }
  if (callbackObj != nullptr && callbackObj != Py_None && !PyCallable_Check(callbackObj))
  {
    PyErr_SetString(PyExc_TypeError, "expected a callable or None");
    return nullptr;
  }
  if (callbackObj == Py_None) callbackObj = nullptr;

  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  plugin_panel_ensure_layout(container);
  auto* layout = container->layout();

  auto* table = new QTableWidget{};
  table->setObjectName(plugin_panel_object_name(QStringLiteral("table"), key));
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setShowGrid(false);
  table->verticalHeader()->setVisible(false);
  table->horizontalHeader()->setStretchLastSection(true);
  table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);

  const auto colCount = static_cast<int>(PyList_Size(columns));
  table->setColumnCount(colCount);
  QStringList headerLabels;
  headerLabels.reserve(colCount);
  for (Py_ssize_t i = 0; i < PyList_Size(columns); ++i)
  {
    auto* col = PyList_GetItem(columns, i);
    const char* colStr = PyUnicode_AsUTF8(col);
    headerLabels.push_back(colStr ? QString::fromUtf8(colStr) : QString{});
  }
  table->setHorizontalHeaderLabels(headerLabels);

  const auto rowCount = static_cast<int>(PyList_Size(rows));
  table->setRowCount(rowCount);
  for (int r = 0; r < rowCount; ++r)
  {
    auto* rowObj = PyList_GetItem(rows, r);
    if (!PyList_Check(rowObj))
    {
      continue;
    }
    const auto cells = static_cast<int>(PyList_Size(rowObj));
    for (int c = 0; c < std::min(cells, colCount); ++c)
    {
      auto* cellObj = PyList_GetItem(rowObj, c);
      const char* cellStr = PyUnicode_AsUTF8(cellObj);
      auto* item = new QTableWidgetItem{cellStr ? QString::fromUtf8(cellStr) : QString{}};
      table->setItem(r, c, item);
    }
  }

  if (height > 0)
  {
    table->setMinimumHeight(height);
  }

  if (callbackObj != nullptr)
  {
    Py_INCREF(callbackObj);
    QObject::connect(
      table,
      &QTableWidget::currentCellChanged,
      container,
      [callbackObj, container](int currentRow, int currentColumn, int, int) {
        auto gil = PyGILState_Ensure();
        auto* win = container->window();
        auto* frame = dynamic_cast<tb::ui::MapWindow*>(win);
        auto* prev = g_currentFrame;
        if (frame != nullptr)
        {
          g_currentFrame = frame;
        }

        PyObject* argList = Py_BuildValue("(ii)", currentRow, currentColumn);
        auto* result = PyObject_CallObject(callbackObj, argList);
        Py_DECREF(argList);

        if (result == nullptr)
        {
          PyErr_Print();
          if (g_currentFrame != nullptr)
          {
            g_currentFrame->pythonLogger().error() << "Error in table widget callback";
          }
        }
        Py_XDECREF(result);
        g_currentFrame = prev;
        PyGILState_Release(gil);
      });
    QObject::connect(table, &QObject::destroyed, container, [callbackObj]() {
      auto gil = PyGILState_Ensure();
      Py_DECREF(callbackObj);
      PyGILState_Release(gil);
    });
  }

  layout->addWidget(table);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_set_table_widget_rows(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  PyObject* rows = nullptr;
  if (!PyArg_ParseTuple(args, "sO", &key, &rows))
  {
    return nullptr;
  }
  if (!PyList_Check(rows))
  {
    PyErr_SetString(PyExc_TypeError, "Expected list for rows");
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  auto* table = plugin_panel_find_widget<QTableWidget>(container, QStringLiteral("table"), key);
  if (table == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such table widget");
    return nullptr;
  }

  const auto colCount = table->columnCount();
  const auto rowCount = static_cast<int>(PyList_Size(rows));
  table->blockSignals(true);
  table->clearContents();
  table->setRowCount(rowCount);
  for (int r = 0; r < rowCount; ++r)
  {
    auto* rowObj = PyList_GetItem(rows, r);
    if (!PyList_Check(rowObj))
    {
      continue;
    }
    const auto cells = static_cast<int>(PyList_Size(rowObj));
    for (int c = 0; c < std::min(cells, colCount); ++c)
    {
      auto* cellObj = PyList_GetItem(rowObj, c);
      const char* cellStr = PyUnicode_AsUTF8(cellObj);
      auto* item = new QTableWidgetItem{cellStr ? QString::fromUtf8(cellStr) : QString{}};
      table->setItem(r, c, item);
    }
  }
  table->blockSignals(false);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_add_tree_widget(PyObject* self, PyObject* args, PyObject* kwds)
{
  const char* key = nullptr;
  PyObject* headers = nullptr;
  PyObject* items = nullptr;
  int height = 200;
  PyObject* callbackObj = nullptr;
  
  static char* kwlist[] = { (char*)"key", (char*)"headers", (char*)"items", (char*)"height", (char*)"callback", nullptr };

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "sOO|iO", kwlist, &key, &headers, &items, &height, &callbackObj))
  {
    return nullptr;
  }
  if (!PyList_Check(headers) || !PyList_Check(items))
  {
    PyErr_SetString(PyExc_TypeError, "Expected list for headers and items");
    return nullptr;
  }
  if (callbackObj != nullptr && callbackObj != Py_None && !PyCallable_Check(callbackObj))
  {
    PyErr_SetString(PyExc_TypeError, "expected a callable or None");
    return nullptr;
  }
  if (callbackObj == Py_None) callbackObj = nullptr;

  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  plugin_panel_ensure_layout(container);
  auto* layout = container->layout();

  auto* tree = new QTreeWidget{};
  tree->setObjectName(plugin_panel_object_name(QStringLiteral("tree"), key));
  tree->setRootIsDecorated(false);
  tree->setSelectionMode(QAbstractItemView::SingleSelection);
  tree->setSelectionBehavior(QAbstractItemView::SelectRows);
  tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tree->setHeaderHidden(false);
  tree->header()->setStretchLastSection(true);

  QStringList headerLabels;
  const auto headerCount = PyList_Size(headers);
  headerLabels.reserve(static_cast<int>(headerCount));
  for (Py_ssize_t i = 0; i < headerCount; ++i)
  {
    auto* h = PyList_GetItem(headers, i);
    const char* hStr = PyUnicode_AsUTF8(h);
    headerLabels.push_back(hStr ? QString::fromUtf8(hStr) : QString{});
  }
  tree->setHeaderLabels(headerLabels);

  const auto itemCount = static_cast<int>(PyList_Size(items));
  for (int i = 0; i < itemCount; ++i)
  {
    auto* rowObj = PyList_GetItem(items, i);
    if (!PyList_Check(rowObj))
    {
      continue;
    }
    QStringList cols;
    const auto colsCount = static_cast<int>(PyList_Size(rowObj));
    cols.reserve(colsCount);
    for (int c = 0; c < colsCount; ++c)
    {
      auto* cellObj = PyList_GetItem(rowObj, c);
      const char* cellStr = PyUnicode_AsUTF8(cellObj);
      cols.push_back(cellStr ? QString::fromUtf8(cellStr) : QString{});
    }
    tree->addTopLevelItem(new QTreeWidgetItem{cols});
  }

  if (height > 0)
  {
    tree->setMinimumHeight(height);
  }

  if (callbackObj != nullptr)
  {
    Py_INCREF(callbackObj);
    QObject::connect(tree, &QTreeWidget::currentItemChanged, container, [callbackObj, container, tree](QTreeWidgetItem* current, QTreeWidgetItem*) {
      int row = -1;
      if (current != nullptr)
      {
        row = tree->indexOfTopLevelItem(current);
      }
      auto gil = PyGILState_Ensure();
      auto* win = container->window();
      auto* frame = dynamic_cast<tb::ui::MapWindow*>(win);
      auto* prev = g_currentFrame;
      if (frame != nullptr)
      {
        g_currentFrame = frame;
      }

      PyObject* argList = Py_BuildValue("(i)", row);
      auto* result = PyObject_CallObject(callbackObj, argList);
      Py_DECREF(argList);

      if (result == nullptr)
      {
        PyErr_Print();
        if (g_currentFrame != nullptr)
        {
          g_currentFrame->pythonLogger().error() << "Error in tree widget callback";
        }
      }
      Py_XDECREF(result);
      g_currentFrame = prev;
      PyGILState_Release(gil);
    });
    QObject::connect(tree, &QObject::destroyed, container, [callbackObj]() {
      auto gil = PyGILState_Ensure();
      Py_DECREF(callbackObj);
      PyGILState_Release(gil);
    });
  }

  layout->addWidget(tree);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_add_tree_node(PyObject* self, PyObject* args, PyObject* kwds)
{
  const char* key = nullptr;
  const char* parent_id = nullptr; // Can be None/Null for root
  const char* node_id = nullptr;
  const char* text = nullptr;
  const char* icon = nullptr;
  int checkable = 0;
  int checked = 0;
  int expanded = 0;

  static char* kwlist[] = { 
    (char*)"key", (char*)"node_id", (char*)"text", 
    (char*)"parent_id", (char*)"icon", (char*)"checkable", (char*)"checked", (char*)"expanded",
    nullptr 
  };

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "sss|ssiii", kwlist, 
      &key, &node_id, &text, &parent_id, &icon, &checkable, &checked, &expanded))
  {
    return nullptr;
  }

  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr) return nullptr;
  auto* container = panel->container->data();
  auto* tree = plugin_panel_find_widget<QTreeWidget>(container, QStringLiteral("tree"), key);
  
  if (tree == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such tree widget");
    return nullptr;
  }

  // Find parent item
  QTreeWidgetItem* parentItem = nullptr;
  if (parent_id != nullptr && *parent_id != '\0')
  {
    auto items = tree->findItems(QString::fromUtf8(parent_id), Qt::MatchRecursive | Qt::MatchExactly, 0); // Assuming col 0 holds ID for internal lookup, or we use data
    // Actually, storing ID in data(0, Qt::UserRole) is better.
    // But for simplicity, let's scan.
    QTreeWidgetItemIterator it(tree);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).toString() == QString::fromUtf8(parent_id)) {
            parentItem = *it;
            break;
        }
        ++it;
    }
    if (!parentItem) {
        // Parent not found, maybe warn? or add to root?
        // Let's add to root but log warning?
    }
  }

  auto* item = new QTreeWidgetItem(parentItem ? parentItem : tree->invisibleRootItem());
  item->setText(0, QString::fromUtf8(text));
  item->setData(0, Qt::UserRole, QString::fromUtf8(node_id)); // Store ID
  
  if (icon && *icon) {
      // Simple icon lookup: standard icons or file paths
      // For now support standard style icons
      if (QString::fromUtf8(icon) == "folder") {
          item->setIcon(0, tree->style()->standardIcon(QStyle::SP_DirIcon));
      } else if (QString::fromUtf8(icon) == "file") {
          item->setIcon(0, tree->style()->standardIcon(QStyle::SP_FileIcon));
      } else if (QString::fromUtf8(icon) == "add") {
          item->setIcon(0, tree->style()->standardIcon(QStyle::SP_DialogApplyButton)); // Tick/Plus like
      } else if (QString::fromUtf8(icon) == "delete") {
          item->setIcon(0, tree->style()->standardIcon(QStyle::SP_DialogCancelButton)); // Cross
      } else if (QString::fromUtf8(icon) == "modified") {
           item->setIcon(0, tree->style()->standardIcon(QStyle::SP_DriveNetIcon)); // Placeholder
      }
      // TODO: Support loading from file path if needed
  }

  if (checkable) {
      item->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
  }
  
  if (expanded) {
      item->setExpanded(true);
  }

  Py_RETURN_NONE;
}

static PyObject* plugin_panel_clear_tree_items(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  if (!PyArg_ParseTuple(args, "s", &key)) return nullptr;
  
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr) return nullptr;
  auto* container = panel->container->data();
  auto* tree = plugin_panel_find_widget<QTreeWidget>(container, QStringLiteral("tree"), key);
  
  if (tree) {
      tree->clear();
  }
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_set_tree_widget_items(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  PyObject* items = nullptr;
  if (!PyArg_ParseTuple(args, "sO", &key, &items))
  {
    return nullptr;
  }
  if (!PyList_Check(items))
  {
    PyErr_SetString(PyExc_TypeError, "Expected list for items");
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  auto* tree = plugin_panel_find_widget<QTreeWidget>(container, QStringLiteral("tree"), key);
  if (tree == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such tree widget");
    return nullptr;
  }

  tree->blockSignals(true);
  tree->clear();
  const auto itemCount = static_cast<int>(PyList_Size(items));
  for (int i = 0; i < itemCount; ++i)
  {
    auto* rowObj = PyList_GetItem(items, i);
    if (!PyList_Check(rowObj))
    {
      continue;
    }
    QStringList cols;
    const auto colsCount = static_cast<int>(PyList_Size(rowObj));
    cols.reserve(colsCount);
    for (int c = 0; c < colsCount; ++c)
    {
      auto* cellObj = PyList_GetItem(rowObj, c);
      const char* cellStr = PyUnicode_AsUTF8(cellObj);
      cols.push_back(cellStr ? QString::fromUtf8(cellStr) : QString{});
    }
    tree->addTopLevelItem(new QTreeWidgetItem{cols});
  }
  tree->blockSignals(false);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_add_checkbox(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  const char* labelText = nullptr;
  int value = 0; // 0 or 1
  if (!PyArg_ParseTuple(args, "ss|i", &key, &labelText, &value))
  {
    return nullptr;
  }

  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }

  auto* container = panel->container->data();
  if (container == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "Plugin panel container is gone");
    return nullptr;
  }
  plugin_panel_ensure_layout(container);
  auto* layout = container->layout();

  auto* checkBox = new QCheckBox{};
  checkBox->setObjectName(plugin_panel_object_name(QStringLiteral("checkbox"), key));
  checkBox->setText(QString::fromUtf8(labelText));
  checkBox->setChecked(value != 0);

  layout->addWidget(checkBox);

  Py_RETURN_NONE;
}

static PyObject* plugin_panel_get_checkbox(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  if (!PyArg_ParseTuple(args, "s", &key))
  {
    return nullptr;
  }

  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }

  auto* container = panel->container->data();
  if (container == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "Plugin panel container is gone");
    return nullptr;
  }

  const auto name = plugin_panel_object_name(QStringLiteral("checkbox"), key);
  auto* checkBox = container->findChild<QCheckBox*>(name);

  if (checkBox == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such checkbox");
    return nullptr;
  }

  return PyBool_FromLong(checkBox->isChecked());
}

static PyObject* plugin_panel_add_combo_box(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  const char* labelText = nullptr;
  PyObject* items = nullptr;
  PyObject* callbackObj = nullptr;
  int initialIndex = 0;

  if (PyArg_ParseTuple(args, "ssOOi", &key, &labelText, &items, &callbackObj, &initialIndex))
  {
    // All arguments provided
  }
  else
  {
    PyErr_Clear();
    if (PyArg_ParseTuple(args, "ssOO", &key, &labelText, &items, &callbackObj))
    {
      if (PyCallable_Check(callbackObj))
      {
        initialIndex = 0;
      }
      else if (PyLong_Check(callbackObj))
      {
        initialIndex = (int)PyLong_AsLong(callbackObj);
        callbackObj = nullptr;
      }
      else if (callbackObj == Py_None)
      {
        callbackObj = nullptr;
        initialIndex = 0;
      }
    }
    else
    {
      PyErr_Clear();
      if (PyArg_ParseTuple(args, "ssO", &key, &labelText, &items))
      {
        callbackObj = nullptr;
        initialIndex = 0;
      }
      else
      {
        return nullptr;
      }
    }
  }

  PyObject* callback = callbackObj;
  if (callback != nullptr && callback != Py_None && !PyCallable_Check(callback))
  {
    PyErr_SetString(PyExc_TypeError, "expected a callable, int (index), or None");
    return nullptr;
  }
  if (callback == Py_None)
  {
    callback = nullptr;
  }

  if (!PyList_Check(items))
  {
    PyErr_SetString(PyExc_TypeError, "Expected list of strings for items");
    return nullptr;
  }

  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  plugin_panel_ensure_layout(container);
  auto* layout = container->layout();

  auto* row = new QWidget{container};
  auto* rowLayout = new QHBoxLayout{};
  rowLayout->setContentsMargins(0, 0, 0, 0);
  rowLayout->setSpacing(6);
  row->setLayout(rowLayout);

  auto* label = new QLabel{};
  label->setText(QString::fromUtf8(labelText));

  auto* combo = new QComboBox{};
  combo->setObjectName(plugin_panel_object_name(QStringLiteral("combo"), key));

  const auto size = PyList_Size(items);
  for (Py_ssize_t i = 0; i < size; ++i)
  {
    auto* item = PyList_GetItem(items, i);
    const char* itemStr = PyUnicode_AsUTF8(item);
    if (itemStr == nullptr)
    {
      return nullptr;
    }
    combo->addItem(QString::fromUtf8(itemStr));
  }

  if (initialIndex >= 0 && initialIndex < combo->count())
  {
    combo->setCurrentIndex(initialIndex);
  }

  if (callback != nullptr)
  {
    Py_INCREF(callback);
    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), container, [callback, container](int index) {
      auto gil = PyGILState_Ensure();
      auto* win = container->window();
      auto* frame = dynamic_cast<tb::ui::MapWindow*>(win);
      auto* prev = g_currentFrame;
      if (frame != nullptr)
      {
        g_currentFrame = frame;
      }
      
      PyObject* argList = Py_BuildValue("(i)", index);
      auto* result = PyObject_CallObject(callback, argList);
      Py_DECREF(argList);
      
      if (result == nullptr)
      {
        PyErr_Print();
        if (g_currentFrame != nullptr)
        {
          g_currentFrame->pythonLogger().error() << "Error in combo box callback";
        }
      }
      Py_XDECREF(result);
      g_currentFrame = prev;
      PyGILState_Release(gil);
    });
    QObject::connect(combo, &QObject::destroyed, container, [callback]() {
      auto gil = PyGILState_Ensure();
      Py_DECREF(callback);
      PyGILState_Release(gil);
    });
  }

  rowLayout->addWidget(label, 1);
  rowLayout->addWidget(combo, 0);
  layout->addWidget(row);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_get_combo_box_index(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  if (!PyArg_ParseTuple(args, "s", &key))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  const auto name = plugin_panel_object_name(QStringLiteral("combo"), key);
  auto* combo = container->findChild<QComboBox*>(name);
  if (combo == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such combo box");
    return nullptr;
  }
  return PyLong_FromLong(combo->currentIndex());
}

static PyObject* plugin_panel_get_combo_box_text(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  if (!PyArg_ParseTuple(args, "s", &key))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  const auto name = plugin_panel_object_name(QStringLiteral("combo"), key);
  auto* combo = container->findChild<QComboBox*>(name);
  if (combo == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such combo box");
    return nullptr;
  }
  return toPyString(combo->currentText().toStdString());
}

static PyObject* plugin_panel_set_text(PyObject* self, PyObject* args)
{
  const char* text = nullptr;
  if (!PyArg_ParseTuple(args, "s", &text))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  plugin_panel_ensure_layout(container);
  auto* layout = container->layout();
  while (auto* item = layout->takeAt(0))
  {
    if (auto* w = item->widget())
    {
      w->deleteLater();
    }
    delete item;
  }
  auto* label = new QLabel{};
  label->setWordWrap(true);
  label->setText(QString::fromUtf8(text));
  layout->addWidget(label);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_set_html(PyObject* self, PyObject* args)
{
  const char* html = nullptr;
  if (!PyArg_ParseTuple(args, "s", &html))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  plugin_panel_ensure_layout(container);
  auto* layout = container->layout();
  while (auto* item = layout->takeAt(0))
  {
    if (auto* w = item->widget())
    {
      w->deleteLater();
    }
    delete item;
  }
  auto* label = new QLabel{};
  label->setWordWrap(true);
  label->setTextFormat(Qt::RichText);
  label->setText(QString::fromUtf8(html));
  layout->addWidget(label);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_add_list_widget(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  PyObject* items = nullptr;
  PyObject* callbackObj = nullptr;

  if (!PyArg_ParseTuple(args, "sO|O", &key, &items, &callbackObj))
  {
    return nullptr;
  }

  if (!PyList_Check(items))
  {
    PyErr_SetString(PyExc_TypeError, "Expected list of strings for items");
    return nullptr;
  }

  if (callbackObj != nullptr && callbackObj != Py_None && !PyCallable_Check(callbackObj))
  {
    PyErr_SetString(PyExc_TypeError, "expected a callable or None");
    return nullptr;
  }
  if (callbackObj == Py_None) callbackObj = nullptr;

  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  plugin_panel_ensure_layout(container);
  auto* layout = container->layout();

  auto* listWidget = new QListWidget{};
  listWidget->setObjectName(plugin_panel_object_name(QStringLiteral("list"), key));
  const auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  listWidget->setFont(font);

  const auto size = PyList_Size(items);
  for (Py_ssize_t i = 0; i < size; ++i)
  {
    auto* item = PyList_GetItem(items, i);
    const char* itemStr = PyUnicode_AsUTF8(item);
    if (itemStr)
    {
      listWidget->addItem(QString::fromUtf8(itemStr));
    }
  }

  if (callbackObj != nullptr)
  {
    Py_INCREF(callbackObj);
    QObject::connect(listWidget, &QListWidget::currentRowChanged, container, [callbackObj, container](int row) {
      auto gil = PyGILState_Ensure();
      auto* win = container->window();
      auto* frame = dynamic_cast<tb::ui::MapWindow*>(win);
      auto* prev = g_currentFrame;
      if (frame != nullptr)
      {
        g_currentFrame = frame;
      }
      
      PyObject* argList = Py_BuildValue("(i)", row);
      auto* result = PyObject_CallObject(callbackObj, argList);
      Py_DECREF(argList);
      
      if (result == nullptr)
      {
        PyErr_Print();
        if (g_currentFrame != nullptr)
        {
          g_currentFrame->pythonLogger().error() << "Error in list widget callback";
        }
      }
      Py_XDECREF(result);
      g_currentFrame = prev;
      PyGILState_Release(gil);
    });
    QObject::connect(listWidget, &QObject::destroyed, container, [callbackObj]() {
      auto gil = PyGILState_Ensure();
      Py_DECREF(callbackObj);
      PyGILState_Release(gil);
    });
  }

  layout->addWidget(listWidget);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_set_list_widget_context_menu(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  PyObject* actions = nullptr; // List of (name, callback)
  if (!PyArg_ParseTuple(args, "sO", &key, &actions))
  {
    return nullptr;
  }

  if (!PyList_Check(actions))
  {
    PyErr_SetString(PyExc_TypeError, "Expected list of (name, callback) tuples");
    return nullptr;
  }

  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  const auto name = plugin_panel_object_name(QStringLiteral("list"), key);
  
  QListWidget* listWidget = nullptr;
  auto* layout = container->layout();
  if (layout) {
      for (int i = 0; i < layout->count(); ++i) {
          auto* item = layout->itemAt(i);
          if (auto* w = item->widget()) {
              if (w->objectName() == name) {
                  listWidget = qobject_cast<QListWidget*>(w);
                  if (listWidget) break;
              }
          }
      }
  }

  if (listWidget == nullptr)
  {
    listWidget = container->findChild<QListWidget*>(name);
  }

  if (listWidget == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such list widget");
    return nullptr;
  }

  listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
  
  Py_INCREF(actions);

  QObject::connect(listWidget, &QListWidget::customContextMenuRequested, container, [listWidget, actions, container](const QPoint& pos) {
      auto* item = listWidget->itemAt(pos);
      int index = -1;
      if (item) index = listWidget->row(item);
      
      QMenu menu;
      auto gil = PyGILState_Ensure();
      
      Py_ssize_t size = PyList_Size(actions);
      for(Py_ssize_t i=0; i<size; ++i) {
          PyObject* entry = PyList_GetItem(actions, i);
          char* actionName = nullptr;
          PyObject* cb = nullptr;
          if (PyArg_ParseTuple(entry, "sO", &actionName, &cb)) {
              if (PyCallable_Check(cb)) {
                  menu.addAction(QString::fromUtf8(actionName));
              }
          } else {
              PyErr_Clear(); 
          }
      }
      
      PyGILState_Release(gil); 
      
      QAction* selectedAction = menu.exec(QCursor::pos());
      
      if (selectedAction) {
           gil = PyGILState_Ensure();
           auto* win = container->window();
           auto* frame = dynamic_cast<tb::ui::MapWindow*>(win);
           auto* prev = g_currentFrame;
           if (frame != nullptr) g_currentFrame = frame;

           for(Py_ssize_t i=0; i<size; ++i) {
               PyObject* entry = PyList_GetItem(actions, i);
               char* actionName = nullptr;
               PyObject* cb = nullptr;
               if (PyArg_ParseTuple(entry, "sO", &actionName, &cb)) {
                   if (selectedAction->text() == QString::fromUtf8(actionName)) {
                       PyObject* argList = Py_BuildValue("(i)", index);
                       PyObject* res = PyObject_CallObject(cb, argList);
                       Py_DECREF(argList);
                       Py_XDECREF(res);
                       if (PyErr_Occurred()) {
                           PyErr_Print();
                           if (g_currentFrame) g_currentFrame->pythonLogger().error() << "Error in context menu callback";
                       }
                       break; 
                   }
               }
           }
           g_currentFrame = prev;
           PyGILState_Release(gil);
      }
  });

  QObject::connect(listWidget, &QObject::destroyed, container, [actions](){
      auto gil = PyGILState_Ensure();
      Py_DECREF(actions);
      PyGILState_Release(gil);
  });

  Py_RETURN_NONE;
}

static PyObject* plugin_panel_add_button(PyObject* self, PyObject* args)
{
  const char* text = nullptr;
  const char* actionPath = nullptr;
  if (!PyArg_ParseTuple(args, "s|z", &text, &actionPath))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  plugin_panel_ensure_layout(container);
  auto* layout = container->layout();
  auto* btn = new QPushButton{};
  btn->setText(QString::fromUtf8(text));
  if (actionPath != nullptr)
  {
    const auto pathStr = std::string{actionPath};
    QObject::connect(btn, &QPushButton::clicked, container, [container, pathStr]() {
      auto* win = container->window();
      auto* frame = dynamic_cast<tb::ui::MapWindow*>(win);
      if (frame == nullptr)
      {
        return;
      }
      try
      {
        const auto path = std::filesystem::path{pathStr};
        const auto& actionsMap = ActionManager::instance().actionsMap();
        const auto iAction = actionsMap.find(path);
        if (iAction == std::end(actionsMap))
        {
          return;
        }
        const auto& action = iAction->second;
        auto context = ActionExecutionContext{
          frame->appController(), frame, frame->currentMapViewBase()};
        if (!action.enabled(context))
        {
          return;
        }
        action.execute(context);
      }
      catch (...)
      {
      }
    });
  }
  layout->addWidget(btn);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_add_html_view(PyObject* self, PyObject* args, PyObject* kwds)
{
  const char* key = nullptr;
  const char* html = nullptr;
  int height = 200;
  PyObject* callback = nullptr;
  static char* kwlist[] = { (char*)"key", (char*)"html", (char*)"height", (char*)"callback", nullptr };

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "ss|iO", kwlist, &key, &html, &height, &callback))
  {
    return nullptr;
  }

  if (callback != nullptr && callback != Py_None && !PyCallable_Check(callback))
  {
    PyErr_SetString(PyExc_TypeError, "expected a callable or None");
    return nullptr;
  }
  if (callback == Py_None) callback = nullptr;

  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  plugin_panel_ensure_layout(container);
  auto* layout = container->layout();

  auto* browser = new QTextBrowser{};
  browser->setObjectName(plugin_panel_object_name(QStringLiteral("html_view"), key));
  browser->setHtml(QString::fromUtf8(html));
  if (height > 0 && height <= 40)
  {
    browser->setFixedHeight(height);
    browser->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    browser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    browser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    browser->document()->setDocumentMargin(0);
  }
  else
  {
    browser->setMinimumHeight(height);
  }
  browser->setOpenExternalLinks(false);
  browser->setFrameShape(QFrame::NoFrame);
  browser->setStyleSheet("background-color: transparent;");
  browser->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
  
  const auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  browser->setFont(font);

  new HtmlViewHoverHighlighter(browser, QColor(42, 45, 46));

  if (callback != nullptr)
  {
    Py_INCREF(callback);
    QObject::connect(browser, &QTextBrowser::anchorClicked, container, [callback, container](const QUrl& url) {
      auto gil = PyGILState_Ensure();
      auto* win = container->window();
      auto* frame = dynamic_cast<tb::ui::MapWindow*>(win);
      auto* prev = g_currentFrame;
      if (frame != nullptr)
      {
        g_currentFrame = frame;
      }
      
      PyObject* argList = Py_BuildValue("(s)", url.toString().toUtf8().constData());
      auto* result = PyObject_CallObject(callback, argList);
      Py_DECREF(argList);
      
      if (result == nullptr)
      {
        PyErr_Print();
        if (g_currentFrame != nullptr)
        {
          g_currentFrame->pythonLogger().error() << "Error in html view callback";
        }
      }
      Py_XDECREF(result);
      g_currentFrame = prev;
      PyGILState_Release(gil);
    });
    QObject::connect(browser, &QObject::destroyed, container, [callback]() {
      auto gil = PyGILState_Ensure();
      Py_DECREF(callback);
      PyGILState_Release(gil);
    });
  }

  layout->addWidget(browser);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_add_color_field(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  const char* labelText = nullptr;
  PyObject* initialColorObj = nullptr;

  if (!PyArg_ParseTuple(args, "ss|O", &key, &labelText, &initialColorObj))
  {
    return nullptr;
  }

  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  plugin_panel_ensure_layout(container);
  auto* layout = container->layout();

  auto* row = new QWidget{container};
  auto* rowLayout = new QHBoxLayout{};
  rowLayout->setContentsMargins(0, 0, 0, 0);
  rowLayout->setSpacing(6);
  row->setLayout(rowLayout);

  auto* label = new QLabel{};
  label->setText(QString::fromUtf8(labelText));

  auto* btn = new QPushButton{};
  btn->setObjectName(plugin_panel_object_name(QStringLiteral("color"), key));
  btn->setAutoFillBackground(true);
  btn->setFlat(true);

  auto setBtnColor = [](QPushButton* b, const QColor& c) {
    b->setStyleSheet(QString("background-color: %1; border: 1px solid gray;").arg(c.name()));
    b->setProperty("color_val", c);
    b->setText(QString("%1, %2, %3").arg(c.red()).arg(c.green()).arg(c.blue()));
    if (c.lightness() > 128)
      b->setStyleSheet(b->styleSheet() + " color: black;");
    else
      b->setStyleSheet(b->styleSheet() + " color: white;");
  };

  QColor color = Qt::white;
  if (initialColorObj != nullptr)
  {
      if (g_vec3Type && PyObject_TypeCheck(initialColorObj, g_vec3Type))
      {
          auto* v = (PyTbVec3*)initialColorObj;
          color = QColor::fromRgbF(static_cast<float>(v->vec.x()), static_cast<float>(v->vec.y()), static_cast<float>(v->vec.z()));
      }
      else if (PySequence_Check(initialColorObj) && PySequence_Size(initialColorObj) == 3)
      {
           PyObject* px = PySequence_GetItem(initialColorObj, 0);
           PyObject* py = PySequence_GetItem(initialColorObj, 1);
           PyObject* pz = PySequence_GetItem(initialColorObj, 2);
           
           if (px && py && pz && 
               (PyFloat_Check(px) || PyLong_Check(px)) && 
               (PyFloat_Check(py) || PyLong_Check(py)) && 
               (PyFloat_Check(pz) || PyLong_Check(pz)))
           {
               double r = PyFloat_AsDouble(px);
               double g = PyFloat_AsDouble(py);
               double b = PyFloat_AsDouble(pz);
               color = QColor(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b));
           }
           Py_XDECREF(px); Py_XDECREF(py); Py_XDECREF(pz);
      }
  }
  setBtnColor(btn, color);

  QObject::connect(btn, &QPushButton::clicked, container, [btn, container, setBtnColor]() {
    QColor current = btn->property("color_val").value<QColor>();
    QColor newColor = QColorDialog::getColor(current, container, "Select Color");
    if (newColor.isValid())
    {
      setBtnColor(btn, newColor);
    }
  });

  rowLayout->addWidget(label, 1);
  rowLayout->addWidget(btn, 0);
  layout->addWidget(row);
  Py_RETURN_NONE;
}

static PyObject* plugin_panel_get_color_field(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  if (!PyArg_ParseTuple(args, "s", &key))
  {
    return nullptr;
  }
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  const auto name = plugin_panel_object_name(QStringLiteral("color"), key);
  auto* btn = container->findChild<QPushButton*>(name);
  if (btn == nullptr)
  {
    PyErr_SetString(PyExc_KeyError, "No such color field");
    return nullptr;
  }
  
  QColor c = btn->property("color_val").value<QColor>();
  auto* tuple = PyTuple_New(3);
  PyTuple_SET_ITEM(tuple, 0, PyLong_FromLong(c.red()));
  PyTuple_SET_ITEM(tuple, 1, PyLong_FromLong(c.green()));
  PyTuple_SET_ITEM(tuple, 2, PyLong_FromLong(c.blue()));
  return tuple;
}

static PyObject* plugin_panel_get_widget_handle(PyObject* self, PyObject*)
{
  auto* panel = getPluginPanelFromPy(self);
  if (panel == nullptr)
  {
    return nullptr;
  }
  auto* container = panel->container->data();
  if (container == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "Plugin panel container is gone");
    return nullptr;
  }
  return PyLong_FromVoidPtr(container);
}

bool initPluginPanelType(PyObject* module)
{
  if (g_pluginPanelType == nullptr)
  {
    static PyMethodDef pluginPanelMethods[] = {
      {"add_list_widget", plugin_panel_add_list_widget, METH_VARARGS, nullptr},
      {"set_list_widget_context_menu", plugin_panel_set_list_widget_context_menu, METH_VARARGS, nullptr},
      {"set_list_widget_items", plugin_panel_set_list_widget_items, METH_VARARGS, nullptr},
      {"clear", plugin_panel_clear, METH_NOARGS, nullptr},
      {"add_label", plugin_panel_add_label, METH_VARARGS, nullptr},
      {"add_label_named", plugin_panel_add_label_named, METH_VARARGS, nullptr},
      {"set_label_text", plugin_panel_set_label_text, METH_VARARGS, nullptr},
      {"add_int_field", (PyCFunction)(void(*)(void))plugin_panel_add_int_field, METH_VARARGS | METH_KEYWORDS, nullptr},
      {"add_float_field", (PyCFunction)(void(*)(void))plugin_panel_add_float_field, METH_VARARGS | METH_KEYWORDS, nullptr},
      {"add_text_field", (PyCFunction)(void(*)(void))plugin_panel_add_text_field, METH_VARARGS | METH_KEYWORDS, nullptr},
      {"set_int_field", plugin_panel_set_int_field, METH_VARARGS, nullptr},
      {"set_float_field", plugin_panel_set_float_field, METH_VARARGS, nullptr},
      {"set_text_field", plugin_panel_set_text_field, METH_VARARGS, nullptr},
      {"add_checkbox", plugin_panel_add_checkbox, METH_VARARGS, nullptr},
      {"set_checkbox", plugin_panel_set_checkbox, METH_VARARGS, nullptr},
      {"add_combo_box", plugin_panel_add_combo_box, METH_VARARGS, nullptr},
      {"set_combo_box_index", plugin_panel_set_combo_box_index, METH_VARARGS, nullptr},
      {"set_combo_box_items", plugin_panel_set_combo_box_items, METH_VARARGS, nullptr},
      {"add_color_field", plugin_panel_add_color_field, METH_VARARGS, nullptr},
      {"get_int_field", plugin_panel_get_int_field, METH_VARARGS, nullptr},
      {"get_float_field", plugin_panel_get_float_field, METH_VARARGS, nullptr},
      {"get_text_field", plugin_panel_get_text_field, METH_VARARGS, nullptr},
      {"get_checkbox", plugin_panel_get_checkbox, METH_VARARGS, nullptr},
      {"get_combo_box_index", plugin_panel_get_combo_box_index, METH_VARARGS, nullptr},
      {"get_combo_box_text", plugin_panel_get_combo_box_text, METH_VARARGS, nullptr},
      {"get_color_field", plugin_panel_get_color_field, METH_VARARGS, nullptr},
      {"set_widget_enabled", plugin_panel_set_widget_enabled, METH_VARARGS, nullptr},
      {"set_widget_visible", plugin_panel_set_widget_visible, METH_VARARGS, nullptr},
      {"set_text", plugin_panel_set_text, METH_VARARGS, nullptr},
      {"set_html", plugin_panel_set_html, METH_VARARGS, nullptr},
      {"add_button", plugin_panel_add_button, METH_VARARGS, nullptr},
      {"add_html_view", (PyCFunction)(void(*)(void))plugin_panel_add_html_view, METH_VARARGS | METH_KEYWORDS, nullptr},
      {"set_html_view", plugin_panel_set_html_view, METH_VARARGS, nullptr},
      {"add_group", plugin_panel_add_group, METH_VARARGS, nullptr},
      {"add_row", plugin_panel_add_row, METH_VARARGS, nullptr},
      {"add_column", plugin_panel_add_column, METH_VARARGS, nullptr},
      {"add_text_area", (PyCFunction)(void(*)(void))plugin_panel_add_text_area, METH_VARARGS | METH_KEYWORDS, nullptr},
      {"get_text_area", plugin_panel_get_text_area, METH_VARARGS, nullptr},
      {"set_text_area", plugin_panel_set_text_area, METH_VARARGS, nullptr},
      {"add_table_widget", plugin_panel_add_table_widget, METH_VARARGS, nullptr},
      {"set_table_widget_rows", plugin_panel_set_table_widget_rows, METH_VARARGS, nullptr},
      {"add_tree_widget", (PyCFunction)(void(*)(void))plugin_panel_add_tree_widget, METH_VARARGS | METH_KEYWORDS, nullptr},
      {"add_tree_node", (PyCFunction)(void(*)(void))plugin_panel_add_tree_node, METH_VARARGS | METH_KEYWORDS, nullptr},
      {"clear_tree_items", plugin_panel_clear_tree_items, METH_VARARGS, nullptr},
      {"set_tree_widget_items", plugin_panel_set_tree_widget_items, METH_VARARGS, nullptr},
      {"get_widget_handle", plugin_panel_get_widget_handle, METH_NOARGS, nullptr},
      {"add_button_callback",
           [](PyObject* self, PyObject* args) -> PyObject* {
             const char* text = nullptr;
             PyObject* callback = nullptr;
             if (!PyArg_ParseTuple(args, "sO", &text, &callback))
             {
               return nullptr;
             }
             if (!PyCallable_Check(callback))
             {
               PyErr_SetString(PyExc_TypeError, "expected a callable");
               return nullptr;
             }
             auto* panel = getPluginPanelFromPy(self);
             if (panel == nullptr)
             {
               return nullptr;
             }
             auto* container = panel->container->data();
             plugin_panel_ensure_layout(container);
             auto* layout = container->layout();
             auto* btn = new QPushButton{};
             btn->setText(QString::fromUtf8(text));
             Py_INCREF(callback);
             QObject::connect(btn, &QPushButton::clicked, container, [callback, container]() {
               auto gil = PyGILState_Ensure();
               auto* win = container->window();
               auto* frame = dynamic_cast<tb::ui::MapWindow*>(win);
               auto* prev = g_currentFrame;
               if (frame != nullptr)
               {
                 g_currentFrame = frame;
               }
               auto* result = PyObject_CallObject(callback, nullptr);
               if (result == nullptr)
               {
                 PyErr_Print();
                 if (g_currentFrame != nullptr)
                 {
                   g_currentFrame->pythonLogger().error() << "Error in button callback";
                 }
               }
               Py_XDECREF(result);
               g_currentFrame = prev;
               PyGILState_Release(gil);
             });
             QObject::connect(btn, &QObject::destroyed, container, [callback]() {
               auto gil = PyGILState_Ensure();
               Py_DECREF(callback);
               PyGILState_Release(gil);
             });
             layout->addWidget(btn);
             Py_RETURN_NONE;
           },
           METH_VARARGS,
           nullptr},
      {nullptr, nullptr, 0, nullptr},
    };

    static auto pluginPanelType = PyTypeObject{};
    pluginPanelType.tp_name = "tb.PluginPanel";
    pluginPanelType.tp_basicsize = sizeof(PyTbPluginPanel);
    pluginPanelType.tp_flags = Py_TPFLAGS_DEFAULT;
    pluginPanelType.tp_dealloc = (destructor)freePluginPanelObject;
    pluginPanelType.tp_methods = pluginPanelMethods;

    if (PyType_Ready(&pluginPanelType) != 0)
    {
      return false;
    }
    g_pluginPanelType = &pluginPanelType;

    Py_INCREF(g_pluginPanelType);
    if (PyModule_AddObject(module, "PluginPanel", reinterpret_cast<PyObject*>(g_pluginPanelType)) != 0)
    {
      Py_DECREF(g_pluginPanelType);
      return false;
    }
  }
  return true;
}

} // namespace tb::ui
