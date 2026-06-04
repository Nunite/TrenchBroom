#include "ui/python/PythonTypes.h"
#include "kd/vector_utils.h"
#include "vm/plane.h"

namespace tb::ui {

static PyObject* plane_new(PyTypeObject* type, PyObject*, PyObject*)
{
  auto* self = (PyTbPlane*)type->tp_alloc(type, 0);
  if (self != nullptr)
  {
    new (&self->plane) vm::plane3d{};
  }
  return (PyObject*)self;
}

static int plane_init(PyTbPlane* self, PyObject* args, PyObject*)
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

static void plane_dealloc(PyTbPlane* self)
{
  Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* plane_repr(PyTbPlane* self)
{
  char buffer[256];
  snprintf(buffer, sizeof(buffer), "Plane(normal=Vec3(%g, %g, %g), dist=%g)",
           self->plane.normal.x(), self->plane.normal.y(), self->plane.normal.z(), self->plane.distance);
  return PyUnicode_FromString(buffer);
}

static PyObject* plane_from_points(PyObject*, PyObject* args)
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

static PyObject* plane_distance(PyObject* self, PyObject* args)
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

static PyObject* plane_project(PyObject* self, PyObject* args)
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

static PyObject* plane_get_normal(PyObject* self, void*)
{
  auto* p = (PyTbPlane*)self;
  return createVec3Object(p->plane.normal);
}

static int plane_set_normal(PyObject* self, PyObject* value, void*)
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

static PyObject* plane_get_dist(PyObject* self, void*)
{
  auto* p = (PyTbPlane*)self;
  return PyFloat_FromDouble(p->plane.distance);
}

static int plane_set_dist(PyObject* self, PyObject* value, void*)
{
  double d = PyFloat_AsDouble(value);
  if (PyErr_Occurred()) return -1;
  auto* p = (PyTbPlane*)self;
  p->plane = vm::plane3d{d, p->plane.normal};
  return 0;
}

bool initPlaneType(PyObject* module)
{
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
  return true;
}

} // namespace tb::ui
