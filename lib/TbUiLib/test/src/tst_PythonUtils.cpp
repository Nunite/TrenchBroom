/*
 Copyright (C) 2026 Kristian Duske

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

#include "ui/python/PythonPlane.h"
#include "ui/python/PythonTypes.h"
#include "ui/python/PythonUtils.h"
#include "ui/python/PythonVec3.h"

#include <string>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
struct PythonInterpreter
{
  PythonInterpreter()
  {
    if (!Py_IsInitialized())
    {
      Py_Initialize();
    }
  }
};

struct PyObjectHandle
{
  PyObject* object = nullptr;

  explicit PyObjectHandle(PyObject* i_object)
    : object{i_object}
  {
  }

  ~PyObjectHandle() { Py_XDECREF(object); }

  PyObjectHandle(const PyObjectHandle&) = delete;
  PyObjectHandle& operator=(const PyObjectHandle&) = delete;
  PyObjectHandle(PyObjectHandle&& other) noexcept
    : object{other.object}
  {
    other.object = nullptr;
  }
  PyObjectHandle& operator=(PyObjectHandle&& other) noexcept
  {
    if (this != &other)
    {
      Py_XDECREF(object);
      object = other.object;
      other.object = nullptr;
    }
    return *this;
  }

  PyObject* get() const { return object; }
  PyObject* release()
  {
    auto* result = object;
    object = nullptr;
    return result;
  }
};

PyObjectHandle createPythonModuleWithMathTypes()
{
  auto module = PyObjectHandle{PyModule_New("tb_python_utils_test")};
  REQUIRE(module.get() != nullptr);
  REQUIRE(initVec3Type(module.get()));
  REQUIRE(initPlaneType(module.get()));
  return module;
}

double pyFloatValue(PyObject* object)
{
  REQUIRE(object != nullptr);
  const auto result = PyFloat_AsDouble(object);
  REQUIRE(!PyErr_Occurred());
  return result;
}

double pyFloatAttr(PyObject* object, const char* attr)
{
  auto value = PyObjectHandle{PyObject_GetAttrString(object, attr)};
  return pyFloatValue(value.get());
}
} // namespace

TEST_CASE("PythonUtils")
{
  const auto interpreter = PythonInterpreter{};

  SECTION("converts std::string to Python unicode")
  {
    auto* object = toPyString("hello tb");
    REQUIRE(object != nullptr);
    CHECK(PyUnicode_Check(object));
    CHECK(PyUnicode_AsUTF8(object) == std::string{"hello tb"});
    Py_DECREF(object);
  }

  SECTION("converts vec3d to Python tuple")
  {
    auto* object = toPyVec3dTuple(vm::vec3d{1.5, -2.0, 3.25});
    REQUIRE(object != nullptr);
    REQUIRE(PyTuple_Check(object));
    REQUIRE(PyTuple_Size(object) == 3);
    CHECK(PyFloat_AsDouble(PyTuple_GetItem(object, 0)) == 1.5);
    CHECK(PyFloat_AsDouble(PyTuple_GetItem(object, 1)) == -2.0);
    CHECK(PyFloat_AsDouble(PyTuple_GetItem(object, 2)) == 3.25);
    Py_DECREF(object);
  }

  SECTION("initializes and uses tb.Vec3")
  {
    const auto module = createPythonModuleWithMathTypes();

    auto vec = PyObjectHandle{PyObject_CallFunction(
      reinterpret_cast<PyObject*>(g_vec3Type), "ddd", 1.0, 2.0, 3.0)};
    auto other = PyObjectHandle{PyObject_CallFunction(
      reinterpret_cast<PyObject*>(g_vec3Type), "ddd", 4.0, 5.0, 6.0)};
    REQUIRE(vec.get() != nullptr);
    REQUIRE(other.get() != nullptr);

    CHECK(pyFloatAttr(vec.get(), "x") == 1.0);
    CHECK(pyFloatAttr(vec.get(), "y") == 2.0);
    CHECK(pyFloatAttr(vec.get(), "z") == 3.0);

    auto newX = PyObjectHandle{PyFloat_FromDouble(7.0)};
    REQUIRE(PyObject_SetAttrString(vec.get(), "x", newX.get()) == 0);
    CHECK(pyFloatAttr(vec.get(), "x") == 7.0);

    auto sum = PyObjectHandle{PyNumber_Add(vec.get(), other.get())};
    REQUIRE(sum.get() != nullptr);
    CHECK(pyFloatAttr(sum.get(), "x") == 11.0);
    CHECK(pyFloatAttr(sum.get(), "y") == 7.0);
    CHECK(pyFloatAttr(sum.get(), "z") == 9.0);

    auto scalar = PyObjectHandle{PyLong_FromLong(2)};
    REQUIRE(scalar.get() != nullptr);
    auto scaled = PyObjectHandle{PyNumber_Multiply(vec.get(), scalar.get())};
    REQUIRE(scaled.get() != nullptr);
    CHECK(pyFloatAttr(scaled.get(), "x") == 14.0);
    CHECK(pyFloatAttr(scaled.get(), "y") == 4.0);
    CHECK(pyFloatAttr(scaled.get(), "z") == 6.0);

    auto dot = PyObjectHandle{PyObject_CallMethod(vec.get(), "dot", "O", other.get())};
    CHECK(pyFloatValue(dot.get()) == 56.0);

    auto cross =
      PyObjectHandle{PyObject_CallMethod(vec.get(), "cross", "O", other.get())};
    REQUIRE(cross.get() != nullptr);
    CHECK(pyFloatAttr(cross.get(), "x") == -3.0);
    CHECK(pyFloatAttr(cross.get(), "y") == -30.0);
    CHECK(pyFloatAttr(cross.get(), "z") == 27.0);

    CHECK(PySequence_Size(vec.get()) == 3);
    auto second = PyObjectHandle{PySequence_GetItem(vec.get(), 1)};
    CHECK(pyFloatValue(second.get()) == 2.0);
  }

  SECTION("initializes and uses tb.Plane")
  {
    const auto module = createPythonModuleWithMathTypes();

    auto normal = PyObjectHandle{PyObject_CallFunction(
      reinterpret_cast<PyObject*>(g_vec3Type), "ddd", 0.0, 0.0, 1.0)};
    auto plane = PyObjectHandle{PyObject_CallFunction(
      reinterpret_cast<PyObject*>(g_planeType), "Od", normal.get(), 4.0)};
    REQUIRE(normal.get() != nullptr);
    REQUIRE(plane.get() != nullptr);

    CHECK(pyFloatAttr(plane.get(), "dist") == 4.0);

    auto point = PyObjectHandle{PyObject_CallFunction(
      reinterpret_cast<PyObject*>(g_vec3Type), "ddd", 1.0, 2.0, 7.0)};
    REQUIRE(point.get() != nullptr);

    auto distance =
      PyObjectHandle{PyObject_CallMethod(plane.get(), "distance", "O", point.get())};
    CHECK(pyFloatValue(distance.get()) == 3.0);

    auto projected =
      PyObjectHandle{PyObject_CallMethod(plane.get(), "project", "O", point.get())};
    REQUIRE(projected.get() != nullptr);
    CHECK(pyFloatAttr(projected.get(), "x") == 1.0);
    CHECK(pyFloatAttr(projected.get(), "y") == 2.0);
    CHECK(pyFloatAttr(projected.get(), "z") == 4.0);

    auto p1 = PyObjectHandle{PyObject_CallFunction(
      reinterpret_cast<PyObject*>(g_vec3Type), "ddd", 0.0, 0.0, 0.0)};
    auto p2 = PyObjectHandle{PyObject_CallFunction(
      reinterpret_cast<PyObject*>(g_vec3Type), "ddd", 1.0, 0.0, 0.0)};
    auto p3 = PyObjectHandle{PyObject_CallFunction(
      reinterpret_cast<PyObject*>(g_vec3Type), "ddd", 0.0, 1.0, 0.0)};
    REQUIRE(p1.get() != nullptr);
    REQUIRE(p2.get() != nullptr);
    REQUIRE(p3.get() != nullptr);

    auto fromPoints = PyObjectHandle{PyObject_CallMethod(
      reinterpret_cast<PyObject*>(g_planeType),
      "from_points",
      "OOO",
      p1.get(),
      p2.get(),
      p3.get())};
    REQUIRE(fromPoints.get() != nullptr);
    CHECK(pyFloatAttr(fromPoints.get(), "dist") == 0.0);
  }
}

} // namespace tb::ui
