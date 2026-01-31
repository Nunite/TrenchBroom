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

#include "PythonDocument.h"
#include "PythonEntity.h"
#include "PythonMaterial.h"
#include "PythonPluginPanel.h"
#include "PythonSelection.h"
#include "PythonTransaction.h"

#if defined(slots)
#undef slots
#endif
#include <QAbstractTextDocumentLayout>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextTable>
#include <QTextTableCell>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "Exceptions.h"
#include "Logger.h"
#include "PythonPlane.h"
#include "PythonTypes.h"
#include "PythonUtils.h"
#include "PythonVec3.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushFaceAttributes.h"
#include "mdl/BrushFaceHandle.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Brushes.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Geometry.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Material.h"
#include "mdl/MaterialCollection.h"
#include "mdl/MaterialManager.h"
#include "mdl/PatchNode.h"
#include "mdl/Texture.h"
#include "mdl/Transaction.h"
#include "mdl/VertexHandleManager.h"
#include "mdl/WorldNode.h"
#include "ui/Actions.h"
#include "ui/Inspector.h"
#include "ui/MapDocument.h"
#include "ui/MapFrame.h"
#include "ui/PluginInspector.h"

#include "kdl/overload.h"
#include "kdl/vector_utils.h"

#include "vm/plane.h"
#include "vm/segment.h"

#include <Python.h>
#include <algorithm>
#include <array>
#include <string>
#include <QTimer>
#include <map>

namespace tb::ui
{
namespace
{
bool g_pythonRegistered = false;

// --- Timer Support ---
class PythonTimer : public QObject
{
public:
  PythonTimer(int id, int interval, PyObject* callback) : m_id(id), m_callback(callback)
  {
    Py_INCREF(m_callback);
    m_timer = new QTimer(this);
    m_timer->setInterval(interval);
    connect(m_timer, &QTimer::timeout, this, &PythonTimer::onTimeout);
    m_timer->start();
  }

  ~PythonTimer()
  {
    m_timer->stop();
    auto gil = PyGILState_Ensure();
    Py_DECREF(m_callback);
    PyGILState_Release(gil);
  }

private:
  void onTimeout()
  {
    auto gil = PyGILState_Ensure();
    auto* prev = g_currentFrame;
    // Note: g_currentFrame might be null or stale if the map frame is closed.
    // However, timers run in the main thread event loop, and we should check if
    // we have a valid context or if we should just run.
    // For safety, we can check if the main window still exists, but simple callback execution is usually fine.
    
    PyObject* res = PyObject_CallObject(m_callback, nullptr);
    if (res == nullptr)
    {
      PyErr_Print();
      if (g_currentFrame)
      {
         g_currentFrame->pythonLogger().error("Error in timer callback");
      }
    }
    else
    {
      Py_DECREF(res);
    }
    
    g_currentFrame = prev;
    PyGILState_Release(gil);
  }

  int m_id;
  PyObject* m_callback;
  QTimer* m_timer;
};

static std::map<int, PythonTimer*> g_timers;
static int g_nextTimerId = 1;

static PyObject* script_set_interval(PyObject* self, PyObject* args)
{
  unused(self);
  PyObject* callback = nullptr;
  int interval = 0;
  if (!PyArg_ParseTuple(args, "Oi", &callback, &interval))
  {
    return nullptr;
  }
  
  if (!PyCallable_Check(callback))
  {
    PyErr_SetString(PyExc_TypeError, "callback must be callable");
    return nullptr;
  }
  
  // We need a QObject parent for the timer to ensure it lives in the correct thread (main thread)
  // PythonScripting is a singleton, but not a QObject.
  // We can parent the timer to nothing (and manage memory manually), 
  // or use a static QObject if needed. Here we manage manually in g_timers map.
  
  int id = g_nextTimerId++;
  auto* timer = new PythonTimer(id, interval, callback);
  g_timers[id] = timer;
  
  return PyLong_FromLong(id);
}

static PyObject* script_clear_interval(PyObject* self, PyObject* args)
{
  unused(self);
  int id = 0;
  if (!PyArg_ParseTuple(args, "i", &id))
  {
    return nullptr;
  }
  
  auto it = g_timers.find(id);
  if (it != g_timers.end())
  {
    delete it->second;
    g_timers.erase(it);
  }
  
  Py_RETURN_NONE;
}

// --- End Timer Support ---

class StdStreamRedirect
{
private:
  PyObject* m_oldStdout = nullptr;
  PyObject* m_oldStderr = nullptr;

public:
  StdStreamRedirect()
  {
    m_oldStdout = PySys_GetObject("stdout");
    m_oldStderr = PySys_GetObject("stderr");

    if (m_oldStdout)
    {
      Py_INCREF(m_oldStdout);
    }
    if (m_oldStderr)
    {
      Py_INCREF(m_oldStderr);
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

  ~StdStreamRedirect()
  {
    if (m_oldStdout)
    {
      PySys_SetObject("stdout", m_oldStdout);
      Py_DECREF(m_oldStdout);
    }
    if (m_oldStderr)
    {
      PySys_SetObject("stderr", m_oldStderr);
      Py_DECREF(m_oldStderr);
    }
  }
};


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


PyObject* module_transaction(PyObject* self, PyObject* args) {
  unused(self);
  const char* name = "Python Script";
  if (!PyArg_ParseTuple(args, "|s", &name)) {
    return nullptr;
  }
  auto* doc = activeDocument();
  if (!doc)
  {
    PyErr_SetString(PyExc_RuntimeError, "No active document");
    return nullptr;
  }
  PyObject* pyName = PyUnicode_FromString(name);
  if (!pyName) {
      return nullptr;
  }
  PyObject* result = createTransactionObject(doc, pyName);
  Py_DECREF(pyName);
  return result;
}


PyObject* module_register_callback(PyObject* self, PyObject* args)
{
  const char* event = nullptr;
  PyObject* callback = nullptr;
  if (!PyArg_ParseTuple(args, "sO", &event, &callback))
  {
    return nullptr;
  }

  if (!PyCallable_Check(callback))
  {
    PyErr_SetString(PyExc_TypeError, "Callback must be callable");
    return nullptr;
  }

  PyObject* dict = PyModule_GetDict(self);
  if (dict == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "Could not get module dict");
    return nullptr;
  }

  PyObject* callbacks = PyDict_GetItemString(dict, "_callbacks");
  if (callbacks == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "_callbacks not found in module");
    return nullptr;
  }

