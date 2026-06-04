#pragma once

#include <Python.h>
#include <QPointer>
#include <QWidget>

#include "vm/plane.h"
#include "kd/vector_utils.h"

// Forward declarations
namespace tb::ui { class MapDocument; }
namespace tb::mdl { class BrushNode; }
namespace tb::mdl { class EntityNodeBase; }
namespace tb::mdl { class Transaction; }
namespace tb::gl { class Material; }
namespace tb::gl { class MaterialCollection; }

namespace tb::ui {

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
  const tb::gl::Material* material;
};

struct PyTbMaterialCollection
{
  PyObject_HEAD
  const tb::gl::MaterialCollection* collection;
};

struct PyTbPluginPanel
{
  PyObject_HEAD QPointer<QWidget>* container;
};

// Globals
extern PyTypeObject* g_vec3Type;
extern PyTypeObject* g_planeType;
extern PyTypeObject* g_brushType;
extern PyTypeObject* g_faceType;
extern PyTypeObject* g_documentType;
extern PyTypeObject* g_selectionType;
extern PyTypeObject* g_entityType;
extern PyTypeObject* g_logWriterType;
extern PyTypeObject* g_transactionType;
extern PyTypeObject* g_pluginPanelType;
extern PyTypeObject* g_materialType;
extern PyTypeObject* g_materialCollectionType;

// Helper functions
PyObject* createVec3Object(const vm::vec3d& v);
PyObject* createMaterialObject(const tb::gl::Material* material);
PyObject* createMaterialCollectionObject(const tb::gl::MaterialCollection* collection);
PyObject* toPyString(const std::string& str);
PyObject* toPyVec3dTuple(const vm::vec3d& v);
PyObject* createLogWriterObject(const int isError);
PyObject* createDocumentObject(tb::ui::MapDocument* document);
PyObject* createSelectionObject(tb::ui::MapDocument* document);
PyObject* createEntityObject(tb::ui::MapDocument* document, tb::mdl::EntityNodeBase* node);
PyObject* createBrushObject(tb::ui::MapDocument* document, tb::mdl::BrushNode* brushNode);
PyObject* createFaceObject(tb::ui::MapDocument* document, tb::mdl::BrushNode* brushNode, size_t faceIndex);
PyObject* createTransactionObject(tb::ui::MapDocument* document, PyObject* name);
PyObject* createPluginPanelObject(QWidget* container);

tb::ui::MapDocument* getDocumentFromPy(PyObject* self);
PyTbSelection* getSelectionFromPy(PyObject* self);

// Free functions
void freePythonObject(PyObject* self);
void freePluginPanelObject(PyObject* self);
void freeTransactionObject(PyObject* self);

// Init functions
bool initVec3Type(PyObject* module);
bool initPlaneType(PyObject* module);

} // namespace tb::ui
