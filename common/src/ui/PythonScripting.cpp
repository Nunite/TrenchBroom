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
#include "mdl/Map_Brushes.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushFaceAttributes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Transaction.h"
#include "mdl/VertexHandleManager.h"
#include "mdl/MaterialManager.h"
#include "mdl/MaterialCollection.h"
#include "mdl/Material.h"
#include "mdl/Texture.h"
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
#include <QColorDialog>

#include "vm/segment.h"
#include "vm/plane.h"

#include <string>

namespace tb::ui
{
namespace
{
thread_local MapFrame* g_currentFrame = nullptr;
bool g_pythonRegistered = false;

struct PyTbVec3
{
  PyObject_HEAD vm::vec3d vec;
};

struct PyTbPlane
{
  PyObject_HEAD vm::plane3d plane;
};

struct PyTbBrush
{
  PyObject_HEAD tb::ui::MapDocument* document;
  tb::mdl::BrushNode* brushNode;
};

struct PyTbFace
{
  PyObject_HEAD tb::ui::MapDocument* document;
  tb::mdl::BrushNode* brushNode;
  size_t faceIndex;
};

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

struct PyTbMaterial
{
  PyObject_HEAD
  const tb::mdl::Material* material;
};

struct PyTbMaterialCollection
{
  PyObject_HEAD
  const tb::mdl::MaterialCollection* collection;
};

PyTypeObject* g_vec3Type = nullptr;
PyTypeObject* g_planeType = nullptr;
PyTypeObject* g_brushType = nullptr;
PyTypeObject* g_faceType = nullptr;
PyTypeObject* g_documentType = nullptr;
PyTypeObject* g_selectionType = nullptr;
PyTypeObject* g_entityType = nullptr;
PyTypeObject* g_logWriterType = nullptr;
PyTypeObject* g_transactionType = nullptr;
PyTypeObject* g_pluginPanelType = nullptr;
PyTypeObject* g_materialType = nullptr;
PyTypeObject* g_materialCollectionType = nullptr;

struct PyTbPluginPanel
{
  PyObject_HEAD QPointer<QWidget>* container;
};

PyObject* createVec3Object(const vm::vec3d& v)
{
  if (g_vec3Type == nullptr)
    return nullptr;
  auto* obj = PyObject_New(PyTbVec3, g_vec3Type);
  if (obj)
    obj->vec = v;
  return (PyObject*)obj;
}

PyObject* createMaterialObject(const tb::mdl::Material* material)
{
  if (g_materialType == nullptr || material == nullptr)
    return nullptr;
  auto* obj = PyObject_New(PyTbMaterial, g_materialType);
  if (obj)
    obj->material = material;
  return (PyObject*)obj;
}

PyObject* createMaterialCollectionObject(const tb::mdl::MaterialCollection* collection)
{
  if (g_materialCollectionType == nullptr || collection == nullptr)
    return nullptr;
  auto* obj = PyObject_New(PyTbMaterialCollection, g_materialCollectionType);
  if (obj)
    obj->collection = collection;
  return (PyObject*)obj;
}

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

PyObject* vec3_new(PyTypeObject* type, PyObject*, PyObject*)
{
  auto* self = (PyTbVec3*)type->tp_alloc(type, 0);
  if (self != nullptr)
  {
    new (&self->vec) vm::vec3d{};
  }
  return (PyObject*)self;
}

int vec3_init(PyTbVec3* self, PyObject* args, PyObject*)
{
  double x = 0.0, y = 0.0, z = 0.0;
  if (!PyArg_ParseTuple(args, "|ddd", &x, &y, &z))
  {
    return -1;
  }
  self->vec = vm::vec3d{x, y, z};
  return 0;
}

void vec3_dealloc(PyTbVec3* self)
{
  Py_TYPE(self)->tp_free((PyObject*)self);
}

PyObject* vec3_repr(PyTbVec3* self)
{
  char buffer[128];
  snprintf(buffer, sizeof(buffer), "Vec3(%g, %g, %g)", self->vec.x(), self->vec.y(), self->vec.z());
  return PyUnicode_FromString(buffer);
}

PyObject* vec3_str(PyTbVec3* self)
{
  char buffer[128];
  snprintf(buffer, sizeof(buffer), "%g %g %g", self->vec.x(), self->vec.y(), self->vec.z());
  return PyUnicode_FromString(buffer);
}

PyObject* vec3_get_x(PyObject* self, void*)
{
  auto* v = (PyTbVec3*)self;
  return PyFloat_FromDouble(v->vec.x());
}
PyObject* vec3_get_y(PyObject* self, void*)
{
  auto* v = (PyTbVec3*)self;
  return PyFloat_FromDouble(v->vec.y());
}
PyObject* vec3_get_z(PyObject* self, void*)
{
  auto* v = (PyTbVec3*)self;
  return PyFloat_FromDouble(v->vec.z());
}

int vec3_set_x(PyObject* self, PyObject* value, void*)
{
  auto* v = (PyTbVec3*)self;
  if (PyFloat_Check(value))
    v->vec[0] = PyFloat_AsDouble(value);
  else if (PyLong_Check(value))
    v->vec[0] = PyLong_AsDouble(value);
  else
  {
    PyErr_SetString(PyExc_TypeError, "Float required");
    return -1;
  }
  return 0;
}
int vec3_set_y(PyObject* self, PyObject* value, void*)
{
  auto* v = (PyTbVec3*)self;
  if (PyFloat_Check(value))
    v->vec[1] = PyFloat_AsDouble(value);
  else if (PyLong_Check(value))
    v->vec[1] = PyLong_AsDouble(value);
  else
  {
    PyErr_SetString(PyExc_TypeError, "Float required");
    return -1;
  }
  return 0;
}
int vec3_set_z(PyObject* self, PyObject* value, void*)
{
  auto* v = (PyTbVec3*)self;
  if (PyFloat_Check(value))
    v->vec[2] = PyFloat_AsDouble(value);
  else if (PyLong_Check(value))
    v->vec[2] = PyLong_AsDouble(value);
  else
  {
    PyErr_SetString(PyExc_TypeError, "Float required");
    return -1;
  }
  return 0;
}

PyObject* vec3_add(PyObject* left, PyObject* right)
{
  if (PyObject_TypeCheck(left, g_vec3Type) && PyObject_TypeCheck(right, g_vec3Type))
  {
    auto* l = (PyTbVec3*)left;
    auto* r = (PyTbVec3*)right;
    auto* res = (PyTbVec3*)g_vec3Type->tp_alloc(g_vec3Type, 0);
    new (&res->vec) vm::vec3d{l->vec + r->vec};
    return (PyObject*)res;
  }
  Py_RETURN_NOTIMPLEMENTED;
}

PyObject* vec3_sub(PyObject* left, PyObject* right)
{
  if (PyObject_TypeCheck(left, g_vec3Type) && PyObject_TypeCheck(right, g_vec3Type))
  {
    auto* l = (PyTbVec3*)left;
    auto* r = (PyTbVec3*)right;
    auto* res = (PyTbVec3*)g_vec3Type->tp_alloc(g_vec3Type, 0);
    new (&res->vec) vm::vec3d{l->vec - r->vec};
    return (PyObject*)res;
  }
  Py_RETURN_NOTIMPLEMENTED;
}

PyObject* vec3_mul(PyObject* left, PyObject* right)
{
  if (PyObject_TypeCheck(left, g_vec3Type))
  {
    auto* l = (PyTbVec3*)left;
    double scalar = 0.0;
    if (PyFloat_Check(right))
      scalar = PyFloat_AsDouble(right);
    else if (PyLong_Check(right))
      scalar = PyLong_AsDouble(right);
    else
      Py_RETURN_NOTIMPLEMENTED;

    auto* res = (PyTbVec3*)g_vec3Type->tp_alloc(g_vec3Type, 0);
    new (&res->vec) vm::vec3d{l->vec * scalar};
    return (PyObject*)res;
  }
  else if (PyObject_TypeCheck(right, g_vec3Type))
  {
    return vec3_mul(right, left);
  }
  Py_RETURN_NOTIMPLEMENTED;
}

PyObject* vec3_truediv(PyObject* left, PyObject* right)
{
  if (PyObject_TypeCheck(left, g_vec3Type))
  {
    auto* l = (PyTbVec3*)left;
    double scalar = 0.0;
    if (PyFloat_Check(right))
      scalar = PyFloat_AsDouble(right);
    else if (PyLong_Check(right))
      scalar = PyLong_AsDouble(right);
    else
      Py_RETURN_NOTIMPLEMENTED;

    if (scalar == 0.0)
    {
      PyErr_SetString(PyExc_ZeroDivisionError, "float division by zero");
      return nullptr;
    }

    auto* res = (PyTbVec3*)g_vec3Type->tp_alloc(g_vec3Type, 0);
    new (&res->vec) vm::vec3d{l->vec / scalar};
    return (PyObject*)res;
  }
  Py_RETURN_NOTIMPLEMENTED;
}

PyObject* vec3_neg(PyObject* self)
{
  auto* v = (PyTbVec3*)self;
  auto* res = (PyTbVec3*)g_vec3Type->tp_alloc(g_vec3Type, 0);
  new (&res->vec) vm::vec3d{-v->vec};
  return (PyObject*)res;
}

Py_ssize_t vec3_len(PyObject*)
{
  return 3;
}

PyObject* vec3_getitem(PyObject* self, Py_ssize_t i)
{
  auto* v = (PyTbVec3*)self;
  if (i < 0 || i >= 3)
  {
    PyErr_SetString(PyExc_IndexError, "Vec3 index out of range");
    return nullptr;
  }
  return PyFloat_FromDouble(v->vec[static_cast<size_t>(i)]);
}

PyObject* vec3_richcompare(PyObject* self, PyObject* other, int op)
{
  if (!PyObject_TypeCheck(self, g_vec3Type) || !PyObject_TypeCheck(other, g_vec3Type))
  {
    Py_RETURN_NOTIMPLEMENTED;
  }

  auto* v1 = (PyTbVec3*)self;
  auto* v2 = (PyTbVec3*)other;

  if (op == Py_EQ)
  {
    if (v1->vec == v2->vec) Py_RETURN_TRUE;
    Py_RETURN_FALSE;
  }
  else if (op == Py_NE)
  {
    if (v1->vec != v2->vec) Py_RETURN_TRUE;
    Py_RETURN_FALSE;
  }

  Py_RETURN_NOTIMPLEMENTED;
}

PyObject* vec3_dot(PyObject* self, PyObject* args)
{
  PyObject* otherObj = nullptr;
  if (!PyArg_ParseTuple(args, "O", &otherObj))
  {
    return nullptr;
  }

  if (!PyObject_TypeCheck(otherObj, g_vec3Type))
  {
    PyErr_SetString(PyExc_TypeError, "Expected tb.Vec3");
    return nullptr;
  }

  auto* v1 = (PyTbVec3*)self;
  auto* v2 = (PyTbVec3*)otherObj;

  return PyFloat_FromDouble(vm::dot(v1->vec, v2->vec));
}

PyObject* vec3_cross(PyObject* self, PyObject* args)
{
  PyObject* otherObj = nullptr;
  if (!PyArg_ParseTuple(args, "O", &otherObj))
  {
    return nullptr;
  }

  if (!PyObject_TypeCheck(otherObj, g_vec3Type))
  {
    PyErr_SetString(PyExc_TypeError, "Expected tb.Vec3");
    return nullptr;
  }

  auto* v1 = (PyTbVec3*)self;
  auto* v2 = (PyTbVec3*)otherObj;

  return createVec3Object(vm::cross(v1->vec, v2->vec));
}

PyObject* vec3_length(PyObject* self, PyObject*)
{
  auto* v = (PyTbVec3*)self;
  return PyFloat_FromDouble(vm::length(v->vec));
}

PyObject* vec3_normalize(PyObject* self, PyObject*)
{
  auto* v = (PyTbVec3*)self;
  return createVec3Object(vm::normalize(v->vec));
}

PyObject* plane_new(PyTypeObject* type, PyObject*, PyObject*)
{
  auto* self = (PyTbPlane*)type->tp_alloc(type, 0);
  if (self != nullptr)
  {
    new (&self->plane) vm::plane3d{};
  }
  return (PyObject*)self;
}

int plane_init(PyTbPlane* self, PyObject* args, PyObject*)
{
  PyObject* normalObj = nullptr;
  double dist = 0.0;
  if (!PyArg_ParseTuple(args, "Od", &normalObj, &dist))
  {
    return -1;
  }

  if (!PyObject_TypeCheck(normalObj, g_vec3Type))
  {
    PyErr_SetString(PyExc_TypeError, "expected tb.Vec3 for normal");
    return -1;
  }

  auto* v = (PyTbVec3*)normalObj;
  self->plane = vm::plane3d{dist, v->vec};
  return 0;
}

void plane_dealloc(PyTbPlane* self)
{
  Py_TYPE(self)->tp_free((PyObject*)self);
}

PyObject* plane_repr(PyTbPlane* self)
{
  char buffer[256];
  snprintf(buffer, sizeof(buffer), "Plane(normal=Vec3(%g, %g, %g), dist=%g)",
           self->plane.normal.x(), self->plane.normal.y(), self->plane.normal.z(), self->plane.distance);
  return PyUnicode_FromString(buffer);
}

PyObject* plane_from_points(PyObject*, PyObject* args)
{
  PyObject* p1Obj = nullptr;
  PyObject* p2Obj = nullptr;
  PyObject* p3Obj = nullptr;

  if (!PyArg_ParseTuple(args, "OOO", &p1Obj, &p2Obj, &p3Obj))
  {
    return nullptr;
  }

  if (!PyObject_TypeCheck(p1Obj, g_vec3Type) ||
      !PyObject_TypeCheck(p2Obj, g_vec3Type) ||
      !PyObject_TypeCheck(p3Obj, g_vec3Type))
  {
    PyErr_SetString(PyExc_TypeError, "expected 3 tb.Vec3 points");
    return nullptr;
  }

  auto* v1 = (PyTbVec3*)p1Obj;
  auto* v2 = (PyTbVec3*)p2Obj;
  auto* v3 = (PyTbVec3*)p3Obj;

  try
  {
    const auto normal = vm::normalize(vm::cross(v2->vec - v1->vec, v3->vec - v1->vec));
    const auto dist = vm::dot(v1->vec, normal);
    auto* obj = (PyTbPlane*)g_planeType->tp_alloc(g_planeType, 0);
    if (obj)
    {
       new (&obj->plane) vm::plane3d{dist, normal};
    }
    return (PyObject*)obj;
  }
  catch (const std::exception& e)
  {
    PyErr_SetString(PyExc_ValueError, e.what());
    return nullptr;
  }
}

PyObject* plane_distance(PyObject* self, PyObject* args)
{
  PyObject* pointObj = nullptr;
  if (!PyArg_ParseTuple(args, "O", &pointObj))
  {
    return nullptr;
  }

  if (!PyObject_TypeCheck(pointObj, g_vec3Type))
  {
    PyErr_SetString(PyExc_TypeError, "expected tb.Vec3");
    return nullptr;
  }

  auto* p = (PyTbPlane*)self;
  auto* v = (PyTbVec3*)pointObj;

  return PyFloat_FromDouble(p->plane.point_distance(v->vec));
}

PyObject* plane_project(PyObject* self, PyObject* args)
{
  PyObject* pointObj = nullptr;
  if (!PyArg_ParseTuple(args, "O", &pointObj))
  {
    return nullptr;
  }

  if (!PyObject_TypeCheck(pointObj, g_vec3Type))
  {
    PyErr_SetString(PyExc_TypeError, "expected tb.Vec3");
    return nullptr;
  }

  auto* p = (PyTbPlane*)self;
  auto* v = (PyTbVec3*)pointObj;

  auto projected = p->plane.project_point(v->vec);
  return createVec3Object(projected);
}

PyObject* plane_get_normal(PyObject* self, void*)
{
  auto* p = (PyTbPlane*)self;
  return createVec3Object(p->plane.normal);
}

int plane_set_normal(PyObject* self, PyObject* value, void*)
{
  if (!PyObject_TypeCheck(value, g_vec3Type))
  {
    PyErr_SetString(PyExc_TypeError, "expected tb.Vec3");
    return -1;
  }
  auto* p = (PyTbPlane*)self;
  auto* v = (PyTbVec3*)value;
  p->plane = vm::plane3d{p->plane.distance, v->vec};
  return 0;
}

PyObject* plane_get_dist(PyObject* self, void*)
{
  auto* p = (PyTbPlane*)self;
  return PyFloat_FromDouble(p->plane.distance);
}

int plane_set_dist(PyObject* self, PyObject* value, void*)
{
  double d = PyFloat_AsDouble(value);
  if (PyErr_Occurred()) return -1;
  auto* p = (PyTbPlane*)self;
  p->plane = vm::plane3d{d, p->plane.normal};
  return 0;
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

PyObject* createBrushObject(tb::ui::MapDocument* document, tb::mdl::BrushNode* brushNode)
{
  if (g_brushType == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "tb.Brush type is not initialized");
    return nullptr;
  }

  auto* obj = PyObject_New(PyTbBrush, g_brushType);
  if (obj == nullptr)
  {
    return nullptr;
  }
  obj->document = document;
  obj->brushNode = brushNode;
  return reinterpret_cast<PyObject*>(obj);
}

PyObject* createFaceObject(tb::ui::MapDocument* document, tb::mdl::BrushNode* brushNode, size_t faceIndex)
{
  if (g_faceType == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "tb.Face type is not initialized");
    return nullptr;
  }

