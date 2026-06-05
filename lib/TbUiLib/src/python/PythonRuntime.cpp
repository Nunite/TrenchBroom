#include "ui/python/PythonRuntime.h"

#include "Logger.h"
#include "ui/MapWindow.h"
#include "ui/python/PythonPluginSession.h"
#include "ui/python/PythonV2Module.h"

#if defined(slots)
#undef slots
#endif

#include <Python.h>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>

namespace tb::ui
{
namespace
{
thread_local PythonExecutionContext* g_currentExecutionContext = nullptr;
thread_local PythonPluginSession* g_currentPluginSession = nullptr;

struct PyRuntimeLogWriter
{
  PyObject_HEAD int isError = 0;
};

PyTypeObject* g_logWriterType = nullptr;

class ScopedExecutionContext
{
private:
  std::optional<PythonExecutionContext> m_ownedContext;
  PythonExecutionContext* m_contextPtr = nullptr;
  PythonExecutionContext* m_previous = nullptr;
  PythonPluginSession* m_previousSession = nullptr;

public:
  ScopedExecutionContext(
    const PythonExecutionContext& context, PythonPluginSession* session = nullptr)
    : m_ownedContext{session == nullptr ? std::make_optional(context) : std::nullopt}
    , m_contextPtr{session != nullptr ? &session->context() : &*m_ownedContext}
    , m_previous{g_currentExecutionContext}
    , m_previousSession{g_currentPluginSession}
  {
    g_currentExecutionContext = m_contextPtr;
    g_currentPluginSession = session;
  }

  ~ScopedExecutionContext()
  {
    g_currentExecutionContext = m_previous;
    g_currentPluginSession = m_previousSession;
  }
};

class ScopedSysPath
{
private:
  bool m_inserted = false;

public:
  explicit ScopedSysPath(const std::filesystem::path& path)
  {
    auto* sysPath = PySys_GetObject("path");
    if (sysPath == nullptr || !PyList_Check(sysPath))
    {
      return;
    }

    const auto pathStr = path.u8string();
    auto* pyPath = PyUnicode_FromStringAndSize(
      reinterpret_cast<const char*>(pathStr.c_str()),
      static_cast<Py_ssize_t>(pathStr.size()));
    if (pyPath == nullptr)
    {
      return;
    }

    m_inserted = PyList_Insert(sysPath, 0, pyPath) == 0;
    Py_DECREF(pyPath);
  }

  ~ScopedSysPath()
  {
    if (!m_inserted)
    {
      return;
    }

    auto* sysPath = PySys_GetObject("path");
    if (sysPath != nullptr && PyList_Check(sysPath) && PyList_Size(sysPath) > 0)
    {
      PySequence_DelItem(sysPath, 0);
    }
  }
};

class ScopedStdStreamRedirect
{
private:
  PyObject* m_oldStdout = nullptr;
  PyObject* m_oldStderr = nullptr;

public:
  ScopedStdStreamRedirect()
  {
    m_oldStdout = PySys_GetObject("stdout");
    m_oldStderr = PySys_GetObject("stderr");

    Py_XINCREF(m_oldStdout);
    Py_XINCREF(m_oldStderr);

    auto* newStdout = createLogWriterObject(0);
    auto* newStderr = createLogWriterObject(1);
    if (newStdout != nullptr && newStderr != nullptr)
    {
      PySys_SetObject("stdout", newStdout);
      PySys_SetObject("stderr", newStderr);
    }
    Py_XDECREF(newStdout);
    Py_XDECREF(newStderr);
  }

