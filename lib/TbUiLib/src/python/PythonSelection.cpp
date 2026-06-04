#include "ui/python/PythonSelection.h"
#include "ui/python/PythonTypes.h"
#include "ui/python/PythonUtils.h"

#include "mdl/Map.h"
#include "mdl/Map_Selection.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Geometry.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/EntityNode.h"
#include "mdl/VertexHandleManager.h"
#include "ui/MapDocument.h"
#include "kd/vector_utils.h"

#include <vector>

namespace tb::ui {

namespace {

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

} // namespace

bool initSelectionType(PyObject* module)
{
  if (g_selectionType == nullptr)
  {
    static auto selectionType = PyTypeObject{};
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
  return true;
}

} // namespace tb::ui
