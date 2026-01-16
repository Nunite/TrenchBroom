/*
 Copyright (C) 2026 Lws

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

#include "PythonScripting.h"

#if defined(slots)
#undef slots
#endif
#include <Python.h>

#include "Exceptions.h"
#include "Logger.h"
#include "kdl/vector_utils.h"
#include "kdl/overload.h"
#include "mdl/BrushFaceHandle.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/EntityNode.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/PatchNode.h"
#include "mdl/WorldNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Geometry.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Transaction.h"
#include "mdl/VertexHandleManager.h"
#include "ui/Actions.h"
#include "ui/MapFrame.h"
#include "ui/MapDocument.h"
#include "ui/Inspector.h"
#include "ui/PluginInspector.h"
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QSpinBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "vm/segment.h"

#include <string>

namespace tb::ui
{
namespace
{
thread_local MapFrame* g_currentFrame = nullptr;
bool g_pythonRegistered = false;

struct PyTbDocument
{
  PyObject_HEAD tb::ui::MapDocument* document;
};

struct PyTbSelection
{
  PyObject_HEAD tb::ui::MapDocument* document;
};

struct PyTbEntity
{
  PyObject_HEAD tb::ui::MapDocument* document;
  tb::mdl::EntityNodeBase* entityNode;
};

struct PyTbLogWriter
{
  PyObject_HEAD int isError;
};

struct PyTbTransaction
{
  PyObject_HEAD tb::ui::MapDocument* document;
  PyObject* name;
  tb::mdl::Transaction* transaction;
};

PyTypeObject* g_documentType = nullptr;
PyTypeObject* g_selectionType = nullptr;
PyTypeObject* g_entityType = nullptr;
PyTypeObject* g_logWriterType = nullptr;
PyTypeObject* g_transactionType = nullptr;
PyTypeObject* g_pluginPanelType = nullptr;

struct PyTbPluginPanel
{
  PyObject_HEAD QPointer<QWidget>* container;
};

PyObject* toPyString(const std::string& str)
{
  return PyUnicode_FromStringAndSize(str.c_str(), static_cast<Py_ssize_t>(str.size()));
}

PyObject* toPyVec3dTuple(const vm::vec3d& v)
{
  auto* tuple = PyTuple_New(3);
  if (tuple == nullptr)
  {
    return nullptr;
  }

  auto* x = PyFloat_FromDouble(v.x());
  if (x == nullptr)
  {
    Py_DECREF(tuple);
    return nullptr;
  }
  PyTuple_SET_ITEM(tuple, 0, x);

  auto* y = PyFloat_FromDouble(v.y());
  if (y == nullptr)
  {
    Py_DECREF(tuple);
    return nullptr;
  }
  PyTuple_SET_ITEM(tuple, 1, y);

  auto* z = PyFloat_FromDouble(v.z());
  if (z == nullptr)
  {
    Py_DECREF(tuple);
    return nullptr;
  }
  PyTuple_SET_ITEM(tuple, 2, z);

  return tuple;
}

tb::ui::MapDocument* activeDocument()
{
  return g_currentFrame ? &g_currentFrame->document() : nullptr;
}

void freePythonObject(PyObject* self)
{
  PyObject_Del(self);
}

void freePluginPanelObject(PyObject* self)
{
  auto* obj = reinterpret_cast<PyTbPluginPanel*>(self);
  delete obj->container;
  obj->container = nullptr;
  PyObject_Del(self);
}

void freeTransactionObject(PyObject* self)
{
  auto* obj = reinterpret_cast<PyTbTransaction*>(self);
  if (obj->transaction != nullptr)
  {
    obj->transaction->cancel();
    delete obj->transaction;
    obj->transaction = nullptr;
  }
  Py_XDECREF(obj->name);
  obj->name = nullptr;
  PyObject_Del(self);
}

PyObject* createLogWriterObject(const int isError)
{
  if (g_logWriterType == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "tb._LogWriter type is not initialized");
    return nullptr;
  }

  auto* obj = PyObject_New(PyTbLogWriter, g_logWriterType);
  if (obj == nullptr)
  {
    return nullptr;
  }
  obj->isError = isError;
  return reinterpret_cast<PyObject*>(obj);
}

PyObject* createDocumentObject(tb::ui::MapDocument* document)
{
  if (g_documentType == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "tb.Document type is not initialized");
    return nullptr;
  }

  auto* obj = PyObject_New(PyTbDocument, g_documentType);
  if (obj == nullptr)
  {
    return nullptr;
  }
  obj->document = document;
  return reinterpret_cast<PyObject*>(obj);
}

PyObject* createSelectionObject(tb::ui::MapDocument* document)
{
  if (g_selectionType == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "tb.Selection type is not initialized");
    return nullptr;
  }

  auto* obj = PyObject_New(PyTbSelection, g_selectionType);
  if (obj == nullptr)
  {
    return nullptr;
  }
  obj->document = document;
  return reinterpret_cast<PyObject*>(obj);
}

PyObject* createEntityObject(tb::ui::MapDocument* document, tb::mdl::EntityNodeBase* node)
{
  if (g_entityType == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "tb.Entity type is not initialized");
    return nullptr;
  }

  auto* obj = PyObject_New(PyTbEntity, g_entityType);
  if (obj == nullptr)
  {
    return nullptr;
  }
  obj->document = document;
  obj->entityNode = node;
  return reinterpret_cast<PyObject*>(obj);
}

PyObject* createTransactionObject(tb::ui::MapDocument* document, PyObject* name)
{
  if (g_transactionType == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "tb.Transaction type is not initialized");
    return nullptr;
  }

  auto* obj = PyObject_New(PyTbTransaction, g_transactionType);
  if (obj == nullptr)
  {
    return nullptr;
  }

  obj->document = document;
  obj->transaction = nullptr;

  if (name != nullptr)
  {
    Py_INCREF(name);
    obj->name = name;
  }
  else
  {
    obj->name = toPyString("Python Script");
    if (obj->name == nullptr)
    {
      PyObject_Del(obj);
      return nullptr;
    }
  }

  return reinterpret_cast<PyObject*>(obj);
}

PyObject* createPluginPanelObject(QWidget* container)
{
  if (g_pluginPanelType == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "tb.PluginPanel type is not initialized");
    return nullptr;
  }
  auto* obj = PyObject_New(PyTbPluginPanel, g_pluginPanelType);
  if (obj == nullptr)
  {
    return nullptr;
  }
  obj->container = new QPointer<QWidget>{container};
  return reinterpret_cast<PyObject*>(obj);
}

tb::ui::MapDocument* getDocumentFromPy(PyObject* self)
{
  if (g_documentType == nullptr || !PyObject_TypeCheck(self, g_documentType))
  {
    PyErr_SetString(PyExc_TypeError, "expected tb.Document");
    return nullptr;
  }

  auto* doc = reinterpret_cast<PyTbDocument*>(self)->document;
  if (doc == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "Document is null");
    return nullptr;
  }
  return doc;
}

tb::ui::MapDocument* getDocumentFromSelectionPy(PyObject* self)
{
  if (g_selectionType == nullptr || !PyObject_TypeCheck(self, g_selectionType))
  {
    PyErr_SetString(PyExc_TypeError, "expected tb.Selection");
    return nullptr;
  }

  auto* doc = reinterpret_cast<PyTbSelection*>(self)->document;
  if (doc == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "Selection has no document");
    return nullptr;
  }
  return doc;
}

PyTbEntity* getEntityFromPy(PyObject* self)
{
  if (g_entityType == nullptr || !PyObject_TypeCheck(self, g_entityType))
  {
    PyErr_SetString(PyExc_TypeError, "expected tb.Entity");
    return nullptr;
  }

  auto* entity = reinterpret_cast<PyTbEntity*>(self);
  if (entity->document == nullptr || entity->entityNode == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "Entity is not valid");
    return nullptr;
  }
  return entity;
}

PyTbLogWriter* getLogWriterFromPy(PyObject* self)
{
  if (g_logWriterType == nullptr || !PyObject_TypeCheck(self, g_logWriterType))
  {
    PyErr_SetString(PyExc_TypeError, "expected tb._LogWriter");
    return nullptr;
  }

  return reinterpret_cast<PyTbLogWriter*>(self);
}

PyTbTransaction* getTransactionFromPy(PyObject* self)
{
  if (g_transactionType == nullptr || !PyObject_TypeCheck(self, g_transactionType))
  {
    PyErr_SetString(PyExc_TypeError, "expected tb.Transaction");
    return nullptr;
  }

  auto* tx = reinterpret_cast<PyTbTransaction*>(self);
  if (tx->document == nullptr || tx->name == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "Transaction is not valid");
    return nullptr;
  }

  return tx;
}

PyTbPluginPanel* getPluginPanelFromPy(PyObject* self)
{
  if (g_pluginPanelType == nullptr || !PyObject_TypeCheck(self, g_pluginPanelType))
  {
    PyErr_SetString(PyExc_TypeError, "expected tb.PluginPanel");
    return nullptr;
  }
  auto* panel = reinterpret_cast<PyTbPluginPanel*>(self);
  if (panel->container == nullptr || panel->container->data() == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "PluginPanel is not valid");
    return nullptr;
  }
  return panel;
}

PyObject* plugin_panel_clear(PyObject* self, PyObject*)
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

PyObject* plugin_panel_add_label(PyObject* self, PyObject* args)
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

QString plugin_panel_object_name(const QString& prefix, const char* key)
{
  return QStringLiteral("tb_py_panel_%1_%2").arg(prefix, QString::fromUtf8(key));
}

PyObject* plugin_panel_add_label_named(PyObject* self, PyObject* args)
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

PyObject* plugin_panel_set_label_text(PyObject* self, PyObject* args)
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

PyObject* plugin_panel_add_int_field(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  const char* labelText = nullptr;
  int value = 0;
  int minValue = 0;
  int maxValue = 999999;
  if (!PyArg_ParseTuple(args, "ssi|ii", &key, &labelText, &value, &minValue, &maxValue))
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

PyObject* plugin_panel_add_float_field(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  const char* labelText = nullptr;
  double value = 0.0;
  double minValue = -1.0e9;
  double maxValue = 1.0e9;
  int decimals = 3;
  double step = 1.0;
  if (!PyArg_ParseTuple(args, "ssd|ddid", &key, &labelText, &value, &minValue, &maxValue, &decimals, &step))
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

PyObject* plugin_panel_get_int_field(PyObject* self, PyObject* args)
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

PyObject* plugin_panel_get_float_field(PyObject* self, PyObject* args)
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

PyObject* plugin_panel_add_checkbox(PyObject* self, PyObject* args)
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

PyObject* plugin_panel_get_checkbox(PyObject* self, PyObject* args)
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

PyObject* plugin_panel_add_combo_box(PyObject* self, PyObject* args)
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
      // 4 arguments: Check if the 4th is callback or index
      if (PyCallable_Check(callbackObj))
      {
        // It's a callback
        initialIndex = 0;
      }
      else if (PyLong_Check(callbackObj))
      {
        // It's an index
        initialIndex = (int)PyLong_AsLong(callbackObj);
        callbackObj = nullptr;
      }
      else if (callbackObj == Py_None)
      {
        callbackObj = nullptr;
        initialIndex = 0;
      }
      else
      {
        // Invalid type for 4th argument, let error handling below catch it if needed, 
        // or treat as error now. But strict PyCallable_Check below handles it.
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
      auto* frame = dynamic_cast<tb::ui::MapFrame*>(win);
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
          g_currentFrame->pythonLogger().error("Error in combo box callback");
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

PyObject* plugin_panel_get_combo_box_index(PyObject* self, PyObject* args)
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

PyObject* plugin_panel_get_combo_box_text(PyObject* self, PyObject* args)
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

PyObject* plugin_panel_set_text(PyObject* self, PyObject* args)
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

PyObject* plugin_panel_set_html(PyObject* self, PyObject* args)
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

PyObject* plugin_panel_add_button(PyObject* self, PyObject* args)
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
      auto* frame = dynamic_cast<tb::ui::MapFrame*>(win);
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
        auto context = ActionExecutionContext{frame, frame->currentMapViewBase()};
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

PyObject* log_writer_write(PyObject* self, PyObject* args)
{
  PyObject* value = nullptr;
  if (!PyArg_ParseTuple(args, "O", &value))
  {
    return nullptr;
  }

  auto* writer = getLogWriterFromPy(self);
  if (writer == nullptr)
  {
    return nullptr;
  }

  auto* strObj = PyObject_Str(value);
  if (strObj == nullptr)
  {
    return nullptr;
  }

  Py_ssize_t size = 0;
  const auto* utf8 = PyUnicode_AsUTF8AndSize(strObj, &size);
  if (utf8 == nullptr)
  {
    Py_DECREF(strObj);
    return nullptr;
  }

  auto message = std::string_view{utf8, static_cast<size_t>(size)};
  while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
  {
    message.remove_suffix(1);
  }

  if (!message.empty() && g_currentFrame != nullptr)
  {
    if (writer->isError != 0)
    {
      g_currentFrame->pythonLogger().error(message);
    }
    else
    {
      g_currentFrame->pythonLogger().info(message);
    }
  }

  Py_DECREF(strObj);
  return PyLong_FromSsize_t(size);
}

PyObject* log_writer_flush(PyObject*, PyObject*)
{
  Py_RETURN_NONE;
}

PyObject* log_writer_isatty(PyObject*, PyObject*)
{
  Py_RETURN_FALSE;
}

PyObject* document_selection(PyObject* self, PyObject*)
{
  auto* doc = getDocumentFromPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }
  return createSelectionObject(doc);
}

PyObject* document_get_selection(PyObject* self, void*)
{
  return document_selection(self, nullptr);
}

PyObject* document_transaction(PyObject* self, PyObject* args)
{
  PyObject* nameObj = nullptr;
  if (!PyArg_ParseTuple(args, "|U", &nameObj))
  {
    return nullptr;
  }

  auto* doc = getDocumentFromPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }

  return createTransactionObject(doc, nameObj);
}

PyObject* document_entities(PyObject* self, void*)
{
  auto* doc = getDocumentFromPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }

  auto result = std::vector<tb::mdl::EntityNodeBase*>{};

  doc->map().world()->accept(kdl::overload(
    [&](auto&& thisLambda, tb::mdl::WorldNode* worldNode) {
      result.push_back(worldNode);
      worldNode->visitChildren(thisLambda);
    },
    [&](auto&& thisLambda, tb::mdl::LayerNode* layerNode) {
      layerNode->visitChildren(thisLambda);
    },
    [&](auto&& thisLambda, tb::mdl::GroupNode* groupNode) {
      groupNode->visitChildren(thisLambda);
    },
    [&](tb::mdl::EntityNode* entityNode) { result.push_back(entityNode); },
    [&](tb::mdl::BrushNode* brushNode) { result.push_back(brushNode->entity()); },
    [&](tb::mdl::PatchNode* patchNode) { result.push_back(patchNode->entity()); }));

  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());

  auto* list = PyList_New(static_cast<Py_ssize_t>(result.size()));
  if (list == nullptr)
  {
    return nullptr;
  }

  Py_ssize_t index = 0;
  for (auto* entityNode : result)
  {
    auto* obj = createEntityObject(doc, entityNode);
    if (obj == nullptr)
    {
      Py_DECREF(list);
      return nullptr;
    }
    PyList_SET_ITEM(list, index, obj);
    ++index;
  }

  return list;
}

PyObject* document_vertex_tool_vertices(PyObject* self, PyObject*)
{
  auto* doc = getDocumentFromPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }

  try
  {
    const auto vertices = doc->map().vertexHandles().selectedHandles();

    auto* list = PyList_New(static_cast<Py_ssize_t>(vertices.size()));
    if (list == nullptr)
    {
      return nullptr;
    }

    Py_ssize_t index = 0;
    for (const auto& v : vertices)
    {
      auto* tuple = toPyVec3dTuple(v);
      if (tuple == nullptr)
      {
        Py_DECREF(list);
        return nullptr;
      }
      PyList_SET_ITEM(list, index, tuple);
      ++index;
    }

    return list;
  }
  catch (const tb::Exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (const std::exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (...)
  {
    PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
    return nullptr;
  }
}

PyObject* transaction_enter(PyObject* self, PyObject*)
{
  auto* tx = getTransactionFromPy(self);
  if (tx == nullptr)
  {
    return nullptr;
  }

  if (tx->transaction != nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "Transaction already started");
    return nullptr;
  }

  Py_ssize_t size = 0;
  const auto* nameUtf8 = PyUnicode_AsUTF8AndSize(tx->name, &size);
  if (nameUtf8 == nullptr)
  {
    return nullptr;
  }

  try
  {
    tx->transaction =
      new tb::mdl::Transaction{tx->document->map(), std::string(nameUtf8, static_cast<size_t>(size))};
  }
  catch (const tb::Exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (const std::exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (...)
  {
    PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
    return nullptr;
  }

  Py_INCREF(self);
  return self;
}

PyObject* transaction_commit(PyObject* self, PyObject*)
{
  auto* tx = getTransactionFromPy(self);
  if (tx == nullptr)
  {
    return nullptr;
  }

  if (tx->transaction == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "Transaction not started");
    return nullptr;
  }

  try
  {
    const auto ok = tx->transaction->commit();
    delete tx->transaction;
    tx->transaction = nullptr;
    if (ok)
    {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
  }
  catch (const tb::Exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (const std::exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (...)
  {
    PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
    return nullptr;
  }
}

PyObject* transaction_cancel(PyObject* self, PyObject*)
{
  auto* tx = getTransactionFromPy(self);
  if (tx == nullptr)
  {
    return nullptr;
  }

  if (tx->transaction == nullptr)
  {
    Py_RETURN_NONE;
  }

  try
  {
    tx->transaction->cancel();
    delete tx->transaction;
    tx->transaction = nullptr;
    Py_RETURN_NONE;
  }
  catch (const tb::Exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (const std::exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (...)
  {
    PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
    return nullptr;
  }
}

PyObject* transaction_rollback(PyObject* self, PyObject*)
{
  auto* tx = getTransactionFromPy(self);
  if (tx == nullptr)
  {
    return nullptr;
  }

  if (tx->transaction == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "Transaction not started");
    return nullptr;
  }

  try
  {
    tx->transaction->rollback();
    Py_RETURN_NONE;
  }
  catch (const tb::Exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (const std::exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (...)
  {
    PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
    return nullptr;
  }
}

PyObject* transaction_exit(PyObject* self, PyObject* args)
{
  PyObject* excType = nullptr;
  PyObject* excValue = nullptr;
  PyObject* excTraceback = nullptr;
  if (!PyArg_ParseTuple(args, "OOO", &excType, &excValue, &excTraceback))
  {
    return nullptr;
  }

  const auto hasException = excType != Py_None;

  if (hasException)
  {
    auto* r = transaction_cancel(self, nullptr);
    Py_XDECREF(r);
    Py_RETURN_FALSE;
  }

  auto* r = transaction_commit(self, nullptr);
  if (r == nullptr)
  {
    return nullptr;
  }
  Py_DECREF(r);
  Py_RETURN_FALSE;
}

PyObject* selection_call(PyObject* self, PyObject* args, PyObject* kwargs)
{
  if ((args != nullptr && PyTuple_Size(args) != 0) || (kwargs != nullptr && PyDict_Size(kwargs) != 0))
  {
    PyErr_SetString(PyExc_TypeError, "Selection() takes no arguments");
    return nullptr;
  }

  Py_INCREF(self);
  return self;
}

PyObject* selection_entities(PyObject* self, PyObject*)
{
  auto* doc = getDocumentFromSelectionPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }

  auto& map = doc->map();
  const auto& entities = map.selection().entities;

  auto* list = PyList_New(static_cast<Py_ssize_t>(entities.size()));
  if (list == nullptr)
  {
    return nullptr;
  }

  Py_ssize_t index = 0;
  for (auto* entityNode : entities)
  {
    auto* obj = createEntityObject(doc, entityNode);
    if (obj == nullptr)
    {
      Py_DECREF(list);
      return nullptr;
    }
    PyList_SET_ITEM(list, index, obj);
    ++index;
  }

  return list;
}

PyObject* selection_get_entities(PyObject* self, void*)
{
  return selection_entities(self, nullptr);
}

PyObject* selection_all_entities(PyObject* self, PyObject*)
{
  auto* doc = getDocumentFromSelectionPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }

  auto& map = doc->map();
  const auto& entities = map.selection().allEntities();

  auto* list = PyList_New(static_cast<Py_ssize_t>(entities.size()));
  if (list == nullptr)
  {
    return nullptr;
  }

  Py_ssize_t index = 0;
  for (auto* entityNode : entities)
  {
    auto* obj = createEntityObject(doc, entityNode);
    if (obj == nullptr)
    {
      Py_DECREF(list);
      return nullptr;
    }
    PyList_SET_ITEM(list, index, obj);
    ++index;
  }

  return list;
}

PyObject* selection_get_all_entities(PyObject* self, void*)
{
  return selection_all_entities(self, nullptr);
}

PyObject* selection_brush_vertices(PyObject* self, PyObject*)
{
  auto* doc = getDocumentFromSelectionPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }

  auto& map = doc->map();
  const auto& selection = map.selection();

  auto brushNodes = std::vector<tb::mdl::BrushNode*>{};
  brushNodes.insert(
    brushNodes.end(), selection.allBrushes().begin(), selection.allBrushes().end());

  if (selection.hasBrushFaces())
  {
    auto nodesFromFaces = tb::mdl::toNodes(selection.brushFaces);
    brushNodes.insert(brushNodes.end(), nodesFromFaces.begin(), nodesFromFaces.end());
  }

  brushNodes = kdl::vec_sort_and_remove_duplicates(std::move(brushNodes));

  auto* outer = PyList_New(static_cast<Py_ssize_t>(brushNodes.size()));
  if (outer == nullptr)
  {
    return nullptr;
  }

  Py_ssize_t brushIndex = 0;
  for (auto* brushNode : brushNodes)
  {
    const auto vertices = brushNode->brush().vertexPositions();
    auto* inner = PyList_New(static_cast<Py_ssize_t>(vertices.size()));
    if (inner == nullptr)
    {
      Py_DECREF(outer);
      return nullptr;
    }

    Py_ssize_t vertexIndex = 0;
    for (const auto& v : vertices)
    {
      auto* tuple = toPyVec3dTuple(v);
      if (tuple == nullptr)
      {
        Py_DECREF(inner);
        Py_DECREF(outer);
        return nullptr;
      }
      PyList_SET_ITEM(inner, vertexIndex, tuple);
      ++vertexIndex;
    }

    PyList_SET_ITEM(outer, brushIndex, inner);
    ++brushIndex;
  }

  return outer;
}

PyObject* selection_add(PyObject* self, PyObject* args)
{
  PyObject* list = nullptr;
  if (!PyArg_ParseTuple(args, "O", &list))
  {
    return nullptr;
  }

  if (!PyList_Check(list))
  {
    PyErr_SetString(PyExc_TypeError, "expected a list of entities");
    return nullptr;
  }

  auto* doc = getDocumentFromSelectionPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }

  auto nodes = std::vector<tb::mdl::Node*>{};
  const auto& currentSelection = doc->map().selection().nodes;
  nodes.insert(nodes.end(), currentSelection.begin(), currentSelection.end());

  const auto size = PyList_Size(list);
  for (Py_ssize_t i = 0; i < size; ++i)
  {
    auto* item = PyList_GetItem(list, i);
    auto* entity = getEntityFromPy(item);
    if (entity == nullptr)
    {
      return nullptr;
    }
    nodes.push_back(entity->entityNode);
  }

  try
  {
    tb::mdl::selectNodes(doc->map(), nodes);
    Py_RETURN_NONE;
  }
  catch (const tb::Exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (const std::exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (...)
  {
    PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
    return nullptr;
  }
}

PyObject* selection_set(PyObject* self, PyObject* args)
{
  PyObject* list = nullptr;
  if (!PyArg_ParseTuple(args, "O", &list))
  {
    return nullptr;
  }

  if (!PyList_Check(list))
  {
    PyErr_SetString(PyExc_TypeError, "expected a list of entities");
    return nullptr;
  }

  auto* doc = getDocumentFromSelectionPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }

  auto nodes = std::vector<tb::mdl::Node*>{};

  const auto size = PyList_Size(list);
  for (Py_ssize_t i = 0; i < size; ++i)
  {
    auto* item = PyList_GetItem(list, i);
    auto* entity = getEntityFromPy(item);
    if (entity == nullptr)
    {
      return nullptr;
    }
    nodes.push_back(entity->entityNode);
  }

  try
  {
    tb::mdl::selectNodes(doc->map(), nodes);
    Py_RETURN_NONE;
  }
  catch (const tb::Exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (const std::exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (...)
  {
    PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
    return nullptr;
  }
}

PyObject* selection_clear(PyObject* self, PyObject*)
{
  auto* doc = getDocumentFromSelectionPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }

  try
  {
    tb::mdl::deselectAll(doc->map());
    Py_RETURN_NONE;
  }
  catch (const tb::Exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (const std::exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (...)
  {
    PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
    return nullptr;
  }
}

PyObject* selection_set_property(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  const char* value = nullptr;
  int defaultToProtected = 0;
  if (!PyArg_ParseTuple(args, "ss|p", &key, &value, &defaultToProtected))
  {
    return nullptr;
  }

  auto* doc = getDocumentFromSelectionPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }

  try
  {
    const auto ok =
      tb::mdl::setEntityProperty(doc->map(), std::string{key}, std::string{value}, defaultToProtected != 0);
    if (ok)
    {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
  }
  catch (const tb::Exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (const std::exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (...)
  {
    PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
    return nullptr;
  }
}

PyObject* selection_duplicate(PyObject* self, PyObject*)
{
  auto* doc = getDocumentFromSelectionPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }

  try
  {
    tb::mdl::duplicateSelectedNodes(doc->map());
    Py_RETURN_NONE;
  }
  catch (const tb::Exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (const std::exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (...)
  {
    PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
    return nullptr;
  }
}

PyObject* selection_translate(PyObject* self, PyObject* args)
{
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  if (!PyArg_ParseTuple(args, "ddd", &x, &y, &z))
  {
    return nullptr;
  }

  auto* doc = getDocumentFromSelectionPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }

  try
  {
    const auto ok = tb::mdl::translateSelection(doc->map(), vm::vec3d{x, y, z});
    if (ok)
    {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
  }
  catch (const tb::Exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (const std::exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (...)
  {
    PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
    return nullptr;
  }
}

PyObject* selection_rotate(PyObject* self, PyObject* args)
{
  double axisX = 0.0;
  double axisY = 0.0;
  double axisZ = 0.0;
  double angleDegrees = 0.0;
  double centerX = 0.0;
  double centerY = 0.0;
  double centerZ = 0.0;

  if (!PyArg_ParseTuple(args, "dddd|ddd", &axisX, &axisY, &axisZ, &angleDegrees, &centerX, &centerY, &centerZ))
  {
    return nullptr;
  }

  auto* doc = getDocumentFromSelectionPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }

  try
  {
    auto& map = doc->map();

    auto center = vm::vec3d{};
    const auto argCount = PyTuple_Size(args);
    if (argCount >= 7)
    {
      center = vm::vec3d{centerX, centerY, centerZ};
    }
    else
    {
      const auto bounds = map.selectionBounds();
      if (!bounds)
      {
        PyErr_SetString(PyExc_RuntimeError, "Selection bounds are not available");
        return nullptr;
      }

      center = bounds->min + bounds->size() / 2.0;
    }

    constexpr double pi = 3.1415926535897932384626433832795;
    const auto angleRadians = angleDegrees * (pi / 180.0);

    const auto ok =
      tb::mdl::rotateSelection(map, center, vm::vec3d{axisX, axisY, axisZ}, angleRadians);
    if (ok)
    {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
  }
  catch (const tb::Exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (const std::exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (...)
  {
    PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
    return nullptr;
  }
}

PyObject* selection_scale(PyObject* self, PyObject* args)
{
  double scaleX = 0.0;
  double scaleY = 0.0;
  double scaleZ = 0.0;
  double centerX = 0.0;
  double centerY = 0.0;
  double centerZ = 0.0;

  if (!PyArg_ParseTuple(args, "ddd|ddd", &scaleX, &scaleY, &scaleZ, &centerX, &centerY, &centerZ))
  {
    return nullptr;
  }

  auto* doc = getDocumentFromSelectionPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }

  try
  {
    auto& map = doc->map();

    auto center = vm::vec3d{};
    const auto argCount = PyTuple_Size(args);
    if (argCount >= 6)
    {
      center = vm::vec3d{centerX, centerY, centerZ};
    }
    else
    {
      const auto bounds = map.selectionBounds();
      if (!bounds)
      {
        PyErr_SetString(PyExc_RuntimeError, "Selection bounds are not available");
        return nullptr;
      }

      center = bounds->min + bounds->size() / 2.0;
    }

    const auto ok =
      tb::mdl::scaleSelection(map, center, vm::vec3d{scaleX, scaleY, scaleZ});
    if (ok)
    {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
  }
  catch (const tb::Exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (const std::exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (...)
  {
    PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
    return nullptr;
  }
}

PyObject* selection_chamfer_vertices(PyObject* self, PyObject* args)
{
  double distance = 0.0;
  if (!PyArg_ParseTuple(args, "d", &distance))
  {
    return nullptr;
  }

  auto* doc = getDocumentFromSelectionPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }

  try
  {
    auto& map = doc->map();
    const auto ok = tb::mdl::chamferVertices(
      map, "Chamfer Vertices", map.vertexHandles().selectedHandles(), distance);
    if (ok)
    {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
  }
  catch (const tb::Exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (const std::exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (...)
  {
    PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
    return nullptr;
  }
}

PyObject* selection_chamfer_edges(PyObject* self, PyObject* args)
{
  double distance = 0.0;
  int segments = 1;
  if (!PyArg_ParseTuple(args, "d|i", &distance, &segments))
  {
    return nullptr;
  }

  if (segments < 1)
  {
    segments = 1;
  }

  auto* doc = getDocumentFromSelectionPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }

  try
  {
    auto& map = doc->map();
    const auto ok = tb::mdl::chamferEdges(
      map, "Chamfer Edges", map.edgeHandles().selectedHandles(), distance, segments);
    if (ok)
    {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
  }
  catch (const tb::Exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (const std::exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (...)
  {
    PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
    return nullptr;
  }
}

PyObject* selection_remove_property(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  if (!PyArg_ParseTuple(args, "s", &key))
  {
    return nullptr;
  }

  auto* doc = getDocumentFromSelectionPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }

  try
  {
    const auto ok = tb::mdl::removeEntityProperty(doc->map(), std::string{key});
    if (ok)
    {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
  }
  catch (const tb::Exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (const std::exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (...)
  {
    PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
    return nullptr;
  }
}

PyObject* selection_rename_property(PyObject* self, PyObject* args)
{
  const char* oldKey = nullptr;
  const char* newKey = nullptr;
  if (!PyArg_ParseTuple(args, "ss", &oldKey, &newKey))
  {
    return nullptr;
  }

  auto* doc = getDocumentFromSelectionPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }

  try
  {
    const auto ok =
      tb::mdl::renameEntityProperty(doc->map(), std::string{oldKey}, std::string{newKey});
    if (ok)
    {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
  }
  catch (const tb::Exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (const std::exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
  catch (...)
  {
    PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
    return nullptr;
  }
}

PyObject* entity_classname(PyObject* self, PyObject*)
{
  auto* entity = getEntityFromPy(self);
  if (entity == nullptr)
  {
    return nullptr;
  }

  const auto& classname = entity->entityNode->entity().classname();
  return toPyString(classname);
}

PyObject* entity_get_classname(PyObject* self, void*)
{
  return entity_classname(self, nullptr);
}

PyObject* entity_keys(PyObject* self, PyObject*)
{
  auto* entity = getEntityFromPy(self);
  if (entity == nullptr)
  {
    return nullptr;
  }

  const auto keys = entity->entityNode->entity().propertyKeys();
  auto* list = PyList_New(static_cast<Py_ssize_t>(keys.size()));
  if (list == nullptr)
  {
    return nullptr;
  }

  Py_ssize_t index = 0;
  for (const auto& key : keys)
  {
    auto* pyKey = toPyString(key);
    if (pyKey == nullptr)
    {
      Py_DECREF(list);
      return nullptr;
    }
    PyList_SET_ITEM(list, index, pyKey);
    ++index;
  }

  return list;
}

PyObject* entity_get(PyObject* self, PyObject* args)
{
  const char* key = nullptr;
  PyObject* defaultValue = Py_None;
  if (!PyArg_ParseTuple(args, "s|O", &key, &defaultValue))
  {
    return nullptr;
  }

  auto* entity = getEntityFromPy(self);
  if (entity == nullptr)
  {
    return nullptr;
  }

  const auto* value = entity->entityNode->entity().property(std::string{key});
  if (value == nullptr)
  {
    Py_INCREF(defaultValue);
    return defaultValue;
  }

  return toPyString(*value);
}

PyObject* entity_getitem(PyObject* self, PyObject* key)
{
  auto* entity = getEntityFromPy(self);
  if (entity == nullptr)
  {
    return nullptr;
  }

  const char* keyStr = PyUnicode_AsUTF8(key);
  if (keyStr == nullptr)
  {
    return nullptr;
  }

  const auto* value = entity->entityNode->entity().property(std::string{keyStr});
  if (value == nullptr)
  {
    PyErr_SetObject(PyExc_KeyError, key);
    return nullptr;
  }

  return toPyString(*value);
}

PyObject* entity_items(PyObject* self, PyObject*)
{
  auto* entity = getEntityFromPy(self);
  if (entity == nullptr)
  {
    return nullptr;
  }

  const auto keys = entity->entityNode->entity().propertyKeys();
  auto* list = PyList_New(static_cast<Py_ssize_t>(keys.size()));
  if (list == nullptr)
  {
    return nullptr;
  }

  Py_ssize_t index = 0;
  for (const auto& key : keys)
  {
    auto* pyKey = toPyString(key);
    if (pyKey == nullptr)
    {
      Py_DECREF(list);
      return nullptr;
    }

    const auto* value = entity->entityNode->entity().property(key);
    auto* pyValue = value ? toPyString(*value) : Py_None;
    if (pyValue == Py_None)
    {
       Py_INCREF(Py_None);
    }
    else if (pyValue == nullptr)
    {
       Py_DECREF(pyKey);
       Py_DECREF(list);
       return nullptr;
    }

    auto* item = PyTuple_Pack(2, pyKey, pyValue);
    Py_DECREF(pyKey);
    Py_DECREF(pyValue);

    if (item == nullptr)
    {
      Py_DECREF(list);
      return nullptr;
    }

    PyList_SET_ITEM(list, index, item);
    ++index;
  }

  return list;
}

PyObject* entity_richcompare(PyObject* self, PyObject* other, int op)
{
  if (!PyObject_TypeCheck(self, g_entityType) || !PyObject_TypeCheck(other, g_entityType))
  {
    Py_RETURN_NOTIMPLEMENTED;
  }

  auto* entity1 = reinterpret_cast<PyTbEntity*>(self);
  auto* entity2 = reinterpret_cast<PyTbEntity*>(other);

  const bool equal = entity1->entityNode == entity2->entityNode;

  if (op == Py_EQ)
  {
    return PyBool_FromLong(equal);
  }
  else if (op == Py_NE)
  {
    return PyBool_FromLong(!equal);
  }

  Py_RETURN_NOTIMPLEMENTED;
}

Py_hash_t entity_hash(PyObject* self)
{
  auto* entity = reinterpret_cast<PyTbEntity*>(self);
  return reinterpret_cast<Py_hash_t>(entity->entityNode);
}

PyObject* module_document(PyObject*, PyObject*)
{
  auto* doc = activeDocument();
  if (doc == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "No active MapFrame");
    return nullptr;
  }
  return createDocumentObject(doc);
}

PyObject* module_current_document(PyObject*, PyObject*)
{
  auto* doc = activeDocument();
  if (doc == nullptr)
  {
    Py_RETURN_NONE;
  }
  return createDocumentObject(doc);
}

PyObject* module_transaction(PyObject*, PyObject* args)
{
  PyObject* nameObj = nullptr;
  if (!PyArg_ParseTuple(args, "|U", &nameObj))
  {
    return nullptr;
  }

  auto* doc = activeDocument();
  if (doc == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "No active MapFrame");
    return nullptr;
  }

  return createTransactionObject(doc, nameObj);
}

bool registerTypes(PyObject* module)
{
  static auto documentType = PyTypeObject{};
  static auto selectionType = PyTypeObject{};
  static auto entityType = PyTypeObject{};
  static auto logWriterType = PyTypeObject{};
  static auto transactionType = PyTypeObject{};

  if (g_documentType == nullptr)
  {
    documentType = PyTypeObject{};
    documentType.tp_name = "tb.Document";
    documentType.tp_basicsize = sizeof(PyTbDocument);
    documentType.tp_flags = Py_TPFLAGS_DEFAULT;
    documentType.tp_dealloc = freePythonObject;

    static PyMethodDef documentMethods[] = {
      {"current",
       [](PyObject*, PyObject*) -> PyObject* { return module_current_document(nullptr, nullptr); },
       METH_CLASS | METH_NOARGS,
       nullptr},
      {"get_selection", document_selection, METH_NOARGS, nullptr},
      {"transaction", document_transaction, METH_VARARGS, nullptr},
      {"vertex_tool_vertices", document_vertex_tool_vertices, METH_NOARGS, nullptr},
      {nullptr, nullptr, 0, nullptr},
    };
    documentType.tp_methods = documentMethods;

    static PyGetSetDef documentGetSet[] = {
      {"selection", document_get_selection, nullptr, nullptr, nullptr},
      {"entities", document_entities, nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    documentType.tp_getset = documentGetSet;

    if (PyType_Ready(&documentType) != 0)
    {
      return false;
    }
    g_documentType = &documentType;

    Py_INCREF(g_documentType);
    if (PyModule_AddObject(module, "Document", reinterpret_cast<PyObject*>(g_documentType)) != 0)
    {
      Py_DECREF(g_documentType);
      return false;
    }
  }

  if (g_selectionType == nullptr)
  {
    selectionType = PyTypeObject{};
    selectionType.tp_name = "tb.Selection";
    selectionType.tp_basicsize = sizeof(PyTbSelection);
    selectionType.tp_flags = Py_TPFLAGS_DEFAULT;
    selectionType.tp_dealloc = freePythonObject;
    selectionType.tp_call = selection_call;

    static PyMethodDef selectionMethods[] = {
      {"brush_vertices", selection_brush_vertices, METH_NOARGS, nullptr},
      {"set_property", selection_set_property, METH_VARARGS, nullptr},
      {"add", selection_add, METH_VARARGS, nullptr},
      {"set", selection_set, METH_VARARGS, nullptr},
      {"duplicate", selection_duplicate, METH_NOARGS, nullptr},
      {"translate", selection_translate, METH_VARARGS, nullptr},
      {"rotate", selection_rotate, METH_VARARGS, nullptr},
      {"scale", selection_scale, METH_VARARGS, nullptr},
      {"chamfer_vertices", selection_chamfer_vertices, METH_VARARGS, nullptr},
      {"chamfer_edges", selection_chamfer_edges, METH_VARARGS, nullptr},
      {"remove_property", selection_remove_property, METH_VARARGS, nullptr},
      {"rename_property", selection_rename_property, METH_VARARGS, nullptr},
      {"clear", selection_clear, METH_NOARGS, nullptr},
      {nullptr, nullptr, 0, nullptr},
    };
    selectionType.tp_methods = selectionMethods;

    static PyGetSetDef selectionGetSet[] = {
      {"entities", selection_get_entities, nullptr, nullptr, nullptr},
      {"all_entities", selection_get_all_entities, nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    selectionType.tp_getset = selectionGetSet;

    if (PyType_Ready(&selectionType) != 0)
    {
      return false;
    }
    g_selectionType = &selectionType;

    Py_INCREF(g_selectionType);
    if (PyModule_AddObject(module, "Selection", reinterpret_cast<PyObject*>(g_selectionType)) != 0)
    {
      Py_DECREF(g_selectionType);
      return false;
    }
  }

  if (g_entityType == nullptr)
  {
    entityType = PyTypeObject{};
    entityType.tp_name = "tb.Entity";
    entityType.tp_basicsize = sizeof(PyTbEntity);
    entityType.tp_flags = Py_TPFLAGS_DEFAULT;
    entityType.tp_dealloc = freePythonObject;
    entityType.tp_richcompare = entity_richcompare;
    entityType.tp_hash = entity_hash;

    static PyMethodDef entityMethods[] = {
      {"keys", entity_keys, METH_NOARGS, nullptr},
      {"get", entity_get, METH_VARARGS, nullptr},
      {"items", entity_items, METH_NOARGS, nullptr},
      {nullptr, nullptr, 0, nullptr},
    };
    entityType.tp_methods = entityMethods;

    static PyGetSetDef entityGetSet[] = {
      {"classname", entity_get_classname, nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    entityType.tp_getset = entityGetSet;

    static PyMappingMethods entityMappingMethods = {
      nullptr,        /* mp_length */
      entity_getitem, /* mp_subscript */
      nullptr,        /* mp_ass_subscript */
    };
    entityType.tp_as_mapping = &entityMappingMethods;

    if (PyType_Ready(&entityType) != 0)
    {
      return false;
    }
    g_entityType = &entityType;

    Py_INCREF(g_entityType);
    if (PyModule_AddObject(module, "Entity", reinterpret_cast<PyObject*>(g_entityType)) != 0)
    {
      Py_DECREF(g_entityType);
      return false;
    }
  }

  if (g_logWriterType == nullptr)
  {
    logWriterType = PyTypeObject{};
    logWriterType.tp_name = "tb._LogWriter";
    logWriterType.tp_basicsize = sizeof(PyTbLogWriter);
    logWriterType.tp_flags = Py_TPFLAGS_DEFAULT;
    logWriterType.tp_dealloc = freePythonObject;

    static PyMethodDef logWriterMethods[] = {
      {"write", log_writer_write, METH_VARARGS, nullptr},
      {"flush", log_writer_flush, METH_NOARGS, nullptr},
      {"isatty", log_writer_isatty, METH_NOARGS, nullptr},
      {nullptr, nullptr, 0, nullptr},
    };
    logWriterType.tp_methods = logWriterMethods;

    if (PyType_Ready(&logWriterType) != 0)
    {
      return false;
    }
    g_logWriterType = &logWriterType;

    Py_INCREF(g_logWriterType);
    if (
      PyModule_AddObject(module, "_LogWriter", reinterpret_cast<PyObject*>(g_logWriterType))
      != 0)
    {
      Py_DECREF(g_logWriterType);
      return false;
    }
  }

  if (g_transactionType == nullptr)
  {
    transactionType = PyTypeObject{};
    transactionType.tp_name = "tb.Transaction";
    transactionType.tp_basicsize = sizeof(PyTbTransaction);
    transactionType.tp_flags = Py_TPFLAGS_DEFAULT;
    transactionType.tp_dealloc = freeTransactionObject;

    static PyMethodDef transactionMethods[] = {
      {"__enter__", transaction_enter, METH_NOARGS, nullptr},
      {"__exit__", transaction_exit, METH_VARARGS, nullptr},
      {"commit", transaction_commit, METH_NOARGS, nullptr},
      {"cancel", transaction_cancel, METH_NOARGS, nullptr},
      {"rollback", transaction_rollback, METH_NOARGS, nullptr},
      {nullptr, nullptr, 0, nullptr},
    };
    transactionType.tp_methods = transactionMethods;

    if (PyType_Ready(&transactionType) != 0)
    {
      return false;
    }
    g_transactionType = &transactionType;

    Py_INCREF(g_transactionType);
    if (
      PyModule_AddObject(module, "Transaction", reinterpret_cast<PyObject*>(g_transactionType))
      != 0)
    {
      Py_DECREF(g_transactionType);
      return false;
    }
  }

  if (g_pluginPanelType == nullptr)
  {
    static PyTypeObject pluginPanelType = PyTypeObject{};
    pluginPanelType = PyTypeObject{};
    pluginPanelType.tp_name = "tb.PluginPanel";
    pluginPanelType.tp_basicsize = sizeof(PyTbPluginPanel);
    pluginPanelType.tp_flags = Py_TPFLAGS_DEFAULT;
    pluginPanelType.tp_dealloc = freePluginPanelObject;

    static PyMethodDef pluginPanelMethods[] = {
      {"clear", plugin_panel_clear, METH_NOARGS, nullptr},
      {"add_label", plugin_panel_add_label, METH_VARARGS, nullptr},
      {"add_label_named", plugin_panel_add_label_named, METH_VARARGS, nullptr},
      {"set_label_text", plugin_panel_set_label_text, METH_VARARGS, nullptr},
      {"add_int_field", plugin_panel_add_int_field, METH_VARARGS, nullptr},
      {"add_float_field", plugin_panel_add_float_field, METH_VARARGS, nullptr},
      {"add_checkbox", plugin_panel_add_checkbox, METH_VARARGS, nullptr},
      {"add_combo_box", plugin_panel_add_combo_box, METH_VARARGS, nullptr},
      {"get_int_field", plugin_panel_get_int_field, METH_VARARGS, nullptr},
      {"get_float_field", plugin_panel_get_float_field, METH_VARARGS, nullptr},
      {"get_checkbox", plugin_panel_get_checkbox, METH_VARARGS, nullptr},
      {"get_combo_box_index", plugin_panel_get_combo_box_index, METH_VARARGS, nullptr},
      {"get_combo_box_text", plugin_panel_get_combo_box_text, METH_VARARGS, nullptr},
      {"set_text", plugin_panel_set_text, METH_VARARGS, nullptr},
      {"set_html", plugin_panel_set_html, METH_VARARGS, nullptr},
      {"add_button", plugin_panel_add_button, METH_VARARGS, nullptr},
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
           auto* frame = dynamic_cast<tb::ui::MapFrame*>(win);
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
               g_currentFrame->pythonLogger().error("Error in button callback");
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
    pluginPanelType.tp_methods = pluginPanelMethods;

    if (PyType_Ready(&pluginPanelType) != 0)
    {
      return false;
    }
    g_pluginPanelType = &pluginPanelType;

    Py_INCREF(g_pluginPanelType);
    if (
      PyModule_AddObject(module, "PluginPanel", reinterpret_cast<PyObject*>(g_pluginPanelType))
      != 0)
    {
      Py_DECREF(g_pluginPanelType);
      return false;
    }
  }

  return true;
}

