#include "ui/python/PythonV2Module.h"

#include <QLabel>
#include <QPointer>
#include <QVBoxLayout>
#include <QWidget>

#include "Logger.h"
#include "gl/Material.h"
#include "gl/MaterialCollection.h"
#include "gl/MaterialManager.h"
#include "mdl/Brush.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/PatchNode.h"
#include "mdl/Selection.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"
#include "ui/Action.h"
#include "ui/ActionExecutionContext.h"
#include "ui/ActionManager.h"
#include "ui/Inspector.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/python/PythonExecutionContext.h"
#include "ui/python/PythonHandleRegistry.h"
#include "ui/python/PythonPluginSession.h"
#include "ui/python/PythonRuntime.h"

#include "kd/overload.h"

#if defined(slots)
#undef slots
#endif

#include <pybind11/embed.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace py = pybind11;

namespace tb::ui
{
namespace
{
struct DocumentHandle
{
  MapDocument* document = nullptr;
  size_t generation = 0;

  MapDocument& get() const
  {
    if (
      document == nullptr
      || generation != PythonHandleRegistry::instance().documentGeneration(document))
    {
      throw std::runtime_error{"Document is no longer valid"};
    }
    return *document;
  }
};

struct SelectionHandle
{
  MapDocument* document = nullptr;
  size_t generation = 0;
  MapDocument& getDocument() const { return DocumentHandle{document, generation}.get(); }
};

struct EntityHandle
{
  MapDocument* document = nullptr;
  size_t generation = 0;
  mdl::EntityNodeBase* entity = nullptr;
  size_t nodeGeneration = 0;

  mdl::EntityNodeBase& get() const
  {
    DocumentHandle{document, generation}.get();
    if (
      entity == nullptr
      || nodeGeneration != PythonHandleRegistry::instance().nodeGeneration(entity))
    {
      throw std::runtime_error{"Entity is no longer valid"};
    }
    return *entity;
  }
};

struct BrushHandle
{
  MapDocument* document = nullptr;
  size_t generation = 0;
  mdl::BrushNode* brush = nullptr;
  size_t nodeGeneration = 0;

  mdl::BrushNode& get() const
  {
    DocumentHandle{document, generation}.get();
    if (
      brush == nullptr
      || nodeGeneration != PythonHandleRegistry::instance().nodeGeneration(brush))
    {
      throw std::runtime_error{"Brush is no longer valid"};
    }
    return *brush;
  }
};

struct FaceHandle
{
  MapDocument* document = nullptr;
  size_t generation = 0;
  mdl::BrushNode* brush = nullptr;
  size_t nodeGeneration = 0;
  size_t faceIndex = 0;

  const mdl::BrushFace& get() const
  {
    auto& brushNode = BrushHandle{document, generation, brush, nodeGeneration}.get();
    if (faceIndex >= brushNode.brush().faceCount())
    {
      throw std::runtime_error{"Face is no longer valid"};
    }
    return brushNode.brush().face(faceIndex);
  }
};

struct MaterialHandle
{
  const gl::Material* material = nullptr;

  const gl::Material& get() const
  {
    if (material == nullptr)
    {
      throw std::runtime_error{"Material is no longer valid"};
    }
    return *material;
  }
};

struct TransactionHandle
{
  MapDocument* document = nullptr;
  size_t generation = 0;
  std::string name;
  std::unique_ptr<mdl::Transaction> transaction;

  TransactionHandle(
    MapDocument* i_document, const size_t i_generation, std::string i_name)
    : document{i_document}
    , generation{i_generation}
    , name{std::move(i_name)}
  {
  }

  TransactionHandle& enter()
  {
    if (transaction)
    {
      throw std::runtime_error{"Transaction already started"};
    }
    transaction = std::make_unique<mdl::Transaction>(
      DocumentHandle{document, generation}.get().map(), name);
    return *this;
  }

  bool commit()
  {
    if (!transaction)
    {
      throw std::runtime_error{"Transaction not started"};
    }
    const auto result = transaction->commit();
    transaction.reset();
    return result;
  }

  void cancel()
  {
    if (transaction)
    {
      transaction->cancel();
      transaction.reset();
    }
  }
};

struct PluginPanelHandle
{
  QPointer<QWidget> container;