  PyObject* list = PyDict_GetItemString(callbacks, event);
  if (list == nullptr)
  {
    PyErr_Format(PyExc_ValueError, "Unknown event type: %s", event);
    return nullptr;
  }

  int contains = PySequence_Contains(list, callback);
  if (contains == -1)
    return nullptr;
  if (contains == 1)
  {
    Py_RETURN_NONE;
  }

  if (PyList_Append(list, callback) != 0)
  {
    return nullptr;
  }

  Py_RETURN_NONE;
}

PyObject* module_unregister_callback(PyObject* self, PyObject* args)
{
  const char* event = nullptr;
  PyObject* callback = nullptr;
  if (!PyArg_ParseTuple(args, "sO", &event, &callback))
  {
    return nullptr;
  }

  PyObject* dict = PyModule_GetDict(self);
  if (dict == nullptr)
    return nullptr;

  PyObject* callbacks = PyDict_GetItemString(dict, "_callbacks");
  if (callbacks == nullptr)
    return nullptr;

  PyObject* list = PyDict_GetItemString(callbacks, event);
  if (list == nullptr)
    return nullptr;

  int contains = PySequence_Contains(list, callback);
  if (contains == -1)
    return nullptr;
  if (contains == 1)
  {
    PyObject* res = PyObject_CallMethod(list, "remove", "O", callback);
    if (res == nullptr)
      return nullptr;
    Py_DECREF(res);
  }

  Py_RETURN_NONE;
}


bool registerTypes(PyObject* module)
{
  static auto logWriterType = PyTypeObject{};
  if (!initVec3Type(module))
    return false;

  if (!initPlaneType(module))
    return false;

  if (!initDocumentType(module))
    return false;


  if (!initSelectionType(module))
    return false;

  if (!initEntityType(module))
    return false;
  if (!initBrushType(module))
    return false;
  if (!initFaceType(module))
    return false;

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
      PyModule_AddObject(
        module, "_LogWriter", reinterpret_cast<PyObject*>(g_logWriterType))
      != 0)
    {
      Py_DECREF(g_logWriterType);
      return false;
    }
  }

  if (!initTransactionType(module))
    return false;

  if (!initMaterialType(module))
    return false;

  if (!initMaterialCollectionType(module))
    return false;

  if (!initPluginPanelType(module))
    return false;

  return true;
}