bool ensureInitialized()
{
  if (!g_pythonRegistered)
  {
    if (PyImport_AppendInittab("tb", []() -> PyObject* {
          static PyMethodDef methods[] = {
            {"document", module_document, METH_NOARGS, nullptr},
            {"current_document", module_current_document, METH_NOARGS, nullptr},
            {"transaction", module_transaction, METH_VARARGS, nullptr},
            {"add_plugin_panel",
             [](PyObject*, PyObject* args) -> PyObject* {
               const char* title = nullptr;
               const char* content = nullptr;
               if (!PyArg_ParseTuple(args, "s|z", &title, &content))
               {
                 return nullptr;
               }
               if (g_currentFrame == nullptr)
               {
                 PyErr_SetString(PyExc_RuntimeError, "No active MapFrame");
                 return nullptr;
               }
               try
               {
                 const auto qTitle = QString::fromUtf8(title);
                 const auto qContent =
                   content != nullptr ? QString::fromUtf8(content) : QString{};
                 g_currentFrame->addPluginPanel(qTitle, qContent);
                 g_currentFrame->switchToInspectorPage(InspectorPage::Plugin);
                 Py_RETURN_NONE;
               }
               catch (const tb::Exception& e)
               {
                 PyErr_SetString(PyExc_RuntimeError, e.what());
                 return nullptr;
               }
               catch (const std::exception& e)
               {
                 PyErr_SetString(PyExc_RuntimeError, e.what());
                 return nullptr;
               }
               catch (...)
               {
                 PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
                 return nullptr;
               }
             },
             METH_VARARGS,
             nullptr},
            {"create_plugin_panel",
             [](PyObject*, PyObject* args) -> PyObject* {
               const char* title = nullptr;
               if (!PyArg_ParseTuple(args, "s", &title))
               {
                 return nullptr;
               }
               if (g_currentFrame == nullptr)
               {
                 PyErr_SetString(PyExc_RuntimeError, "No active MapFrame");
                 return nullptr;
               }
               try
               {
                 const auto qTitle = QString::fromUtf8(title);
                 auto* container = g_currentFrame->addPluginPanel(qTitle);
                 g_currentFrame->switchToInspectorPage(InspectorPage::Plugin);
                 return createPluginPanelObject(container);
               }
               catch (const tb::Exception& e)
               {
                 PyErr_SetString(PyExc_RuntimeError, e.what());
                 return nullptr;
               }
               catch (const std::exception& e)
               {
                 PyErr_SetString(PyExc_RuntimeError, e.what());
                 return nullptr;
               }
               catch (...)
               {
                 PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
                 return nullptr;
               }
             },
             METH_VARARGS,
             nullptr},
            {"execute_action",
             [](PyObject*, PyObject* args) -> PyObject* {
               const char* actionPath = nullptr;
               if (!PyArg_ParseTuple(args, "s", &actionPath))
                {
                 return nullptr;
               }

               if (g_currentFrame == nullptr)
               {
                 PyErr_SetString(PyExc_RuntimeError, "No active MapFrame");
                 return nullptr;
               }

               try
               {
                 const auto path = std::filesystem::path{actionPath};
                 const auto& actionsMap = ActionManager::instance().actionsMap();
                 const auto iAction = actionsMap.find(path);
                 if (iAction == std::end(actionsMap))
                 {
                   PyErr_SetString(PyExc_KeyError, actionPath);
                   return nullptr;
                 }

                 const auto& action = iAction->second;
                 auto context =
                   ActionExecutionContext{g_currentFrame, g_currentFrame->currentMapViewBase()};
                 if (!action.enabled(context))
                 {
                   PyErr_SetString(PyExc_RuntimeError, "Action is disabled");
                   return nullptr;
                 }
                 action.execute(context);
                 Py_RETURN_NONE;
               }
               catch (const tb::Exception& e)
               {
                 PyErr_SetString(PyExc_RuntimeError, e.what());
                 return nullptr;
               }
               catch (const std::exception& e)
               {
                 PyErr_SetString(PyExc_RuntimeError, e.what());
                 return nullptr;
               }
               catch (...)
               {
                 PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
                 return nullptr;
               }
             },
             METH_VARARGS,
             nullptr},
            {"list_actions",
             [](PyObject*, PyObject*) -> PyObject* {
               const auto& actionsMap = ActionManager::instance().actionsMap();
               auto* list = PyList_New(static_cast<Py_ssize_t>(actionsMap.size()));
               if (list == nullptr)
               {
                 return nullptr;
               }

               Py_ssize_t index = 0;
               for (const auto& [path, action] : actionsMap)
               {
                 unused(action);
                 const auto pathStr = path.generic_string();
                 auto* pyStr = toPyString(pathStr);
                 if (pyStr == nullptr)
                 {
                   Py_DECREF(list);
                   return nullptr;
                 }
                 PyList_SET_ITEM(list, index, pyStr);
                 ++index;
               }

               return list;
             },
             METH_NOARGS,
             nullptr},
            {nullptr, nullptr, 0, nullptr},
          };

          static PyModuleDef moduleDef = {
            PyModuleDef_HEAD_INIT,
            "tb",
            nullptr,
            -1,
            methods,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
          };

          auto* module = PyModule_Create(&moduleDef);
          if (module == nullptr)
          {
            return nullptr;
          }

          if (!registerTypes(module))
          {
            Py_DECREF(module);
            return nullptr;
          }

          return module;
        }) != 0)
    {
      return false;
    }
    g_pythonRegistered = true;
  }

  if (!Py_IsInitialized())
  {
    Py_Initialize();
    if (!Py_IsInitialized())
    {
      return false;
    }
  }

  {
    auto* module = PyImport_ImportModule("tb");
    if (module == nullptr)
    {
      PyErr_Clear();
      return false;
    }
    Py_DECREF(module);
  }

  return true;
}