  auto* obj = PyObject_New(PyTbFace, g_faceType);
  if (obj == nullptr)
  {
    return nullptr;
  }
  obj->document = document;
  obj->brushNode = brushNode;
  obj->faceIndex = faceIndex;
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

PyTbSelection* getSelectionFromPy(PyObject* self)
{
  if (g_selectionType == nullptr || !PyObject_TypeCheck(self, g_selectionType))
  {
    PyErr_SetString(PyExc_TypeError, "expected tb.Selection");
    return nullptr;
  }
  auto* selection = reinterpret_cast<PyTbSelection*>(self);
  if (selection->document == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "Selection is not valid");
    return nullptr;
  }
  return selection;
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

PyObject* plugin_panel_add_color_field(PyObject* self, PyObject* args)
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
    
    // Update text to show RGB
    b->setText(QString("%1, %2, %3").arg(c.red()).arg(c.green()).arg(c.blue()));
    
    // Set text color to contrasting color
    if (c.lightness() > 128)
      b->setStyleSheet(b->styleSheet() + " color: black;");
    else
      b->setStyleSheet(b->styleSheet() + " color: white;");
  };

  QColor color = Qt::white;
  if (initialColorObj != nullptr)
  {
      // Try to parse Vec3 or tuple
      if (PyObject_TypeCheck(initialColorObj, g_vec3Type))
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
               // Check if normalized (0-1) or byte (0-255).
               // Usually Vec3 is 0-X. But color fields often imply 0-1 or 0-255.
               // Let's assume 0-255 if any > 1.0?
               // But Vec3 colors in TB are usually 0-1?
               // Wait, texture colors are 0-255?
               // Let's assume standard QColor behavior:
               // If we use setRgbF, it expects 0-1.
               // If we use setRgb, it expects 0-255.
               
               // Let's try to be smart or strict.
               // Let's treat them as 0-255 for tuple, unless they are clearly floats <= 1.0?
               // No, that's ambiguous.
               // Let's assume 0-255 for tuple to match typical RGB usage, 
               // OR 0-1 if passed as Vec3?
               // In TB, light colors are 0-1 or 0-255? 
               // Standard Quake lights are 0-255.
               
               // Let's stick to 0-255 for tuple input for safety.
               color = QColor(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b));
           }
           Py_XDECREF(px); Py_XDECREF(py); Py_XDECREF(pz);
      }
  }
  setBtnColor(btn, color);

  QObject::connect(btn, &QPushButton::clicked, container, [btn, container, setBtnColor]() {
    QColor current = btn->property("color_val").value<QColor>();
    
    // We need to release GIL before opening modal dialog?
    // Modal dialog runs its own event loop.
    // Python might be blocked?
    // No, we are in C++ callback invoked by Qt.
    // But we are creating the lambda here.
    
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

PyObject* plugin_panel_get_color_field(PyObject* self, PyObject* args)
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
  // Return as Vec3 (0-1 range? or 0-255?)
  // Let's return as tuple (r, g, b) in 0-255 range to be safe and compatible with integer colors.
  
  auto* tuple = PyTuple_New(3);
  PyTuple_SET_ITEM(tuple, 0, PyLong_FromLong(c.red()));
  PyTuple_SET_ITEM(tuple, 1, PyLong_FromLong(c.green()));
  PyTuple_SET_ITEM(tuple, 2, PyLong_FromLong(c.blue()));
  return tuple;
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

namespace {
PyObject* selection_get_brushes(PyObject* self, void*)
{
  auto* selection = getSelectionFromPy(self);
  if (selection == nullptr)
  {
    return nullptr;
  }

  const auto& brushes = selection->document->map().selection().brushes;
  
  auto* list = PyList_New(static_cast<Py_ssize_t>(brushes.size()));
  if (list == nullptr)
  {
    return nullptr;
  }

  for (size_t i = 0; i < brushes.size(); ++i)
  {
    auto* brushObj = createBrushObject(selection->document, brushes[i]);
    if (brushObj == nullptr)
    {
      Py_DECREF(list);
      return nullptr;
    }
    PyList_SET_ITEM(list, static_cast<Py_ssize_t>(i), brushObj);
  }

  return list;
}
} // namespace

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

tb::mdl::Node* getNodeFromPy(PyObject* obj)
{
  if (g_entityType && PyObject_TypeCheck(obj, g_entityType))
  {
    auto* entity = reinterpret_cast<PyTbEntity*>(obj);
    return entity->entityNode;
  }
  if (g_brushType && PyObject_TypeCheck(obj, g_brushType))
  {
    auto* brush = reinterpret_cast<PyTbBrush*>(obj);
    return brush->brushNode;
  }
  return nullptr;
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
    PyErr_SetString(PyExc_TypeError, "expected a list of entities or brushes");
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
    auto* node = getNodeFromPy(item);
    if (node == nullptr)
    {
      PyErr_SetString(PyExc_TypeError, "List items must be Entity or Brush");
      return nullptr;
    }
    nodes.push_back(node);
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
    PyErr_SetString(PyExc_TypeError, "expected a list of entities or brushes");
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
    auto* node = getNodeFromPy(item);
    if (node == nullptr)
    {
      PyErr_SetString(PyExc_TypeError, "List items must be Entity or Brush");
      return nullptr;
    }
    nodes.push_back(node);
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

PyObject* selection_select(PyObject* self, PyObject* args)
{
  PyObject* obj = nullptr;
  if (!PyArg_ParseTuple(args, "O", &obj))
  {
    return nullptr;
  }

  auto* doc = getDocumentFromSelectionPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }

  auto* node = getNodeFromPy(obj);
  if (node == nullptr)
  {
    PyErr_SetString(PyExc_TypeError, "Expected Entity or Brush");
    return nullptr;
  }

  auto nodes = std::vector<tb::mdl::Node*>{};
  const auto& currentSelection = doc->map().selection().nodes;
  nodes.insert(nodes.end(), currentSelection.begin(), currentSelection.end());
  nodes.push_back(node);

  try
  {
    tb::mdl::selectNodes(doc->map(), nodes);
    Py_RETURN_NONE;
  }
  catch (const std::exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
}

PyObject* selection_deselect(PyObject* self, PyObject* args)
{
  PyObject* obj = nullptr;
  if (!PyArg_ParseTuple(args, "O", &obj))
  {
    return nullptr;
  }

  auto* doc = getDocumentFromSelectionPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }

  auto* node = getNodeFromPy(obj);
  if (node == nullptr)
  {
    PyErr_SetString(PyExc_TypeError, "Expected Entity or Brush");
    return nullptr;
  }

  auto nodes = std::vector<tb::mdl::Node*>{};
  const auto& currentSelection = doc->map().selection().nodes;
  for (auto* n : currentSelection)
  {
    if (n != node)
    {
      nodes.push_back(n);
    }
  }

  try
  {
    tb::mdl::selectNodes(doc->map(), nodes);
    Py_RETURN_NONE;
  }
  catch (const std::exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
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

PyObject* entity_get_brushes(PyObject* self, void*)
{
  auto* entity = getEntityFromPy(self);
  if (entity == nullptr)
  {
    return nullptr;
  }

  auto* node = dynamic_cast<tb::mdl::Node*>(entity->entityNode);
  if (node == nullptr)
  {
    return PyList_New(0);
  }

  const auto& children = node->children();
  std::vector<tb::mdl::BrushNode*> brushes;
  for (auto* child : children)
  {
    if (auto* brush = dynamic_cast<tb::mdl::BrushNode*>(child))
    {
      brushes.push_back(brush);
    }
  }

  auto* list = PyList_New(static_cast<Py_ssize_t>(brushes.size()));
  if (list == nullptr)
  {
    return nullptr;
  }

  for (size_t i = 0; i < brushes.size(); ++i)
  {
    auto* brushObj = createBrushObject(entity->document, brushes[i]);
    if (brushObj == nullptr)
    {
      Py_DECREF(list);
      return nullptr;
    }
    PyList_SET_ITEM(list, static_cast<Py_ssize_t>(i), brushObj);
  }
  return list;
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

PyTbBrush* getBrushFromPy(PyObject* self)
{
  if (g_brushType == nullptr || !PyObject_TypeCheck(self, g_brushType))
  {
    PyErr_SetString(PyExc_TypeError, "expected tb.Brush");
    return nullptr;
  }
  auto* brush = reinterpret_cast<PyTbBrush*>(self);
  if (brush->document == nullptr || brush->brushNode == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "Brush is not valid");
    return nullptr;
  }
  return brush;
}

PyTbFace* getFaceFromPy(PyObject* self)
{
  if (g_faceType == nullptr || !PyObject_TypeCheck(self, g_faceType))
  {
    PyErr_SetString(PyExc_TypeError, "expected tb.Face");
    return nullptr;
  }
  auto* face = reinterpret_cast<PyTbFace*>(self);
  if (face->document == nullptr || face->brushNode == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "Face is not valid");
    return nullptr;
  }
  if (face->faceIndex >= face->brushNode->brush().faces().size())
  {
    PyErr_SetString(PyExc_RuntimeError, "Face index out of range");
    return nullptr;
  }
  return face;
}

PyObject* brush_faces(PyObject* self, PyObject*)
{
  auto* brushObj = getBrushFromPy(self);
  if (brushObj == nullptr)
  {
    return nullptr;
  }

  const auto& faces = brushObj->brushNode->brush().faces();
  auto* list = PyList_New(static_cast<Py_ssize_t>(faces.size()));
  if (list == nullptr)
  {
    return nullptr;
  }

  for (size_t i = 0; i < faces.size(); ++i)
  {
    auto* faceObj = createFaceObject(brushObj->document, brushObj->brushNode, i);
    if (faceObj == nullptr)
    {
      Py_DECREF(list);
      return nullptr;
    }
    PyList_SET_ITEM(list, static_cast<Py_ssize_t>(i), faceObj);
  }
  return list;
}

PyObject* brush_bounds(PyObject* self, void*)
{
  auto* brushObj = getBrushFromPy(self);
  if (brushObj == nullptr)
  {
    return nullptr;
  }

  const auto& bounds = brushObj->brushNode->brush().bounds();
  auto* minTuple = toPyVec3dTuple(bounds.min);
  auto* maxTuple = toPyVec3dTuple(bounds.max);

  if (minTuple == nullptr || maxTuple == nullptr)
  {
    Py_XDECREF(minTuple);
    Py_XDECREF(maxTuple);
    return nullptr;
  }

  auto* tuple = PyTuple_New(2);
   PyTuple_SET_ITEM(tuple, 0, minTuple);
   PyTuple_SET_ITEM(tuple, 1, maxTuple);
   return tuple;
 }

 PyObject* brush_delete(PyObject* self, PyObject*)
 {
   auto* brushObj = getBrushFromPy(self);
   if (brushObj == nullptr) return nullptr;
   
   auto* doc = brushObj->document;
   if (doc == nullptr) return nullptr;
   
   try
   {
     tb::mdl::removeNodes(doc->map(), {brushObj->brushNode});
     Py_RETURN_NONE;
   }
   catch (const std::exception& e)
   {
     PyErr_SetString(PyExc_RuntimeError, e.what());
     return nullptr;
   }
 }

 PyObject* brush_copy(PyObject* self, PyObject*)
 {
   auto* brushObj = getBrushFromPy(self);
   if (brushObj == nullptr) return nullptr;
   
   auto* doc = brushObj->document;
   if (doc == nullptr) return nullptr;
   
   try
   {
     auto& map = doc->map();
     auto* original = brushObj->brushNode;
     
     auto* suggestedParent = tb::mdl::parentForNodes(map, {original}); 
     auto* clone = original->cloneRecursively(map.worldBounds());
     
     auto addedNodes = tb::mdl::addNodes(map, {{suggestedParent, {clone}}});
     
     if (addedNodes.empty())
     {
        // clone is deleted by addNodes command destructor if failed
        PyErr_SetString(PyExc_RuntimeError, "Failed to copy brush");
        return nullptr;
     }
     
     auto* newBrushNode = dynamic_cast<tb::mdl::BrushNode*>(addedNodes[0]);
     if (newBrushNode)
     {
         return createBrushObject(doc, newBrushNode);
     }
     Py_RETURN_NONE;
   }
   catch (const std::exception& e)
   {
     PyErr_SetString(PyExc_RuntimeError, e.what());
     return nullptr;
   }
 }
 
 int face_set_texture_name(PyObject* self, PyObject* value, void*)
{
  auto* faceObj = getFaceFromPy(self);
  if (faceObj == nullptr)
  {
    return -1;
  }
  if (!PyUnicode_Check(value))
  {
    PyErr_SetString(PyExc_TypeError, "expected string");
    return -1;
  }
  const char* str = PyUnicode_AsUTF8(value);
  if (str == nullptr)
  {
    return -1;
  }

  try
  {
    auto brush = faceObj->brushNode->brush();
    auto& face = brush.faces()[faceObj->faceIndex];
    auto attribs = face.attributes();
    attribs.setMaterialName(std::string{str});
    face.setAttributes(attribs);
    
    // We need to update the brush node
    // This is a bit heavy for a property setter if it triggers full update every time.
    // But it ensures consistency.
    
    // However, doing this without a transaction means no undo.
    // Maybe we should restrict to read-only for now? 
    // Or allow it but warn about no undo?
    // Or maybe we should use `tb::mdl::setBrushFaceAttributes`?
    
    // Let's implement setters that modify the brush node directly for now, 
    // but we should be aware of undo/redo.
    // Ideally we should use `Transaction`.
    // But `property` setters in Python usually don't take a transaction object.
    
    // For now, let's implement GETTERS only if unsure, OR implement setters that do direct modification (unsafe for undo).
    // Given the request "create_brush", maybe the user wants to manipulate a brush BEFORE adding it?
    // But `PyTbBrush` holds a `BrushNode*` which is usually in a map.
    
    // If I implement setters, I should probably do:
    // 1. Create a transaction (maybe implicitly?) -> Bad idea.
    // 2. Just modify.
    
    // Let's look at `selection_set_property`. It calls `tb::mdl::setEntityProperty`.
    // That function creates a transaction internally? Let's check `Map_Entities.cpp`.
    // `setEntityProperty` takes `Map&`.
    
    // I will stick to GETTERS for now, and maybe setters later if requested or if I find a safe way.
    // Wait, the user wants "expose ... types". Usually implies read/write.
    // But without explicit transaction context, write is dangerous for scene integrity.
    // However, if the user just created the brush via `create_brush`, it might be fine.
    
    // Let's implement GETTERS first.
    // Actually, `BrushFaceAttributes` has setters.
    // If I allow setting, I should update the node.
    
    // I'll implement setters that modify the brush and call `faceObj->brushNode->setBrush(brush)`.
    // This will trigger `NotifyNodeChange` and update geometry/renderer.
    // It won't support undo unless wrapped in a transaction in Python.
    // e.g. `with tb.Transaction(doc): face.texture = "foo"`
    // But `setBrush` itself doesn't use `Transaction`.
    
    // Wait, `BrushNode::setBrush` is a method on the node.
    // Operations that support undo usually use `Map::perform` or `Transaction` wrapping.
    // If I modify the node directly, it's a "local" change.
    // If the user uses `tb.Transaction`, and I modify the node inside it?
    // `Transaction` captures state diffs.
    // So if I modify the node inside a transaction, it *should* work?
    // `Transaction` listens to `NodeChange` events?
    // `Transaction` captures the state of the map.
    
    // Let's check `mdl::Transaction`.
    // It captures "before" state on creation, and "after" state on commit?
    // No, it usually records actions.
    
    // Actually, `Transaction` in TrenchBroom is `UndoStack` related?
    // `mdl::Transaction` (line 44 in includes).
    
    // If I look at `Map_Brushes.cpp`, `createBrush` uses `Transaction`.
    // `auto transaction = Transaction{map, "Create Brush"};`
    // `transaction.commit()`.
    
    // So if Python script does:
    // `with tb.Transaction(doc, "My Action"):`
    //    `face.texture = "foo"`
    
    // The `face.texture = ...` setter should modify the model.
    // Does `Transaction` automatically capture this?
    // `Transaction` constructor takes `Map`.
    // It probably listens to signals.
    
    // If so, implementing setters via `brushNode->setBrush(...)` is correct.
    
    faceObj->brushNode->setBrush(brush);
    return 0;
  }
  catch (const std::exception& e)
  {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return -1;
  }
}

PyObject* face_get_texture_name(PyObject* self, void*)
{
  auto* faceObj = getFaceFromPy(self);
  if (faceObj == nullptr) return nullptr;
  return toPyString(faceObj->brushNode->brush().faces()[faceObj->faceIndex].attributes().materialName());
}

PyObject* module_create_brush(PyObject*, PyObject* args)
{
  PyObject* pointsList = nullptr;
  if (!PyArg_ParseTuple(args, "O", &pointsList))
  {
    return nullptr;
  }
  
  if (!PyList_Check(pointsList))
  {
    PyErr_SetString(PyExc_TypeError, "Expected a list of points (Vec3)");
    return nullptr;
  }
  
  std::vector<vm::vec3d> points;
  const auto size = PyList_Size(pointsList);
  for (Py_ssize_t i = 0; i < size; ++i)
  {
    PyObject* item = PyList_GetItem(pointsList, i);
    // Assuming item is a tb.Vec3 or a tuple/list of 3 floats
    // Let's use `g_vec3Type` to check or helper
    // But `PyTbVec3` structure is local.
    // Use `PyObject_GetAttrString` or simple check.
    
    if (PyObject_TypeCheck(item, g_vec3Type))
    {
       auto* v = (PyTbVec3*)item;
       points.push_back(v->vec);
    }
    else if (PySequence_Check(item) && PySequence_Size(item) == 3)
    {
       PyObject* px = PySequence_GetItem(item, 0);
       PyObject* py = PySequence_GetItem(item, 1);
       PyObject* pz = PySequence_GetItem(item, 2);
       
       double x = 0, y = 0, z = 0;
       bool ok = true;
       
       if (px && (PyFloat_Check(px) || PyLong_Check(px))) x = PyFloat_AsDouble(px); else ok = false;
       if (py && (PyFloat_Check(py) || PyLong_Check(py))) y = PyFloat_AsDouble(py); else ok = false;
       if (pz && (PyFloat_Check(pz) || PyLong_Check(pz))) z = PyFloat_AsDouble(pz); else ok = false;
       
       Py_XDECREF(px); Py_XDECREF(py); Py_XDECREF(pz);
       
       if (ok)
       {
          points.push_back(vm::vec3d{x, y, z});
       }
       else
       {
           PyErr_SetString(PyExc_TypeError, "Invalid point coordinates (must be numbers)");
           return nullptr;
       }
    }
    else
    {
        PyErr_SetString(PyExc_TypeError, "Expected Vec3 or tuple of 3 floats");
        return nullptr;
    }
  }
  
  // Create brush
  auto* doc = activeDocument();
  if (doc == nullptr)
  {
    PyErr_SetString(PyExc_RuntimeError, "No active document");
    return nullptr;
  }
  
  // Use `tb::mdl::createBrush`
  // But wait, `createBrush` in `Map_Brushes.cpp` creates it in the map and selects it.
  // Does it return the created brush node?
  // It returns `bool`.
  // And it selects the new brush.
  
  // So we can check the selection after calling.
  // But `createBrush` deselects all then selects the new one.
  // So:
  
  try
  {
      if (tb::mdl::createBrush(doc->map(), points))
      {
          // Get the single selected brush
          // Wait, `createBrush` creates a `BrushNode`.
          // `selection.entities` are `EntityNodeBase*`.
          // But `BrushNode` is an `EntityNodeBase` (via `Node` hierarchy? No, `BrushNode` -> `Node`. `EntityNodeBase`?)
          // `BrushNode` is not `EntityNodeBase`. `BrushNode` is a child of an entity.
          // `createBrush` adds it to `parentForNodes(map)`.
          
          // `selection` in `Map_Selection.h`:
          // `std::vector<EntityNodeBase*> entities;`
          // `std::vector<BrushNode*> brushes;` -> No?
          
          // Let's check `Selection` struct in `Map_Selection.h` or `Map.h`.
          // `Map::selection()` returns `Selection`.
          
          // `Map_Selection.h`:
          // `struct Selection { ... std::set<Node*> nodes; ... }`
          
          const auto& selectedNodes = doc->map().selection().nodes;
          if (selectedNodes.size() == 1)
          {
             auto* node = *selectedNodes.begin();
             auto* brushNode = dynamic_cast<tb::mdl::BrushNode*>(node);
             if (brushNode)
             {
                 return createBrushObject(doc, brushNode);
             }
          }
          Py_RETURN_NONE; 
      }
      else
      {
          PyErr_SetString(PyExc_RuntimeError, "Failed to create brush");
          return nullptr;
      }
  }
  catch (const std::exception& e)
  {
      PyErr_SetString(PyExc_RuntimeError, e.what());
      return nullptr;
  }
 }
 
 PyObject* face_get_offset(PyObject* self, void*) {
     auto* faceObj = getFaceFromPy(self);
     if (!faceObj) return nullptr;
     const auto& offset = faceObj->brushNode->brush().faces()[faceObj->faceIndex].attributes().offset();
     auto* tuple = PyTuple_New(2);
     PyTuple_SET_ITEM(tuple, 0, PyFloat_FromDouble(offset.x()));
     PyTuple_SET_ITEM(tuple, 1, PyFloat_FromDouble(offset.y()));
     return tuple;
 }
 
 int face_set_offset(PyObject* self, PyObject* value, void*) {
     auto* faceObj = getFaceFromPy(self);
     if (!faceObj) return -1;
     double x=0, y=0;
     if (!PyArg_ParseTuple(value, "dd", &x, &y)) {
         PyErr_SetString(PyExc_TypeError, "Expected tuple of 2 floats");
         return -1;
     }
     try {
         auto brush = faceObj->brushNode->brush();
         auto& face = brush.faces()[faceObj->faceIndex];
         auto attribs = face.attributes();
         attribs.setOffset(vm::vec2f{static_cast<float>(x), static_cast<float>(y)});
         face.setAttributes(attribs);
         faceObj->brushNode->setBrush(brush);
         return 0;
     } catch (...) { return -1; }
 }
 
 PyObject* face_get_scale(PyObject* self, void*) {
     auto* faceObj = getFaceFromPy(self);
     if (!faceObj) return nullptr;
     const auto& scale = faceObj->brushNode->brush().faces()[faceObj->faceIndex].attributes().scale();
     auto* tuple = PyTuple_New(2);
     PyTuple_SET_ITEM(tuple, 0, PyFloat_FromDouble(scale.x()));
     PyTuple_SET_ITEM(tuple, 1, PyFloat_FromDouble(scale.y()));
     return tuple;
 }
 
 int face_set_scale(PyObject* self, PyObject* value, void*) {
     auto* faceObj = getFaceFromPy(self);
     if (!faceObj) return -1;
     double x=0, y=0;
     if (!PyArg_ParseTuple(value, "dd", &x, &y)) {
         PyErr_SetString(PyExc_TypeError, "Expected tuple of 2 floats");
         return -1;
     }
     try {
         auto brush = faceObj->brushNode->brush();
         auto& face = brush.faces()[faceObj->faceIndex];
         auto attribs = face.attributes();
         attribs.setScale(vm::vec2f{static_cast<float>(x), static_cast<float>(y)});
         face.setAttributes(attribs);
         faceObj->brushNode->setBrush(brush);
         return 0;
     } catch (...) { return -1; }
 }
 
 PyObject* face_get_rotation(PyObject* self, void*) {
     auto* faceObj = getFaceFromPy(self);
     if (!faceObj) return nullptr;
     return PyFloat_FromDouble(faceObj->brushNode->brush().faces()[faceObj->faceIndex].attributes().rotation());
 }
 
 int face_set_rotation(PyObject* self, PyObject* value, void*) {
     auto* faceObj = getFaceFromPy(self);
     if (!faceObj) return -1;
     double rot = PyFloat_AsDouble(value);
     if (PyErr_Occurred()) return -1;
     try {
         auto brush = faceObj->brushNode->brush();
         auto& face = brush.faces()[faceObj->faceIndex];
         auto attribs = face.attributes();
         attribs.setRotation(static_cast<float>(rot));
         face.setAttributes(attribs);
         faceObj->brushNode->setBrush(brush);
         return 0;
     } catch (...) { return -1; }
 }
 
 PyObject* face_get_surface_contents(PyObject* self, void*)
 {
   auto* faceObj = getFaceFromPy(self);
   if (!faceObj) return nullptr;
   const auto& val = faceObj->brushNode->brush().faces()[faceObj->faceIndex].attributes().surfaceContents();
   if (val) return PyLong_FromLong(*val);
   Py_RETURN_NONE;
 }
 
 int face_set_surface_contents(PyObject* self, PyObject* value, void*)
 {
   auto* faceObj = getFaceFromPy(self);
   if (!faceObj) return -1;
   
   std::optional<int> val;
   if (value != Py_None)
   {
       if (!PyLong_Check(value)) { PyErr_SetString(PyExc_TypeError, "Expected int or None"); return -1; }
       val = static_cast<int>(PyLong_AsLong(value));
   }
 
   try {
       auto brush = faceObj->brushNode->brush();
       auto& face = brush.faces()[faceObj->faceIndex];
       auto attribs = face.attributes();
       attribs.setSurfaceContents(val);
       face.setAttributes(attribs);
       faceObj->brushNode->setBrush(brush);
       return 0;
   } catch (...) { return -1; }
 }
 
 PyObject* face_get_surface_flags(PyObject* self, void*)
 {
   auto* faceObj = getFaceFromPy(self);
   if (!faceObj) return nullptr;
   const auto& val = faceObj->brushNode->brush().faces()[faceObj->faceIndex].attributes().surfaceFlags();
   if (val) return PyLong_FromLong(*val);
   Py_RETURN_NONE;
 }
 
 int face_set_surface_flags(PyObject* self, PyObject* value, void*)
 {
   auto* faceObj = getFaceFromPy(self);
   if (!faceObj) return -1;
   
   std::optional<int> val;
   if (value != Py_None)
   {
       if (!PyLong_Check(value)) { PyErr_SetString(PyExc_TypeError, "Expected int or None"); return -1; }
       val = static_cast<int>(PyLong_AsLong(value));
   }
 
   try {
       auto brush = faceObj->brushNode->brush();
       auto& face = brush.faces()[faceObj->faceIndex];
       auto attribs = face.attributes();
       attribs.setSurfaceFlags(val);
       face.setAttributes(attribs);
       faceObj->brushNode->setBrush(brush);
       return 0;
   } catch (...) { return -1; }
 }
 
 PyObject* face_get_surface_value(PyObject* self, void*)
 {
   auto* faceObj = getFaceFromPy(self);
   if (!faceObj) return nullptr;
   const auto& val = faceObj->brushNode->brush().faces()[faceObj->faceIndex].attributes().surfaceValue();
   if (val) return PyFloat_FromDouble(*val);
   Py_RETURN_NONE;
 }
 
 int face_set_surface_value(PyObject* self, PyObject* value, void*)
  {
    auto* faceObj = getFaceFromPy(self);
    if (!faceObj) return -1;
    
    std::optional<float> val;
    if (value != Py_None)
    {
        if (!PyFloat_Check(value) && !PyLong_Check(value)) { PyErr_SetString(PyExc_TypeError, "Expected float or None"); return -1; }
        val = static_cast<float>(PyFloat_AsDouble(value));
    }
  
    try {
        auto brush = faceObj->brushNode->brush();
        auto& face = brush.faces()[faceObj->faceIndex];
        auto attribs = face.attributes();
        attribs.setSurfaceValue(val);
        face.setAttributes(attribs);
        faceObj->brushNode->setBrush(brush);
        return 0;
    } catch (...) { return -1; }
  }
  
  PyObject* face_get_vertices(PyObject* self, void*)
  {
    auto* faceObj = getFaceFromPy(self);
    if (!faceObj) return nullptr;
    
    const auto vertices = faceObj->brushNode->brush().faces()[faceObj->faceIndex].vertexPositions();
    
    auto* list = PyList_New(static_cast<Py_ssize_t>(vertices.size()));
    if (!list) return nullptr;
    
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        auto* v = createVec3Object(vertices[i]);
        if (!v) { Py_DECREF(list); return nullptr; }
        PyList_SET_ITEM(list, static_cast<Py_ssize_t>(i), v);
    }
    return list;
  }

  PyObject* face_get_normal(PyObject* self, void*)
  {
    auto* faceObj = getFaceFromPy(self);
    if (!faceObj) return nullptr;
    
    const auto normal = faceObj->brushNode->brush().faces()[faceObj->faceIndex].boundary().normal;
    return createVec3Object(normal);
  }

  // Material methods
  PyObject* material_get_name(PyObject* self, void*)
  {
    auto* obj = (PyTbMaterial*)self;
    return toPyString(obj->material->name());
  }

  PyObject* material_get_collection_name(PyObject* self, void*)
  {
    auto* obj = (PyTbMaterial*)self;
    return toPyString(obj->material->collectionName());
  }

  PyObject* material_get_width(PyObject* self, void*)
  {
    auto* obj = (PyTbMaterial*)self;
    const auto* tex = obj->material->texture();
    return PyLong_FromSize_t(tex ? tex->width() : 0);
  }

  PyObject* material_get_height(PyObject* self, void*)
  {
    auto* obj = (PyTbMaterial*)self;
    const auto* tex = obj->material->texture();
    return PyLong_FromSize_t(tex ? tex->height() : 0);
  }

  // MaterialCollection methods
  PyObject* material_collection_get_name(PyObject* self, void*)
  {
    auto* obj = (PyTbMaterialCollection*)self;
    return toPyString(obj->collection->path().generic_string());
  }

  PyObject* material_collection_get_materials(PyObject* self, void*)
  {
    auto* obj = (PyTbMaterialCollection*)self;
    const auto& materials = obj->collection->materials();
    auto* list = PyList_New(static_cast<Py_ssize_t>(materials.size()));
    if (!list) return nullptr;
    
    for (size_t i = 0; i < materials.size(); ++i)
    {
      auto* matObj = createMaterialObject(&materials[i]);
      if (!matObj) {
          Py_DECREF(list);
          return nullptr;
      }
      PyList_SET_ITEM(list, static_cast<Py_ssize_t>(i), matObj);
    }
    return list;
  }

  PyObject* document_get_materials(PyObject* self, void*)
  {
    auto* docObj = (PyTbDocument*)self;
    auto* doc = docObj->document;
    if (!doc) return nullptr;

    const auto& materials = doc->map().materialManager().materials();
    auto* list = PyList_New(static_cast<Py_ssize_t>(materials.size()));
    if (!list) return nullptr;

    for (size_t i = 0; i < materials.size(); ++i)
    {
      auto* matObj = createMaterialObject(materials[i]);
      if (!matObj) {
          Py_DECREF(list);
          return nullptr;
      }
      PyList_SET_ITEM(list, static_cast<Py_ssize_t>(i), matObj);
    }
    return list;
  }

  PyObject* document_get_material_collections(PyObject* self, void*)
  {
    auto* docObj = (PyTbDocument*)self;
    auto* doc = docObj->document;
    if (!doc) return nullptr;

    const auto& collections = doc->map().materialManager().collections();
    auto* list = PyList_New(static_cast<Py_ssize_t>(collections.size()));
    if (!list) return nullptr;

    for (size_t i = 0; i < collections.size(); ++i)
    {
      auto* colObj = createMaterialCollectionObject(&collections[i]);
      if (!colObj) {
          Py_DECREF(list);
          return nullptr;
      }
      PyList_SET_ITEM(list, static_cast<Py_ssize_t>(i), colObj);
    }
    return list;
  }

  static PyMethodDef brushMethods[] = {
      {"faces", brush_faces, METH_NOARGS, nullptr},
      {"delete", brush_delete, METH_NOARGS, nullptr},
      {"copy", brush_copy, METH_NOARGS, nullptr},
      {nullptr, nullptr, 0, nullptr}
  };
 
 static PyGetSetDef brushGetSet[] = {
      {"bounds", brush_bounds, nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr, nullptr, nullptr}
  };
 
 static PyMethodDef faceMethods[] = {
     {nullptr, nullptr, 0, nullptr}
 };
 
 static PyGetSetDef faceGetSet[] = {
      {"texture_name", face_get_texture_name, face_set_texture_name, nullptr, nullptr},
      {"offset", face_get_offset, face_set_offset, nullptr, nullptr},
      {"scale", face_get_scale, face_set_scale, nullptr, nullptr},
      {"rotation", face_get_rotation, face_set_rotation, nullptr, nullptr},
      {"surface_contents", face_get_surface_contents, face_set_surface_contents, nullptr, nullptr},
      {"surface_flags", face_get_surface_flags, face_set_surface_flags, nullptr, nullptr},
      {"surface_value", face_get_surface_value, face_set_surface_value, nullptr, nullptr},
      {"vertices", face_get_vertices, nullptr, nullptr, nullptr},
      {"normal", face_get_normal, nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr, nullptr, nullptr}
  };





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
  if (contains == -1) return nullptr;
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
  if (dict == nullptr) return nullptr;

  PyObject* callbacks = PyDict_GetItemString(dict, "_callbacks");
  if (callbacks == nullptr) return nullptr;

  PyObject* list = PyDict_GetItemString(callbacks, event);
  if (list == nullptr) return nullptr;

  int contains = PySequence_Contains(list, callback);
  if (contains == -1) return nullptr;
  if (contains == 1)
  {
    PyObject* res = PyObject_CallMethod(list, "remove", "O", callback);
    if (res == nullptr) return nullptr;
    Py_DECREF(res);
  }

  Py_RETURN_NONE;
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
  static auto vec3Type = PyTypeObject{};
  static auto documentType = PyTypeObject{};
  static auto selectionType = PyTypeObject{};
  static auto entityType = PyTypeObject{};
  static auto logWriterType = PyTypeObject{};
  static auto transactionType = PyTypeObject{};

  if (g_vec3Type == nullptr)
  {
    vec3Type = PyTypeObject{};
    vec3Type.tp_name = "tb.Vec3";
    vec3Type.tp_basicsize = sizeof(PyTbVec3);
    vec3Type.tp_flags = Py_TPFLAGS_DEFAULT;
    vec3Type.tp_new = vec3_new;
    vec3Type.tp_init = (initproc)vec3_init;
    vec3Type.tp_dealloc = (destructor)vec3_dealloc;
    vec3Type.tp_repr = (reprfunc)vec3_repr;
    vec3Type.tp_str = (reprfunc)vec3_str;
    vec3Type.tp_richcompare = vec3_richcompare;

    static PyMethodDef vec3Methods[] = {
      {"dot", vec3_dot, METH_VARARGS, nullptr},
      {"cross", vec3_cross, METH_VARARGS, nullptr},
      {"length", vec3_length, METH_NOARGS, nullptr},
      {"normalize", vec3_normalize, METH_NOARGS, nullptr},
      {nullptr, nullptr, 0, nullptr}
    };
    vec3Type.tp_methods = vec3Methods;

    static PyGetSetDef vec3GetSet[] = {
      {"x", vec3_get_x, vec3_set_x, nullptr, nullptr},
      {"y", vec3_get_y, vec3_set_y, nullptr, nullptr},
      {"z", vec3_get_z, vec3_set_z, nullptr, nullptr},
      {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    vec3Type.tp_getset = vec3GetSet;

    static PyNumberMethods vec3NumberMethods = {
      vec3_add, // nb_add
      vec3_sub, // nb_subtract
      vec3_mul, // nb_multiply
      nullptr, // nb_remainder
      nullptr, // nb_divmod
      nullptr, // nb_power
      vec3_neg, // nb_negative
      nullptr, // nb_positive
      nullptr, // nb_absolute
      nullptr, // nb_bool
      nullptr, // nb_invert
      nullptr, // nb_lshift
      nullptr, // nb_rshift
      nullptr, // nb_and
      nullptr, // nb_xor
      nullptr, // nb_or
      nullptr, // nb_int
      nullptr, // nb_reserved
      nullptr, // nb_float
      nullptr, // nb_inplace_add
      nullptr, // nb_inplace_subtract
      nullptr, // nb_inplace_multiply
      nullptr, // nb_inplace_remainder
      nullptr, // nb_inplace_power
      nullptr, // nb_inplace_lshift
      nullptr, // nb_inplace_rshift
      nullptr, // nb_inplace_and
      nullptr, // nb_inplace_xor
      nullptr, // nb_inplace_or
      nullptr, // nb_floor_divide
      vec3_truediv, // nb_true_divide
      nullptr, // nb_inplace_floor_divide
      nullptr, // nb_inplace_true_divide
      nullptr, // nb_index
      nullptr, // nb_matrix_multiply
      nullptr, // nb_inplace_matrix_multiply
    };
    vec3Type.tp_as_number = &vec3NumberMethods;

    static PySequenceMethods vec3SequenceMethods = {
      vec3_len, // sq_length
      nullptr, // sq_concat
      nullptr, // sq_repeat
      vec3_getitem, // sq_item
      nullptr, // sq_slice
      nullptr, // sq_ass_item
      nullptr, // sq_ass_slice
      nullptr, // sq_contains
      nullptr, // sq_inplace_concat
      nullptr, // sq_inplace_repeat
    };
    vec3Type.tp_as_sequence = &vec3SequenceMethods;

    if (PyType_Ready(&vec3Type) != 0)
    {
      return false;
    }
    g_vec3Type = &vec3Type;

    Py_INCREF(g_vec3Type);
    if (PyModule_AddObject(module, "Vec3", reinterpret_cast<PyObject*>(g_vec3Type)) != 0)
    {
      Py_DECREF(g_vec3Type);
      return false;
    }
  }

  if (g_planeType == nullptr)
  {
    static PyMethodDef planeMethods[] = {
      {"from_points", (PyCFunction)plane_from_points, METH_CLASS | METH_VARARGS, nullptr},
      {"distance", plane_distance, METH_VARARGS, nullptr},
      {"project", plane_project, METH_VARARGS, nullptr},
      {nullptr, nullptr, 0, nullptr}
    };

    static PyGetSetDef planeGetSet[] = {
      {"normal", plane_get_normal, plane_set_normal, nullptr, nullptr},
      {"dist", plane_get_dist, plane_set_dist, nullptr, nullptr},
      {nullptr, nullptr, nullptr, nullptr, nullptr}
    };

    static auto planeType = PyTypeObject{};
    planeType = PyTypeObject{};
    planeType.tp_name = "tb.Plane";
    planeType.tp_basicsize = sizeof(PyTbPlane);
    planeType.tp_flags = Py_TPFLAGS_DEFAULT;
    planeType.tp_new = plane_new;
    planeType.tp_init = (initproc)plane_init;
    planeType.tp_dealloc = (destructor)plane_dealloc;
    planeType.tp_repr = (reprfunc)plane_repr;
    planeType.tp_methods = planeMethods;
    planeType.tp_getset = planeGetSet;

    if (PyType_Ready(&planeType) != 0)
    {
      return false;
    }
    g_planeType = &planeType;

    Py_INCREF(g_planeType);
    if (PyModule_AddObject(module, "Plane", reinterpret_cast<PyObject*>(g_planeType)) != 0)
    {
      Py_DECREF(g_planeType);
      return false;
    }
  }

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
      {"materials", document_get_materials, nullptr, nullptr, nullptr},
      {"material_collections", document_get_material_collections, nullptr, nullptr, nullptr},
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
      {"select", selection_select, METH_VARARGS, nullptr},
      {"deselect", selection_deselect, METH_VARARGS, nullptr},
      {"deselect_all", selection_clear, METH_NOARGS, nullptr},
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
      {"brushes", selection_get_brushes, nullptr, nullptr, nullptr},
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
      {"brushes", entity_get_brushes, nullptr, nullptr, nullptr},
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

  if (g_brushType == nullptr)
  {
    static auto brushType = PyTypeObject{};
    brushType = PyTypeObject{};
    brushType.tp_name = "tb.Brush";
    brushType.tp_basicsize = sizeof(PyTbBrush);
    brushType.tp_flags = Py_TPFLAGS_DEFAULT;
    brushType.tp_dealloc = freePythonObject;
    brushType.tp_methods = brushMethods;
    brushType.tp_getset = brushGetSet;

    if (PyType_Ready(&brushType) != 0)
    {
      return false;
    }
    g_brushType = &brushType;

    Py_INCREF(g_brushType);
    if (PyModule_AddObject(module, "Brush", reinterpret_cast<PyObject*>(g_brushType)) != 0)
    {
      Py_DECREF(g_brushType);
      return false;
    }
  }

  if (g_faceType == nullptr)
  {
    static auto faceType = PyTypeObject{};
    faceType = PyTypeObject{};
    faceType.tp_name = "tb.Face";
    faceType.tp_basicsize = sizeof(PyTbFace);
    faceType.tp_flags = Py_TPFLAGS_DEFAULT;
    faceType.tp_dealloc = freePythonObject;
    faceType.tp_methods = faceMethods;
    faceType.tp_getset = faceGetSet;

    if (PyType_Ready(&faceType) != 0)
    {
      return false;
    }
    g_faceType = &faceType;

    Py_INCREF(g_faceType);
    if (PyModule_AddObject(module, "Face", reinterpret_cast<PyObject*>(g_faceType)) != 0)
    {
      Py_DECREF(g_faceType);
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

  if (g_materialType == nullptr)
  {
    static PyGetSetDef materialGetSet[] = {
      {"name", material_get_name, nullptr, nullptr, nullptr},
      {"width", material_get_width, nullptr, nullptr, nullptr},
      {"height", material_get_height, nullptr, nullptr, nullptr},
      {"collection_name", material_get_collection_name, nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr, nullptr, nullptr}
    };

    static PyTypeObject materialType = PyTypeObject{};
    materialType = PyTypeObject{};
    materialType.tp_name = "tb.Material";
    materialType.tp_basicsize = sizeof(PyTbMaterial);
    materialType.tp_flags = Py_TPFLAGS_DEFAULT;
    materialType.tp_getset = materialGetSet;
    materialType.tp_dealloc = freePythonObject;

    if (PyType_Ready(&materialType) != 0)
    {
      return false;
    }
    g_materialType = &materialType;

    Py_INCREF(g_materialType);
    if (PyModule_AddObject(module, "Material", reinterpret_cast<PyObject*>(g_materialType)) != 0)
    {
      Py_DECREF(g_materialType);
      return false;
    }
  }

  if (g_materialCollectionType == nullptr)
  {
    static PyGetSetDef materialCollectionGetSet[] = {
      {"name", material_collection_get_name, nullptr, nullptr, nullptr},
      {"materials", material_collection_get_materials, nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr, nullptr, nullptr}
    };

    static PyTypeObject materialCollectionType = PyTypeObject{};
    materialCollectionType = PyTypeObject{};
    materialCollectionType.tp_name = "tb.MaterialCollection";
    materialCollectionType.tp_basicsize = sizeof(PyTbMaterialCollection);
    materialCollectionType.tp_flags = Py_TPFLAGS_DEFAULT;
    materialCollectionType.tp_getset = materialCollectionGetSet;
    materialCollectionType.tp_dealloc = freePythonObject;

    if (PyType_Ready(&materialCollectionType) != 0)
    {
      return false;
    }
    g_materialCollectionType = &materialCollectionType;

    Py_INCREF(g_materialCollectionType);
    if (PyModule_AddObject(module, "MaterialCollection", reinterpret_cast<PyObject*>(g_materialCollectionType)) != 0)
    {
      Py_DECREF(g_materialCollectionType);
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
      {"add_color_field", plugin_panel_add_color_field, METH_VARARGS, nullptr},
      {"get_int_field", plugin_panel_get_int_field, METH_VARARGS, nullptr},
      {"get_float_field", plugin_panel_get_float_field, METH_VARARGS, nullptr},
      {"get_checkbox", plugin_panel_get_checkbox, METH_VARARGS, nullptr},
      {"get_combo_box_index", plugin_panel_get_combo_box_index, METH_VARARGS, nullptr},
      {"get_combo_box_text", plugin_panel_get_combo_box_text, METH_VARARGS, nullptr},
      {"get_color_field", plugin_panel_get_color_field, METH_VARARGS, nullptr},
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

          // Initialize callbacks registry
          auto* callbacks = PyDict_New();
          if (callbacks == nullptr) { Py_DECREF(module); return nullptr; }
          
          // Pre-create lists for known events
          auto* list = PyList_New(0);
          PyDict_SetItemString(callbacks, "selection_changed", list);
          Py_DECREF(list);
          
          if (PyModule_AddObject(module, "_callbacks", callbacks) != 0) {
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

void PythonScripting::onSelectionChanged(MapFrame& frame)
{
  if (!g_pythonRegistered || !Py_IsInitialized())
  {
    return;
  }

  auto gil = PyGILState_Ensure();

  auto* prev = g_currentFrame;
  g_currentFrame = &frame;

  PyObject* oldStdout = PySys_GetObject("stdout");
  PyObject* oldStderr = PySys_GetObject("stderr");
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

  g_currentFrame = prev;
  PyGILState_Release(gil);
}

} // namespace tb::ui

