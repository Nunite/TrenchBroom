#include "PythonDocument.h"
#include "PythonTypes.h"
#include "PythonUtils.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Map_World.h"
#include "mdl/MaterialManager.h"
#include "mdl/MaterialCollection.h"
#include "mdl/VertexHandleManager.h"
#include "mdl/WorldNode.h"
#include "mdl/LayerNode.h"
#include "mdl/GroupNode.h"
#include "mdl/EntityNode.h"
#include "mdl/BrushNode.h"
#include "mdl/PatchNode.h"
#include "ui/MapDocument.h"
#include "kdl/overload.h"
#include "Logger.h"

#include <algorithm>
#include <vector>

namespace tb::ui {

static PyObject* document_selection(PyObject* self, PyObject*)
{
  auto* doc = getDocumentFromPy(self);
  if (doc == nullptr)
  {
    return nullptr;
  }
  return createSelectionObject(doc);
}

static PyObject* document_get_selection(PyObject* self, void*)
{
  return document_selection(self, nullptr);
}

static PyObject* document_transaction(PyObject* self, PyObject* args)
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

static PyObject* document_entities(PyObject* self, void*)
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

static PyObject* document_vertex_tool_vertices(PyObject* self, PyObject*)
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

static PyObject* document_save(PyObject* self, PyObject*)
{
  auto* docObj = (PyTbDocument*)self;
  auto* doc = docObj->document;
  if (!doc) return nullptr;
  
  auto res = doc->map().save();
  if (res.is_error()) {
      (void)res.if_error([](const tb::Error& e) {
          PyErr_SetString(PyExc_RuntimeError, e.msg.c_str());
      });
      return nullptr;
  }
  Py_RETURN_NONE;
}

static PyObject* document_reload(PyObject* self, PyObject*)
{
  auto* docObj = (PyTbDocument*)self;
  auto* doc = docObj->document;
  if (!doc) return nullptr;
  
  auto res = doc->map().reload();
  if (res.is_error()) {
      (void)res.if_error([](const tb::Error& e) {
          PyErr_SetString(PyExc_RuntimeError, e.msg.c_str());
      });
      return nullptr;
  }
  Py_RETURN_NONE;
}

static PyObject* document_get_materials(PyObject* self, void*)
{
  auto* docObj = (PyTbDocument*)self;
  auto* doc = docObj->document;
  if (!doc) {
      PyErr_SetString(PyExc_RuntimeError, "Document is invalid");
      return nullptr;
  }

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

static PyObject* document_get_material_collections(PyObject* self, void*)
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

static PyObject* document_get_path(PyObject* self, void*)
{
  auto* docObj = (PyTbDocument*)self;
  auto* doc = docObj->document;
  if (!doc) return nullptr;

  const auto& path = doc->map().path();
  if (path.empty()) {
      Py_RETURN_NONE;
  }
  
  const auto dirStr = path.u8string();
  return PyUnicode_FromStringAndSize(
    reinterpret_cast<const char*>(dirStr.c_str()), static_cast<Py_ssize_t>(dirStr.size()));
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

PyObject* module_current_document(PyObject*, PyObject*)
{
  auto* doc = activeDocument();
  if (doc == nullptr)
  {
    Py_RETURN_NONE;
  }
  return createDocumentObject(doc);
}

bool initDocumentType(PyObject* module)
{
  if (g_documentType == nullptr)
  {
    static auto documentType = PyTypeObject{};
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
      {"save", document_save, METH_NOARGS, nullptr},
      {"reload", document_reload, METH_NOARGS, nullptr},
      {nullptr, nullptr, 0, nullptr},
    };
    documentType.tp_methods = documentMethods;

    static PyGetSetDef documentGetSet[] = {
      {"selection", document_get_selection, nullptr, nullptr, nullptr},
      {"entities", document_entities, nullptr, nullptr, nullptr},
      {"materials", document_get_materials, nullptr, nullptr, nullptr},
      {"material_collections", document_get_material_collections, nullptr, nullptr, nullptr},
      {"path", document_get_path, nullptr, nullptr, nullptr},
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
  return true;
}

} // namespace tb::ui
