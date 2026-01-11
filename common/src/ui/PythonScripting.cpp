/*
 Copyright (C) 2010 Kristian Duske

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
#include "mdl/BrushFaceHandle.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/EntityNode.h"
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
      {"entities", selection_entities, METH_NOARGS, nullptr},
      {"all_entities", selection_all_entities, METH_NOARGS, nullptr},
      {"brush_vertices", selection_brush_vertices, METH_NOARGS, nullptr},
      {"set_property", selection_set_property, METH_VARARGS, nullptr},
      {"duplicate", selection_duplicate, METH_NOARGS, nullptr},
      {"translate", selection_translate, METH_VARARGS, nullptr},
      {"rotate", selection_rotate, METH_VARARGS, nullptr},
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

    static PyMethodDef entityMethods[] = {
      {"classname", entity_classname, METH_NOARGS, nullptr},
      {"keys", entity_keys, METH_NOARGS, nullptr},
      {"get", entity_get, METH_VARARGS, nullptr},
      {nullptr, nullptr, 0, nullptr},
    };
    entityType.tp_methods = entityMethods;

    static PyGetSetDef entityGetSet[] = {
      {"classname", entity_get_classname, nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    entityType.tp_getset = entityGetSet;

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
