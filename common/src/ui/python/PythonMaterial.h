#pragma once

#include <Python.h>

namespace tb::ui {

bool initMaterialType(PyObject* module);
bool initMaterialCollectionType(PyObject* module);

} // namespace tb::ui
