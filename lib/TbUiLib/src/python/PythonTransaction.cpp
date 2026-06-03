#include "PythonTransaction.h"
#include "PythonTypes.h"
#include "PythonUtils.h"
#include "mdl/Transaction.h"
#include "mdl/Map.h"
#include "ui/MapDocument.h"
#include "Exceptions.h"

namespace tb::ui {

namespace {

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

} // namespace

bool initTransactionType(PyObject* module)
{
  if (g_transactionType == nullptr)
  {
    static auto transactionType = PyTypeObject{};
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
    if (PyModule_AddObject(module, "Transaction", reinterpret_cast<PyObject*>(g_transactionType)) != 0)
    {
      Py_DECREF(g_transactionType);
      return false;
    }
  }
  return true;
}

} // namespace tb::ui