bool prependSysPath(const std::filesystem::path& dir)
{
  auto* sysPath = PySys_GetObject("path");
  if (sysPath == nullptr || !PyList_Check(sysPath))
  {
    return false;
  }

  const auto dirStr = dir.u8string();
  auto* pyDir = PyUnicode_FromStringAndSize(
    reinterpret_cast<const char*>(dirStr.c_str()), static_cast<Py_ssize_t>(dirStr.size()));
  if (pyDir == nullptr)
  {
    return false;
  }

  const auto result = PyList_Insert(sysPath, 0, pyDir);
  Py_DECREF(pyDir);
  return result == 0;
}

} // namespace

PythonScripting::PythonScripting() = default;

PythonScripting& PythonScripting::instance()
{
  static auto instance = PythonScripting{};
  return instance;
}

bool PythonScripting::runScript(MapFrame& frame, const std::filesystem::path& path)
{
  if (!ensureInitialized())
  {
    frame.pythonLogger().error() << "Python initialization failed";
    return false;
  }

  auto gil = PyGILState_Ensure();

  g_currentFrame = &frame;

  const auto dir = path.parent_path();
  if (!dir.empty())
  {
    prependSysPath(dir);
  }

  PyObject* oldStdout = nullptr;
  PyObject* oldStderr = nullptr;
  {
    oldStdout = PySys_GetObject("stdout");
    oldStderr = PySys_GetObject("stderr");

    if (oldStdout)
    {
      Py_INCREF(oldStdout);
    }
    if (oldStderr)
    {
      Py_INCREF(oldStderr);
    }

    auto* newStdout = createLogWriterObject(0);
    auto* newStderr = createLogWriterObject(1);
    if (newStdout && newStderr)
    {
      PySys_SetObject("stdout", newStdout);
      PySys_SetObject("stderr", newStderr);
    }
    Py_XDECREF(newStdout);
    Py_XDECREF(newStderr);
  }

  const auto filename = path.generic_string();
  const auto pathStr = path.u8string();
  FILE* fp = nullptr;
#if defined(_WIN32)
  fp = _wfopen(path.c_str(), L"rb");
#else
  fp = fopen(reinterpret_cast<const char*>(pathStr.c_str()), "rb");
#endif

  if (fp == nullptr)
  {
    frame.pythonLogger().error() << "Could not open script: " << filename;
    if (oldStdout)
    {
      PySys_SetObject("stdout", oldStdout);
      Py_DECREF(oldStdout);
    }
    if (oldStderr)
    {
      PySys_SetObject("stderr", oldStderr);
      Py_DECREF(oldStderr);
    }
    g_currentFrame = nullptr;
    PyGILState_Release(gil);
    return false;
  }

  const auto rc = PyRun_SimpleFileEx(fp, filename.c_str(), 1);
  if (rc != 0)
  {
    PyErr_Print();
    frame.pythonLogger().error() << "Python script failed: " << filename;
    if (oldStdout)
    {
      PySys_SetObject("stdout", oldStdout);
      Py_DECREF(oldStdout);
    }
    if (oldStderr)
    {
      PySys_SetObject("stderr", oldStderr);
      Py_DECREF(oldStderr);
    }
    g_currentFrame = nullptr;
    PyGILState_Release(gil);
    return false;
  }

  if (oldStdout)
  {
    PySys_SetObject("stdout", oldStdout);
    Py_DECREF(oldStdout);
  }
  if (oldStderr)
  {
    PySys_SetObject("stderr", oldStderr);
    Py_DECREF(oldStderr);
  }

  g_currentFrame = nullptr;
  PyGILState_Release(gil);
  return true;
}

} // namespace tb::ui
