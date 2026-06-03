#include "PythonMaterial.h"
#include "PythonTypes.h"
#include "PythonUtils.h"

#include "mdl/Material.h"
#include "mdl/MaterialCollection.h"
#include "mdl/Texture.h"

#include <filesystem>

namespace tb::ui {

namespace {

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

} // namespace

bool initMaterialType(PyObject* module)
{
    if (g_materialType != nullptr) return true;

    static PyGetSetDef materialGetSet[] = {
        {"name", material_get_name, nullptr, nullptr, nullptr},
        {"width", material_get_width, nullptr, nullptr, nullptr},
        {"height", material_get_height, nullptr, nullptr, nullptr},
        {"collection_name", material_get_collection_name, nullptr, nullptr, nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr}
    };

    static PyTypeObject materialType = PyTypeObject{};
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
    return true;
}

bool initMaterialCollectionType(PyObject* module)
{
    if (g_materialCollectionType != nullptr) return true;

    static PyGetSetDef materialCollectionGetSet[] = {
        {"name", material_collection_get_name, nullptr, nullptr, nullptr},
        {"materials", material_collection_get_materials, nullptr, nullptr, nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr}
    };

    static PyTypeObject materialCollectionType = PyTypeObject{};
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
    return true;
}

} // namespace tb::ui
