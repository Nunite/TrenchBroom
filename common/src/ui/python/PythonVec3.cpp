#include "PythonTypes.h"
#include "kdl/vector_utils.h" // For vm::vec3d operations like dot, cross, etc.
#include "vm/plane.h" // Ensure vm::vec3d is fully defined if needed

namespace tb::ui {

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

bool initVec3Type(PyObject* module)
{
    if (g_vec3Type == nullptr)
    {
        static auto vec3Type = PyTypeObject{};
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
    return true;
}

} // namespace tb::ui