  QWidget& get() const
  {
    if (container == nullptr)
    {
      throw std::runtime_error{"Plugin panel is no longer valid"};
    }
    return *container;
  }
};

struct CallbackEntry
{
  std::string pluginId;
  py::object callback;
};

std::unordered_map<int, CallbackEntry> g_callbacks;
std::unordered_map<std::string, std::vector<int>> g_eventCallbacks;
int g_nextCallbackToken = 1;

PythonExecutionContext& requireContext()
{
  auto* context = currentPythonExecutionContext();
  if (context == nullptr)
  {
    throw std::runtime_error{"No active Python execution context"};
  }
  return *context;
}

DocumentHandle currentDocument()
{
  auto& context = requireContext();
  if (context.document == nullptr)
  {
    throw std::runtime_error{"No active document"};
  }
  return DocumentHandle{
    context.document,
    PythonHandleRegistry::instance().documentGeneration(context.document)};
}

std::vector<EntityHandle> allEntities(MapDocument& document)
{
  auto result = std::vector<mdl::EntityNodeBase*>{};
  auto visitNode = std::function<void(mdl::Node&)>{};
  visitNode = [&](mdl::Node& node) {
    node.accept(kdl::overload(
      [&](mdl::WorldNode& worldNode) {
        result.push_back(&worldNode);
        worldNode.visitChildren(visitNode);
      },
      [&](mdl::LayerNode& layerNode) { layerNode.visitChildren(visitNode); },
      [&](mdl::GroupNode& groupNode) { groupNode.visitChildren(visitNode); },
      [&](mdl::EntityNode& entityNode) { result.push_back(&entityNode); },
      [&](mdl::BrushNode& brushNode) { result.push_back(brushNode.entity()); },
      [&](mdl::PatchNode& patchNode) { result.push_back(patchNode.entity()); }));
  };
  visitNode(document.map().worldNode());

  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());

