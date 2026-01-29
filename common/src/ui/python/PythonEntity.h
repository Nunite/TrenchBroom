#pragma once

#include <Python.h>

namespace tb::ui {

bool initEntityType(PyObject* module);
bool initBrushType(PyObject* module);
bool initFaceType(PyObject* module);

PyObject* module_create_brush(PyObject* self, PyObject* args);

} // namespace tb::ui
