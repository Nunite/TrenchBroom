#pragma once

#include <Python.h>

namespace tb::ui {

bool initDocumentType(PyObject* module);
PyObject* module_document(PyObject* self, PyObject* args);
PyObject* module_current_document(PyObject* self, PyObject* args);

} // namespace tb::ui