  auto handles = std::vector<EntityHandle>{};
  handles.reserve(result.size());
  const auto generation = PythonHandleRegistry::instance().documentGeneration(&document);
  for (auto* entity : result)
  {
    handles.push_back(EntityHandle{
      &document,
      generation,
      entity,
      PythonHandleRegistry::instance().nodeGeneration(entity)});
  }
  return handles;
}

std::vector<BrushHandle> entityBrushes(EntityHandle& entity)
{
  auto& entityNode = entity.get();
  auto result = std::vector<BrushHandle>{};
  entityNode.visitChildren(kdl::overload(
    [&](mdl::BrushNode& brushNode) {
      result.push_back(BrushHandle{
        entity.document,
        entity.generation,
        &brushNode,
        PythonHandleRegistry::instance().nodeGeneration(&brushNode)});
    },
    [](mdl::Node&) {}));
  return result;
}

void executeAction(const std::string& actionPath)
{
  auto& context = requireContext();
  if (context.mapWindow == nullptr || context.appController == nullptr)
  {
    throw std::runtime_error{"No active map window"};
  }

  const auto path = std::filesystem::path{actionPath};
  const auto& actionsMap = ActionManager::instance().actionsMap();
  const auto actionIt = actionsMap.find(path);
  if (actionIt == std::end(actionsMap))
  {
    throw py::key_error{actionPath};
  }

  auto actionContext = ActionExecutionContext{
    *context.appController, context.mapWindow, context.currentMapView};
  const auto& action = actionIt->second;
  if (!action.enabled(actionContext))
  {
    throw std::runtime_error{"Action is disabled"};
  }
  action.execute(actionContext);
}

std::vector<std::string> listActions()
{
  auto result = std::vector<std::string>{};
  const auto& actionsMap = ActionManager::instance().actionsMap();
  result.reserve(actionsMap.size());
  for (const auto& [path, action] : actionsMap)
  {
    unused(action);
    result.push_back(path.generic_string());
  }
  return result;
}

PluginPanelHandle createPluginPanel(const std::string& title)
{
  auto& context = requireContext();
  if (context.mapWindow == nullptr)
  {
    throw std::runtime_error{"No active map window"};
  }

  auto* container = context.mapWindow->addPluginPanel(QString::fromStdString(title));
  context.mapWindow->switchToInspectorPage(InspectorPage::Plugin);
  if (auto* session = currentPythonPluginSession())
  {
    session->addPluginPanel(container);
  }
  return PluginPanelHandle{QPointer<QWidget>{container}};
}

int registerCallback(const std::string& eventName, py::object callback)
{
  if (!PyCallable_Check(callback.ptr()))
  {
    throw py::type_error{"callback must be callable"};
  }

  auto& context = requireContext();
  const auto token = g_nextCallbackToken++;
  g_callbacks.emplace(token, CallbackEntry{context.pluginId, std::move(callback)});
  g_eventCallbacks[eventName].push_back(token);
  if (auto* session = currentPythonPluginSession())
  {
    session->addCallbackToken(token);
  }
  return token;
}

int setInterval(py::object callback, const int milliseconds)
{
  if (!PyCallable_Check(callback.ptr()))
  {
    throw py::type_error{"callback must be callable"};
  }
  if (milliseconds <= 0)
  {
    throw py::value_error{"milliseconds must be greater than zero"};
  }
  auto* session = currentPythonPluginSession();
  if (session == nullptr)
  {
    throw std::runtime_error{"Timers require an active Python plugin session"};
  }
  return session->addIntervalTimer(callback.ptr(), milliseconds, false);
}

int setTimeout(py::object callback, const int milliseconds)
{
  if (!PyCallable_Check(callback.ptr()))
  {
    throw py::type_error{"callback must be callable"};
  }
  if (milliseconds <= 0)
  {
    throw py::value_error{"milliseconds must be greater than zero"};
  }
  auto* session = currentPythonPluginSession();
  if (session == nullptr)
  {
    throw std::runtime_error{"Timers require an active Python plugin session"};
  }
  return session->addIntervalTimer(callback.ptr(), milliseconds, true);
}

void clearInterval(const int token)
{
  auto* session = currentPythonPluginSession();
  if (session == nullptr)
  {
    throw std::runtime_error{"Timers require an active Python plugin session"};
  }
  session->clearTimer(token);
}

void unregisterCallback(const int token)
{
  g_callbacks.erase(token);
  for (auto it = g_eventCallbacks.begin(); it != g_eventCallbacks.end();)
  {
    auto& tokens = it->second;
    tokens.erase(std::remove(tokens.begin(), tokens.end(), token), tokens.end());
    if (tokens.empty())
    {
      it = g_eventCallbacks.erase(it);
    }
    else
    {
      ++it;
    }
  }
}

void emitEvent(const std::string& eventName)
{
  const auto eventIt = g_eventCallbacks.find(eventName);
  if (eventIt == std::end(g_eventCallbacks))
  {
    return;
  }

  auto tokens = eventIt->second;
  for (const auto token : tokens)
  {
    const auto callbackIt = g_callbacks.find(token);
    if (callbackIt != std::end(g_callbacks))
    {
      callbackIt->second.callback();
    }
  }
}

void cleanupPlugin(const std::string& pluginId)
{
  auto tokensToRemove = std::vector<int>{};
  for (const auto& [token, entry] : g_callbacks)
  {
    if (entry.pluginId == pluginId)
    {
      tokensToRemove.push_back(token);
    }
  }
  for (const auto token : tokensToRemove)
  {
    unregisterCallback(token);
  }
}

void cleanupPluginSession(PythonPluginSession& session)
{
  for (const auto token : session.takeCallbackTokens())
  {
    unregisterCallback(token);
  }
  cleanupPlugin(session.pluginId());
  session.clearTimers();
  session.closePluginPanels();
}

void defineModule(py::module_& module)
{
  module.doc() = "TrenchBroom Python API v2";

  py::class_<DocumentHandle>(module, "Document")
    .def_property_readonly(
      "entities", [](DocumentHandle& self) { return allEntities(self.get()); })
    .def_property_readonly(
      "selection",
      [](DocumentHandle& self) { return SelectionHandle{&self.get(), self.generation}; })
    .def_property_readonly(
      "materials",
      [](DocumentHandle& self) {
        auto result = std::vector<MaterialHandle>{};
        const auto& materials = self.get().map().materialManager().materials();
        result.reserve(materials.size());
        for (const auto* material : materials)
        {
          result.push_back(MaterialHandle{material});
        }
        return result;
      })
    .def(
      "transaction",
      [](DocumentHandle& self, std::string name) {
        return TransactionHandle{&self.get(), self.generation, std::move(name)};
      },
      py::arg("name") = "Python v2 Script");

  py::class_<SelectionHandle>(module, "Selection")
    .def_property_readonly("brushes", [](SelectionHandle& self) {
      auto result = std::vector<BrushHandle>{};
      const auto& brushes = self.getDocument().map().selection().brushes;
      result.reserve(brushes.size());
      for (auto* brush : brushes)
      {
        result.push_back(BrushHandle{
          &self.getDocument(),
          self.generation,
          brush,
          PythonHandleRegistry::instance().nodeGeneration(brush)});
      }
      return result;
    });

  py::class_<EntityHandle>(module, "Entity")
    .def_property_readonly(
      "classname", [](EntityHandle& self) { return self.get().entity().classname(); })
    .def_property_readonly("brushes", entityBrushes)
    .def(
      "keys",
      [](EntityHandle& self) {
        auto result = std::vector<std::string>{};
        for (const auto& property : self.get().entity().properties())
        {
          result.push_back(property.key());
        }
        return result;
      })
    .def(
      "get",
      [](EntityHandle& self, const std::string& key, py::object defaultValue) {
        const auto* value = self.get().entity().property(key);
        return value != nullptr ? py::cast(*value) : defaultValue;
      },
      py::arg("key"),
      py::arg("default") = py::none());

  py::class_<BrushHandle>(module, "Brush").def("faces", [](BrushHandle& self) {
    auto result = std::vector<FaceHandle>{};
    const auto& brush = self.get().brush();
    result.reserve(brush.faceCount());
    for (size_t i = 0; i < brush.faceCount(); ++i)
    {
      result.push_back(
        FaceHandle{self.document, self.generation, self.brush, self.nodeGeneration, i});
    }
    return result;
  });

  py::class_<FaceHandle>(module, "Face")
    .def_property_readonly("texture_name", [](FaceHandle& self) {
      return self.get().attributes().materialName();
    });

  py::class_<MaterialHandle>(module, "Material")
    .def_property_readonly("name", [](MaterialHandle& self) { return self.get().name(); })
    .def_property_readonly(
      "width",
      [](MaterialHandle& self) {
        const auto* texture = self.get().texture();
        return texture != nullptr ? texture->width() : 0u;
      })
    .def_property_readonly("height", [](MaterialHandle& self) {
      const auto* texture = self.get().texture();
      return texture != nullptr ? texture->height() : 0u;
    });

  py::class_<TransactionHandle>(module, "Transaction")
    .def("__enter__", &TransactionHandle::enter, py::return_value_policy::reference)
    .def(
      "__exit__",
      [](TransactionHandle& self, py::object excType, py::object, py::object) {
        if (!excType.is_none())
        {
          self.cancel();
          return false;
        }
        self.commit();
        return false;
      })
    .def("commit", &TransactionHandle::commit)
    .def("cancel", &TransactionHandle::cancel);

  py::class_<PluginPanelHandle>(module, "PluginPanel")
    .def("add_label", [](PluginPanelHandle& self, const std::string& text) {
      auto* label = new QLabel{QString::fromStdString(text)};
      label->setWordWrap(true);
      auto* layout = self.get().layout();
      if (layout == nullptr)
      {
        auto* vbox = new QVBoxLayout{};
        self.get().setLayout(vbox);
        layout = vbox;
      }
      layout->addWidget(label);
    });

  module.def("current_document", currentDocument);
  module.def("document", currentDocument);
  module.def("execute_action", executeAction);
  module.def("list_actions", listActions);
  module.def("create_plugin_panel", createPluginPanel);
  module.def("register_callback", registerCallback);
  module.def("unregister_callback", unregisterCallback);
  module.def("set_interval", setInterval);
  module.def("clear_interval", clearInterval);
  module.def("set_timeout", setTimeout);
  module.def("_emit_event", emitEvent);
  module.def("_cleanup_plugin", cleanupPlugin);
  module.def("_cleanup_plugin_session", [](py::capsule sessionCapsule) {
    cleanupPluginSession(
      *reinterpret_cast<PythonPluginSession*>(sessionCapsule.get_pointer()));
  });
  module.def("_invalidate_document", [](py::capsule documentCapsule) {
    PythonHandleRegistry::instance().invalidateDocument(
      reinterpret_cast<MapDocument*>(documentCapsule.get_pointer()));
  });
}
} // namespace

PYBIND11_EMBEDDED_MODULE(tb2, module)
{
  defineModule(module);
}

bool installPythonV2Module()
{
  static auto installed = false;
  if (installed)
  {
    return true;
  }

  if (PyImport_AppendInittab("tb2", PyInit_tb2) != 0)
  {
    return false;
  }
  installed = true;
  return true;
}

} // namespace tb::ui
