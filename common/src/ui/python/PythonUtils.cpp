#include "PythonUtils.h"
#include "mdl/Material.h"
#include "mdl/MaterialCollection.h"
#include "mdl/Transaction.h"
#include "mdl/BrushNode.h"
#include "mdl/BrushFace.h"
#include "mdl/EntityNodeBase.h"
#include "ui/MapDocument.h"
#include "ui/MapFrame.h"
#include <QPointer>
#include <QWidget>

namespace tb::ui {

thread_local MapFrame* g_currentFrame = nullptr;

tb::ui::MapDocument* activeDocument()
{
  return g_currentFrame ? &g_currentFrame->document() : nullptr;
}

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

} // namespace tb::ui
