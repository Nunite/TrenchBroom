#include "ui/python/PythonEntity.h"
#include "ui/python/PythonTypes.h"
#include "ui/python/PythonUtils.h"
#include "ui/python/PythonDocument.h"

#include "mdl/EntityNodeBase.h"
#include "mdl/EntityNode.h"
#include "mdl/BrushNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Brushes.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushFaceAttributes.h"
#include "mdl/Map_Selection.h"
#include "ui/MapDocument.h"

namespace tb::ui {

namespace {

bool parseVec2(PyObject* value, double& x, double& y) {
    if (PyTuple_Check(value) && PyTuple_Size(value) == 2) {
        if (!PyArg_ParseTuple(value, "dd", &x, &y)) return false;
        return true;
    }
    if (PyList_Check(value) && PyList_Size(value) == 2) {
        PyObject* i0 = PyList_GetItem(value, 0);
        PyObject* i1 = PyList_GetItem(value, 1);
        if (!i0 || !i1) return false;
        x = PyFloat_AsDouble(i0);
        if (PyErr_Occurred()) return false;
        y = PyFloat_AsDouble(i1);
        if (PyErr_Occurred()) return false;
        return true;
    }
    PyErr_SetString(PyExc_TypeError, "Expected tuple or list of 2 floats");
    return false;
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
    faceObj->brushNode->setBrush(brush);

    // Explicitly notify the map that nodes have changed to update the view
    std::vector<tb::mdl::Node*> changedNodes = {faceObj->brushNode};
    faceObj->document->map().nodesDidChangeNotifier(changedNodes);

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
     if (!parseVec2(value, x, y)) {
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
     } catch (const std::exception& e) {
         PyErr_SetString(PyExc_RuntimeError, e.what());
         return -1;
     } catch (...) {
         PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
         return -1;
     }
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
     } catch (const std::exception& e) {
         PyErr_SetString(PyExc_RuntimeError, e.what());
         return -1;
     } catch (...) {
         PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
         return -1;
     }
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
  } catch (const std::exception& e) {
      PyErr_SetString(PyExc_RuntimeError, e.what());
      return -1;
  } catch (...) {
      PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
      return -1;
  }
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
  } catch (const std::exception& e) {
      PyErr_SetString(PyExc_RuntimeError, e.what());
      return -1;
  } catch (...) {
      PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
      return -1;
  }
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
    } catch (const std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return -1;
    } catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
        return -1;
    }
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

} // namespace

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
  
  try
  {
      if (tb::mdl::createBrush(doc->map(), points))
      {
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
  catch (...)
  {
      PyErr_SetString(PyExc_RuntimeError, "Unknown exception");
      return nullptr;
  }
 }

bool initEntityType(PyObject* module)
{
  if (g_entityType == nullptr)
  {
    static auto entityType = PyTypeObject{};
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
  return true;
}

bool initBrushType(PyObject* module)
{
  if (g_brushType == nullptr)
  {
    static auto brushType = PyTypeObject{};
    brushType.tp_name = "tb.Brush";
    brushType.tp_basicsize = sizeof(PyTbBrush);
    brushType.tp_flags = Py_TPFLAGS_DEFAULT;
    brushType.tp_dealloc = freePythonObject;

    static PyMethodDef brushMethods[] = {
      {"faces", brush_faces, METH_NOARGS, nullptr},
      {"delete", brush_delete, METH_NOARGS, nullptr},
      {"copy", brush_copy, METH_NOARGS, nullptr},
      {nullptr, nullptr, 0, nullptr}
    };
    brushType.tp_methods = brushMethods;

    static PyGetSetDef brushGetSet[] = {
      {"bounds", brush_bounds, nullptr, nullptr, nullptr},
      {nullptr, nullptr, nullptr, nullptr, nullptr}
    };
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
  return true;
}

bool initFaceType(PyObject* module)
{
  if (g_faceType == nullptr)
  {
    static auto faceType = PyTypeObject{};
    faceType.tp_name = "tb.Face";
    faceType.tp_basicsize = sizeof(PyTbFace);
    faceType.tp_flags = Py_TPFLAGS_DEFAULT;
    faceType.tp_dealloc = freePythonObject;

    static PyMethodDef faceMethods[] = {
      {nullptr, nullptr, 0, nullptr}
    };
    faceType.tp_methods = faceMethods;

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
  return true;
}

} // namespace tb::ui