bool ensureInitialized()
{
  if (!g_pythonRegistered)
  {
    if (
      PyImport_AppendInittab(
        "tb",
        []() -> PyObject* {
          static PyMethodDef methods[] = {
            {"register_callback", module_register_callback, METH_VARARGS, nullptr},
            {"unregister_callback", module_unregister_callback, METH_VARARGS, nullptr},
            {"document", module_document, METH_NOARGS, nullptr},
            {"current_document", module_current_document, METH_NOARGS, nullptr},
            {"transaction", module_transaction, METH_VARARGS, nullptr},
            {"create_brush", module_create_brush, METH_VARARGS, nullptr},
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
                 auto context = ActionExecutionContext{
                   g_currentFrame, g_currentFrame->currentMapViewBase()};
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
            {"set_interval", script_set_interval, METH_VARARGS, nullptr},
            {"clear_interval", script_clear_interval, METH_VARARGS, nullptr},
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

          // Initialize callbacks registry
          auto* callbacks = PyDict_New();
          if (callbacks == nullptr)
          {
            Py_DECREF(module);
            return nullptr;
          }

          // Pre-create lists for known events
  auto* list = PyList_New(0);
  PyDict_SetItemString(callbacks, "selection_changed", list);
  Py_DECREF(list);

  list = PyList_New(0);
  PyDict_SetItemString(callbacks, "document_saved", list);
  Py_DECREF(list);

  if (PyModule_AddObject(module, "_callbacks", callbacks) != 0)
          {
            Py_DECREF(callbacks);
            Py_DECREF(module);
            return nullptr;
          }

          if (!registerTypes(module))
          {
            Py_DECREF(module);
            return nullptr;
          }

          return module;
        })
      != 0)
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
    reinterpret_cast<const char*>(dirStr.c_str()),
    static_cast<Py_ssize_t>(dirStr.size()));
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

  StdStreamRedirect streamRedirect;

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
    g_currentFrame = nullptr;
    PyGILState_Release(gil);
    return false;
  }

  const auto rc = PyRun_SimpleFileEx(fp, filename.c_str(), 1);
  if (rc != 0)
  {
    PyErr_Print();
    frame.pythonLogger().error() << "Python script failed: " << filename;
    g_currentFrame = nullptr;
    PyGILState_Release(gil);
    return false;
  }

  g_currentFrame = nullptr;
  PyGILState_Release(gil);
  return true;
}

void PythonScripting::onSelectionChanged(MapFrame& frame)
{
  if (!g_pythonRegistered || !Py_IsInitialized())
  {
    return;
  }

  auto gil = PyGILState_Ensure();

  auto* prev = g_currentFrame;
  g_currentFrame = &frame;

  StdStreamRedirect streamRedirect;

  PyObject* tbModule = PyImport_ImportModule("tb");
  if (tbModule)
  {
    PyObject* callbacks = PyObject_GetAttrString(tbModule, "_callbacks");
    if (callbacks && PyDict_Check(callbacks))
    {
      PyObject* list = PyDict_GetItemString(callbacks, "selection_changed");
      if (list && PyList_Check(list))
      {
        Py_ssize_t size = PyList_Size(list);
        for (Py_ssize_t i = 0; i < size; ++i)
        {
          PyObject* func = PyList_GetItem(list, i);
          if (PyCallable_Check(func))
          {
            PyObject* res = PyObject_CallObject(func, nullptr);
            if (res == nullptr)
            {
              PyErr_Print();
              frame.pythonLogger().error("Error in selection_changed callback");
            }
            else
            {
              Py_DECREF(res);
            }
          }
        }
      }
    }
    Py_XDECREF(callbacks);
    Py_DECREF(tbModule);
  }
  else
  {
    PyErr_Clear();
  }

  g_currentFrame = prev;
  PyGILState_Release(gil);
}

void PythonScripting::onDocumentSaved(MapFrame& frame)
{
  if (!g_pythonRegistered || !Py_IsInitialized())
  {
    return;
  }

  auto gil = PyGILState_Ensure();

  auto* prev = g_currentFrame;
  g_currentFrame = &frame;

  StdStreamRedirect streamRedirect;

  PyObject* tbModule = PyImport_ImportModule("tb");
  if (tbModule)
  {
    PyObject* callbacks = PyObject_GetAttrString(tbModule, "_callbacks");
    if (callbacks && PyDict_Check(callbacks))
    {
      PyObject* list = PyDict_GetItemString(callbacks, "document_saved");
      if (list && PyList_Check(list))
      {
        Py_ssize_t size = PyList_Size(list);
        for (Py_ssize_t i = 0; i < size; ++i)
        {
          PyObject* func = PyList_GetItem(list, i);
          if (PyCallable_Check(func))
          {
            PyObject* res = PyObject_CallObject(func, nullptr);
            if (res == nullptr)
            {
              PyErr_Print();
              frame.pythonLogger().error("Error in document_saved callback");
            }
            else
            {
              Py_DECREF(res);
            }
          }
        }
      }
    }
    Py_XDECREF(callbacks);
    Py_DECREF(tbModule);
  }
  else
  {
    PyErr_Clear();
  }

  g_currentFrame = prev;
  PyGILState_Release(gil);
}

} // namespace tb::ui
