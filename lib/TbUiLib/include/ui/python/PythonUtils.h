#pragma once
#include <Python.h>
#include <string>
#include "PythonTypes.h"
#include "vm/vec.h"

namespace tb::mdl {
    class Material;
    class MaterialCollection;
    class BrushNode;
    class EntityNodeBase;
    class Node;
    class BrushFace;
}

namespace tb::ui {
    class MapDocument;
    class MapWindow;
}

class QWidget;

namespace tb::ui {

extern thread_local MapWindow* g_currentFrame;

tb::ui::MapDocument* activeDocument();

PyObject* createVec3Object(const vm::vec3d& v);
PyObject* createMaterialObject(const tb::mdl::Material* material);
PyObject* createMaterialCollectionObject(const tb::mdl::MaterialCollection* collection);
PyObject* toPyString(const std::string& str);
PyObject* toPyVec3dTuple(const vm::vec3d& v);

void freePythonObject(PyObject* self);
void freePluginPanelObject(PyObject* self);
void freeTransactionObject(PyObject* self);

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

// Also need these which are used in PythonScripting.cpp
PyTbEntity* getEntityFromPy(PyObject* self);
PyTbLogWriter* getLogWriterFromPy(PyObject* self);
PyTbTransaction* getTransactionFromPy(PyObject* self);
PyTbPluginPanel* getPluginPanelFromPy(PyObject* self);
tb::ui::MapDocument* getDocumentFromSelectionPy(PyObject* self);
tb::mdl::Node* getNodeFromPy(PyObject* obj);
PyTbBrush* getBrushFromPy(PyObject* self);
PyTbFace* getFaceFromPy(PyObject* self);

} // namespace tb::ui