  ~ScopedStdStreamRedirect()
  {
    if (m_oldStdout != nullptr)
    {
      PySys_SetObject("stdout", m_oldStdout);
      Py_DECREF(m_oldStdout);
    }
    if (m_oldStderr != nullptr)
    {
      PySys_SetObject("stderr", m_oldStderr);
      Py_DECREF(m_oldStderr);
    }
  }

private:
  static PyObject* createLogWriterObject(const int isError)
  {
    if (g_logWriterType == nullptr)
    {
      return nullptr;
    }

    auto* object = reinterpret_cast<PyRuntimeLogWriter*>(
      g_logWriterType->tp_alloc(g_logWriterType, 0));
    if (object != nullptr)
    {
      object->isError = isError;
    }
    return reinterpret_cast<PyObject*>(object);
  }
};

PyObject* logWriterWrite(PyObject* self, PyObject* args)
{
  PyObject* value = nullptr;
  if (!PyArg_ParseTuple(args, "O", &value))
  {
    return nullptr;
  }

  auto* str = PyObject_Str(value);
  if (str == nullptr)
  {
    return nullptr;
  }

  Py_ssize_t size = 0;
  const auto* utf8 = PyUnicode_AsUTF8AndSize(str, &size);
  if (utf8 == nullptr)
  {
    Py_DECREF(str);
    return nullptr;
  }

  auto message = std::string_view{utf8, static_cast<size_t>(size)};
  while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
  {
    message.remove_suffix(1);
  }

  if (!message.empty())
  {
    auto* writer = reinterpret_cast<PyRuntimeLogWriter*>(self);
    auto* context = currentPythonExecutionContext();
    if (context != nullptr && context->logger != nullptr)
    {
      if (writer->isError != 0)
      {
        context->logger->error() << message;
      }
      else
      {
        context->logger->info() << message;
      }
    }
  }

  Py_DECREF(str);
  return PyLong_FromSsize_t(size);
}

PyObject* logWriterFlush(PyObject*, PyObject*)
{
  Py_RETURN_NONE;
}

PyObject* logWriterIsatty(PyObject*, PyObject*)
{
  Py_RETURN_FALSE;
}

bool ensureLogWriterType()
{
  if (g_logWriterType != nullptr)
  {
    return true;
  }

  static PyMethodDef methods[] = {
    {"write", logWriterWrite, METH_VARARGS, nullptr},
    {"flush", logWriterFlush, METH_NOARGS, nullptr},
    {"isatty", logWriterIsatty, METH_NOARGS, nullptr},
    {nullptr, nullptr, 0, nullptr}};

  static auto type = PyTypeObject{};
  type.tp_name = "tb2._LogWriter";
  type.tp_basicsize = sizeof(PyRuntimeLogWriter);
  type.tp_flags = Py_TPFLAGS_DEFAULT;
  type.tp_methods = methods;
  type.tp_new = PyType_GenericNew;

  if (PyType_Ready(&type) != 0)
  {
    return false;
  }

  g_logWriterType = &type;
  return true;
}

std::string readFile(const std::filesystem::path& path)
{
  auto stream = std::ifstream{path, std::ios::binary};
  auto buffer = std::ostringstream{};
  buffer << stream.rdbuf();
  return buffer.str();
}
} // namespace

PythonRuntime::PythonRuntime() = default;

PythonRuntime& PythonRuntime::instance()
{
  static auto instance = PythonRuntime{};
  return instance;
}

bool PythonRuntime::ensureInitialized()
{
  if (!installV2Module())
  {
    return false;
  }

  if (!Py_IsInitialized())
  {
    Py_Initialize();
  }
  if (!Py_IsInitialized())
  {
    return false;
  }

  auto gil = PyGILState_Ensure();
  if (!ensureLogWriterType())
  {
    PyGILState_Release(gil);
    return false;
  }

  auto* tb2Module = PyImport_ImportModule("tb2");
  if (tb2Module == nullptr)
  {
    PyGILState_Release(gil);
    return false;
  }

  auto* sysModules = PyImport_GetModuleDict();
  auto* tbModule = PyDict_GetItemString(sysModules, "tb");
  if (tbModule != nullptr)
  {
    PyObject_SetAttrString(tbModule, "v2", tb2Module);
    PyDict_SetItemString(sysModules, "tb.v2", tb2Module);
  }

  Py_DECREF(tb2Module);
  PyGILState_Release(gil);
  return true;
}

bool PythonRuntime::runScript(
  const PythonExecutionContext& context, const std::filesystem::path& path)
{
  return runScript(context, path, nullptr);
}

bool PythonRuntime::runScript(PythonPluginSession& session)
{
  return runScript(session.context(), session.context().scriptPath, &session);
}

bool PythonRuntime::runScript(
  const PythonExecutionContext& context,
  const std::filesystem::path& path,
  PythonPluginSession* session)
{
  if (!ensureInitialized())
  {
    if (context.logger)
    {
      m_lastError = "Python v2 initialization failed";
      context.logger->error() << m_lastError;
    }
    return false;
  }

  m_lastError.clear();

  auto gil = PyGILState_Ensure();
  auto scopedContext = ScopedExecutionContext{context, session};
  auto scopedSysPath = ScopedSysPath{path.parent_path()};
  auto scopedStdStreamRedirect = ScopedStdStreamRedirect{};

  auto source = readFile(path);
  if (source.empty() && std::filesystem::file_size(path) > 0)
  {
    m_lastError = "Could not read Python script: " + path.generic_string();
    if (context.logger)
    {
      context.logger->error() << m_lastError;
    }
    PyGILState_Release(gil);
    return false;
  }

  auto* globals = PyDict_New();
  if (globals == nullptr)
  {
    m_lastError = "Could not create Python globals";
    if (context.logger)
    {
      context.logger->error() << m_lastError;
    }
    PyGILState_Release(gil);
    return false;
  }

  auto* builtins = PyEval_GetBuiltins();
  PyDict_SetItemString(globals, "__builtins__", builtins);
  const auto filename = path.u8string();
  auto* filenameObj = PyUnicode_FromStringAndSize(
    reinterpret_cast<const char*>(filename.c_str()),
    static_cast<Py_ssize_t>(filename.size()));
  if (filenameObj != nullptr)
  {
    PyDict_SetItemString(globals, "__file__", filenameObj);
    Py_DECREF(filenameObj);
  }

  auto* result =
    PyRun_StringFlags(source.c_str(), Py_file_input, globals, globals, nullptr);
  Py_DECREF(globals);

  if (result == nullptr)
  {
    if (context.logger)
    {
      m_lastError = formatCurrentException();
      context.logger->error() << m_lastError;
    }
    PyGILState_Release(gil);
    return false;
  }

  Py_DECREF(result);
  PyGILState_Release(gil);
  return true;
}

void PythonRuntime::emitEvent(const std::string& eventName, MapWindow& mapWindow)
{
  if (!ensureInitialized())
  {
    return;
  }

  auto context = PythonExecutionContext{};
  context.mapWindow = &mapWindow;
  context.document = &mapWindow.document();
  context.appController = &mapWindow.appController();
  context.currentMapView = mapWindow.currentMapViewBase();
  context.logger = &mapWindow.pythonLogger();

  auto gil = PyGILState_Ensure();
  auto scopedContext = ScopedExecutionContext{context};
  auto* module = PyImport_ImportModule("tb2");
  if (module != nullptr)
  {
    auto* result = PyObject_CallMethod(module, "_emit_event", "s", eventName.c_str());
    if (result == nullptr && context.logger)
    {
      context.logger->error() << formatCurrentException();
    }
    Py_XDECREF(result);
    Py_DECREF(module);
  }
  PyGILState_Release(gil);
}

void PythonRuntime::cleanupPlugin(const std::string& pluginId)
{
  if (!ensureInitialized())
  {
    return;
  }
  auto gil = PyGILState_Ensure();
  auto* module = PyImport_ImportModule("tb2");
  if (module != nullptr)
  {
    auto* result = PyObject_CallMethod(module, "_cleanup_plugin", "s", pluginId.c_str());
    Py_XDECREF(result);
    Py_DECREF(module);
  }
  PyGILState_Release(gil);
}

void PythonRuntime::cleanupPluginSession(PythonPluginSession& session)
{
  if (!ensureInitialized())
  {
    return;
  }

  auto gil = PyGILState_Ensure();
  auto* module = PyImport_ImportModule("tb2");
  if (module != nullptr)
  {
    auto* capsule = PyCapsule_New(&session, nullptr, nullptr);
    if (capsule != nullptr)
    {
      auto* methodName = PyUnicode_FromString("_cleanup_plugin_session");
      auto* result = methodName != nullptr
                       ? PyObject_CallMethodObjArgs(module, methodName, capsule, nullptr)
                       : nullptr;
      Py_XDECREF(result);
      Py_XDECREF(methodName);
      Py_DECREF(capsule);
    }
    Py_DECREF(module);
  }
  PyGILState_Release(gil);
}

void PythonRuntime::cleanupDocument(MapWindow& mapWindow)
{
  emitEvent("document_closed", mapWindow);

  if (!ensureInitialized())
  {
    return;
  }

  auto gil = PyGILState_Ensure();
  auto* module = PyImport_ImportModule("tb2");
  if (module != nullptr)
  {
    auto* capsule = PyCapsule_New(&mapWindow.document(), nullptr, nullptr);
    if (capsule != nullptr)
    {
      auto* methodName = PyUnicode_FromString("_invalidate_document");
      auto* result = methodName != nullptr
                       ? PyObject_CallMethodObjArgs(module, methodName, capsule, nullptr)
                       : nullptr;
      Py_XDECREF(result);
      Py_XDECREF(methodName);
      Py_DECREF(capsule);
    }
    Py_DECREF(module);
  }
  PyGILState_Release(gil);
}

std::string PythonRuntime::formatCurrentException() const
{
  if (!PyErr_Occurred())
  {
    return "Unknown Python error";
  }

  PyObject* type = nullptr;
  PyObject* value = nullptr;
  PyObject* traceback = nullptr;
  PyErr_Fetch(&type, &value, &traceback);
  PyErr_NormalizeException(&type, &value, &traceback);

  auto result = std::string{};
  auto* tracebackModule = PyImport_ImportModule("traceback");
  if (tracebackModule != nullptr)
  {
    auto* formatException = PyObject_GetAttrString(tracebackModule, "format_exception");
    if (formatException != nullptr)
    {
      auto* formatted = PyObject_CallFunctionObjArgs(
        formatException,
        type != nullptr ? type : Py_None,
        value != nullptr ? value : Py_None,
        traceback != nullptr ? traceback : Py_None,
        nullptr);
      if (formatted != nullptr)
      {
        auto* separator = PyUnicode_FromString("");
        auto* joined =
          separator != nullptr ? PyUnicode_Join(separator, formatted) : nullptr;
        Py_XDECREF(separator);
        if (joined != nullptr)
        {
          const auto* utf8 = PyUnicode_AsUTF8(joined);
          if (utf8 != nullptr)
          {
            result = utf8;
          }
          Py_DECREF(joined);
        }
        Py_DECREF(formatted);
      }
      Py_DECREF(formatException);
    }
    Py_DECREF(tracebackModule);
  }

  if (result.empty() && value != nullptr)
  {
    auto* str = PyObject_Str(value);
    if (str != nullptr)
    {
      const auto* utf8 = PyUnicode_AsUTF8(str);
      if (utf8 != nullptr)
      {
        result = utf8;
      }
      Py_DECREF(str);
    }
  }
  if (result.empty())
  {
    result = "Python error";
  }

  Py_XDECREF(type);
  Py_XDECREF(value);
  Py_XDECREF(traceback);
  return result;
}

const std::string& PythonRuntime::lastError() const
{
  return m_lastError;
}

bool PythonRuntime::installV2Module()
{
  return installPythonV2Module();
}

PythonExecutionContext* currentPythonExecutionContext()
{
  return g_currentExecutionContext;
}

PythonPluginSession* currentPythonPluginSession()
{
  return g_currentPluginSession;
}

} // namespace tb::ui
