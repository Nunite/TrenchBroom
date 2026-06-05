#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QThread>

#include "Logger.h"
#include "Result.h"
#include "fs/TestEnvironment.h"
#include "gl/GlManager.h"
#include "gl/ResourceManager.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/GameConfigFixture.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Brushes.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/WorldNode.h"
#include "ui/AppControllerFixture.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/python/PythonHandleRegistry.h"
#include "ui/python/PythonPluginManager.h"
#include "ui/python/PythonPluginSession.h"
#include "ui/python/PythonRuntime.h"
#include "ui/python/PythonScripting.h"

#include "vm/bbox.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
class TestLogger : public Logger
{
public:
  std::vector<std::string> messages;

private:
  void doLog(const LogLevel, const std::string_view message) override
  {
    messages.emplace_back(message);
  }
};

struct CurrentPathGuard
{
  explicit CurrentPathGuard(std::filesystem::path newCurrentPath)
    : m_oldCurrentPath{std::filesystem::current_path()}
  {
    std::filesystem::current_path(std::move(newCurrentPath));
  }

  ~CurrentPathGuard() { std::filesystem::current_path(m_oldCurrentPath); }

private:
  std::filesystem::path m_oldCurrentPath;
};

std::vector<QWidget*> pluginPanels(MapWindow& window)
{
  auto result = std::vector<QWidget*>{};
  for (auto* widget : window.findChildren<QWidget*>())
  {
    if (widget->objectName() == QStringLiteral("PluginInspector_PluginPanel"))
    {
      result.push_back(widget);
    }
  }
  return result;
}
} // namespace

TEST_CASE("PythonV2")
{
  auto appControllerFixture = AppControllerFixture{};
  auto& appController = appControllerFixture.appController();
  auto document = MapDocument::createDocument(
                    appController.environmentConfig(),
                    mdl::QuakeGameInfo,
                    mdl::MapFormat::Valve,
                    vm::bbox3d{8192.0},
                    appController.taskManager(),
                    appController.glManager().resourceManager())
                  | kdl::value();
  auto window = MapWindow{appController, std::move(document)};

  SECTION("runs v2 smoke script")
  {
    auto env = fs::TestEnvironment{};
    auto currentPathGuard = CurrentPathGuard{env.dir()};
    env.createFile(
      "v2_smoke.py",
      R"(
import tb2 as tb

doc = tb.current_document()
assert doc is not None
assert len(doc.entities) >= 1
assert isinstance(doc.materials, list)
with doc.transaction("v2 smoke"):
    pass
with open("python-v2-smoke-ok.txt", "w", encoding="utf-8") as f:
    f.write(doc.entities[0].classname)
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "v2_smoke.py";

    CHECK(PythonRuntime::instance().runScript(context, context.scriptPath));
    CHECK(env.loadFile("python-v2-smoke-ok.txt") == "worldspawn");
  }

  SECTION("loads manifest plugin and emits events")
  {
    auto env = fs::TestEnvironment{};
    auto currentPathGuard = CurrentPathGuard{env.dir()};
    env.createDirectory("plugin");
    env.createFile(
      "plugin/trenchbroom-plugin.json",
      R"({
        "id": "codex.v2",
        "name": "Codex V2",
        "version": "1.0.0",
        "apiVersion": 2,
        "entry": "main.py"
      })");
    env.createFile(
      "plugin/main.py",
      R"(
import tb2 as tb

panel = tb.create_plugin_panel("V2 Plugin")
panel.add_label("Loaded")

def on_selection_changed():
    with open("python-v2-event-ok.txt", "w", encoding="utf-8") as f:
        f.write("selection")

tb.register_callback("selection_changed", on_selection_changed)
)");

    auto manager = PythonPluginManager{};
    manager.reload({env.dir() / "plugin"});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    CHECK(manager.loadPlugins(window));

    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();

    CHECK_FALSE(pluginPanels(window).empty());

    PythonRuntime::instance().emitEvent("selection_changed", window);
    CHECK(env.loadFile("python-v2-event-ok.txt") == "selection");

    REQUIRE(manager.plugins()[0].session != nullptr);
    manager.unloadPlugins(window);
    CHECK(manager.plugins()[0].session == nullptr);
  }

  SECTION("redirects stdout and stderr")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "v2_streams.py",
      R"(
import sys

print("hello stdout")
print("hello stderr", file=sys.stderr)
)");

    auto logger = TestLogger{};
    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &logger;
    context.scriptPath = env.dir() / "v2_streams.py";

    CHECK(PythonRuntime::instance().runScript(context, context.scriptPath));
    CHECK(
      std::find(logger.messages.begin(), logger.messages.end(), "hello stdout")
      != logger.messages.end());
    CHECK(
      std::find(logger.messages.begin(), logger.messages.end(), "hello stderr")
      != logger.messages.end());
  }

  SECTION("reports tracebacks and plugin load errors")
  {
    auto env = fs::TestEnvironment{};
    env.createDirectory("plugin");
    env.createFile(
      "plugin/trenchbroom-plugin.json",
      R"({
        "id": "codex.v2.failure",
        "name": "Codex V2 Failure",
        "version": "1.0.0",
        "apiVersion": 2,
        "entry": "main.py"
      })");
    env.createFile(
      "plugin/main.py",
      R"(
def explode():
    raise RuntimeError("v2 exploded")

explode()
)");

    auto manager = PythonPluginManager{};
    manager.reload({env.dir() / "plugin"});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    CHECK_FALSE(manager.loadPlugins(window));
    REQUIRE(manager.plugins()[0].status == PythonPluginStatus::Failed);
    CHECK(manager.plugins()[0].error.find("Traceback") != std::string::npos);
    CHECK(manager.plugins()[0].error.find("v2 exploded") != std::string::npos);
  }

  SECTION("invalidates document handles on cleanup")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "v2_cache_document.py",
      R"(
import tb2 as tb

tb._cached_document = tb.current_document()
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "v2_cache_document.py";

    REQUIRE(PythonRuntime::instance().runScript(context, context.scriptPath));
    PythonRuntime::instance().cleanupDocument(window);

    env.createFile(
      "v2_use_cached_document.py",
      R"(
import tb2 as tb

tb._cached_document.entities
)");

    CHECK_FALSE(PythonRuntime::instance().runScript(
      context, env.dir() / "v2_use_cached_document.py"));
    CHECK(
      PythonRuntime::instance().lastError().find("Document is no longer valid")
      != std::string::npos);
  }

  SECTION("invalidates entity handles when nodes change")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "v2_cache_entity.py",
      R"(
import tb2 as tb

tb._cached_entity = tb.current_document().entities[0]
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "v2_cache_entity.py";

    REQUIRE(PythonRuntime::instance().runScript(context, context.scriptPath));
    auto* worldNode = static_cast<mdl::Node*>(&window.document().map().worldNode());
    auto nodes = std::vector<mdl::Node*>{worldNode};
    PythonHandleRegistry::instance().invalidateNodes(nodes);

    env.createFile(
      "v2_use_cached_entity.py",
      R"(
import tb2 as tb

tb._cached_entity.classname
)");

    CHECK_FALSE(PythonRuntime::instance().runScript(
      context, env.dir() / "v2_use_cached_entity.py"));
    CHECK(
      PythonRuntime::instance().lastError().find("Entity is no longer valid")
      != std::string::npos);
  }

  SECTION("runs and clears session timers")
  {
    auto env = fs::TestEnvironment{};
    auto currentPathGuard = CurrentPathGuard{env.dir()};
    env.createDirectory("plugin");
    env.createFile(
      "plugin/trenchbroom-plugin.json",
      R"({
        "id": "codex.v2.timer",
        "name": "Codex V2 Timer",
        "version": "1.0.0",
        "apiVersion": 2,
        "entry": "main.py"
      })");
    env.createFile(
      "plugin/main.py",
      R"(
import tb2 as tb

def on_timer():
    with open("python-v2-timer-ok.txt", "a", encoding="utf-8") as f:
        f.write("tick\n")

tb.set_interval(on_timer, 10)
)");

    auto manager = PythonPluginManager{};
    manager.reload({env.dir() / "plugin"});
    REQUIRE(manager.loadPlugins(window));

    for (int i = 0;
         i < 10 && !std::filesystem::exists(env.dir() / "python-v2-timer-ok.txt");
         ++i)
    {
      QApplication::processEvents();
      QThread::msleep(10);
    }
    CHECK(std::filesystem::exists(env.dir() / "python-v2-timer-ok.txt"));

    manager.unloadPlugins(window);
    env.remove("python-v2-timer-ok.txt");
    for (int i = 0; i < 5; ++i)
    {
      QApplication::processEvents();
      QThread::msleep(10);
    }
    CHECK_FALSE(std::filesystem::exists(env.dir() / "python-v2-timer-ok.txt"));
  }

  SECTION("logs timer callback exceptions")
  {
    auto env = fs::TestEnvironment{};
    env.createDirectory("plugin");
    env.createFile(
      "plugin/trenchbroom-plugin.json",
      R"({
        "id": "codex.v2.timer_failure",
        "name": "Codex V2 Timer Failure",
        "version": "1.0.0",
        "apiVersion": 2,
        "entry": "main.py"
      })");
    env.createFile(
      "plugin/main.py",
      R"(
import tb2 as tb

def on_timer():
    raise RuntimeError("timer exploded")

tb.set_timeout(on_timer, 10)
)");

    auto logger = TestLogger{};
    auto manager = PythonPluginManager{};
    manager.reload({env.dir() / "plugin"});
    REQUIRE(manager.plugins().size() == 1u);

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.logger = &logger;
    context.pluginId = manager.plugins()[0].manifest.id;
    context.pluginDirectory = manager.plugins()[0].manifest.directory;
    context.scriptPath =
      manager.plugins()[0].manifest.directory / manager.plugins()[0].manifest.entry;

    auto session = PythonPluginSession{manager.plugins()[0].manifest, std::move(context)};
    REQUIRE(PythonRuntime::instance().runScript(session));

    for (int i = 0; i < 10 && logger.messages.empty(); ++i)
    {
      QApplication::processEvents();
      QThread::msleep(10);
    }

    REQUIRE_FALSE(logger.messages.empty());
    CHECK(logger.messages.back().find("timer exploded") != std::string::npos);
  }

  SECTION("creates common plugin panel controls")
  {
    auto env = fs::TestEnvironment{};
    auto currentPathGuard = CurrentPathGuard{env.dir()};
    env.createDirectory("plugin");
    env.createFile(
      "plugin/trenchbroom-plugin.json",
      R"({
        "id": "codex.v2.controls",
        "name": "Codex V2 Controls",
        "version": "1.0.0",
        "apiVersion": 2,
        "entry": "main.py"
      })");
    env.createFile(
      "plugin/main.py",
      R"(
import tb2 as tb

panel = tb.create_plugin_panel("Controls")
panel.add_label("Loaded")
panel.add_button("Run", lambda: open("python-v2-button-ok.txt", "w", encoding="utf-8").write("button"))
panel.add_checkbox("Enabled", False, lambda value: open("python-v2-checkbox-ok.txt", "w", encoding="utf-8").write(str(value)))
panel.add_line_edit("", lambda value: open("python-v2-line-ok.txt", "w", encoding="utf-8").write(value))
panel.add_combo_box(["a", "b"], 0, lambda value: open("python-v2-combo-ok.txt", "w", encoding="utf-8").write(value))
)");

    auto manager = PythonPluginManager{};
    manager.reload({env.dir() / "plugin"});
    REQUIRE(manager.loadPlugins(window));

    QApplication::processEvents();
    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();

    auto* button = panel->findChild<QPushButton*>();
    REQUIRE(button != nullptr);
    button->click();
    CHECK(env.loadFile("python-v2-button-ok.txt") == "button");

    auto* checkbox = panel->findChild<QCheckBox*>();
    REQUIRE(checkbox != nullptr);
    checkbox->setChecked(true);
    CHECK(env.loadFile("python-v2-checkbox-ok.txt") == "True");

    auto* lineEdit = panel->findChild<QLineEdit*>();
    REQUIRE(lineEdit != nullptr);
    lineEdit->setText("hello");
    CHECK(env.loadFile("python-v2-line-ok.txt") == "hello");

    auto* comboBox = panel->findChild<QComboBox*>();
    REQUIRE(comboBox != nullptr);
    comboBox->setCurrentIndex(1);
    CHECK(env.loadFile("python-v2-combo-ok.txt") == "b");

    manager.unloadPlugins(window);
    env.remove("python-v2-button-ok.txt");
    button->click();
    CHECK_FALSE(std::filesystem::exists(env.dir() / "python-v2-button-ok.txt"));
  }

  SECTION("edits entity properties transactionally")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "v2_edit_entity.py",
      R"(
import tb2 as tb

entity = tb.current_document().entities[0]
entity.set("message", "hello")
assert entity.get("message") == "hello"
entity.remove("message")
assert entity.get("message") is None
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "v2_edit_entity.py";

    REQUIRE(PythonRuntime::instance().runScript(context, context.scriptPath));
    CHECK(window.document().map().worldNode().entity().property("message") == nullptr);
  }

  SECTION("rolls back entity edits on script failure")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "v2_rollback_entity.py",
      R"(
import tb2 as tb

entity = tb.current_document().entities[0]
with tb.current_document().transaction("rollback entity"):
    entity.set("message", "temporary")
    raise RuntimeError("rollback me")
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "v2_rollback_entity.py";

    CHECK_FALSE(PythonRuntime::instance().runScript(context, context.scriptPath));
    CHECK(window.document().map().worldNode().entity().property("message") == nullptr);
  }

  SECTION("edits document selection")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "original") | kdl::value()};
    mdl::addNodes(map, {{mdl::parentForNodes(map), {brushNode}}});

    auto env = fs::TestEnvironment{};
    env.createFile(
      "v2_select.py",
      R"(
import tb2 as tb

doc = tb.current_document()
brush = doc.entities[0].brushes[0]
doc.select([brush])
assert len(doc.selection.brushes) == 1
doc.clear_selection()
assert len(doc.selection.brushes) == 0
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "v2_select.py";

    REQUIRE(PythonRuntime::instance().runScript(context, context.scriptPath));
    CHECK_FALSE(map.selection().hasAny());
  }

  SECTION("sets face material without changing the visible selection")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "original") | kdl::value()};
    mdl::addNodes(map, {{mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    auto env = fs::TestEnvironment{};
    env.createFile(
      "v2_set_face_material.py",
      R"(
import tb2 as tb

brush = tb.current_document().entities[0].brushes[0]
face = brush.faces()[0]
face.set_material("changed")
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "v2_set_face_material.py";

    REQUIRE(PythonRuntime::instance().runScript(context, context.scriptPath));
    CHECK(brushNode->brush().face(0).attributes().materialName() == "changed");
    REQUIRE(map.selection().brushes.size() == 1u);
    CHECK(map.selection().brushes.front() == brushNode);
  }

  SECTION("creates brushes and edits face attributes")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "v2_create_brush.py",
      R"(
import tb2 as tb

half = 32
brush = tb.create_brush([
    tb.Vec3(-half, -half, -half),
    tb.Vec3( half, -half, -half),
    tb.Vec3( half,  half, -half),
    tb.Vec3(-half,  half, -half),
    tb.Vec3(-half, -half,  half),
    tb.Vec3( half, -half,  half),
    tb.Vec3( half,  half,  half),
    tb.Vec3(-half,  half,  half),
], "original")
face = brush.faces()[0]
face.texture_name = "changed"
face.offset = (12.0, 24.0)
face.scale = (0.5, 0.25)
face.rotation = 45.0
face.surface_contents = 7
face.surface_flags = 11
face.surface_value = 3.5
assert face.texture_name == "changed"
assert face.material == "changed"
assert face.offset == (12.0, 24.0)
assert face.scale == (0.5, 0.25)
assert face.rotation == 45.0
assert face.surface_contents == 7
assert face.surface_flags == 11
assert face.surface_value == 3.5
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "v2_create_brush.py";

    REQUIRE(PythonRuntime::instance().runScript(context, context.scriptPath));
    const auto& selectedBrushes = window.document().map().selection().brushes;
    REQUIRE(selectedBrushes.size() == 1u);
    const auto& face = selectedBrushes.front()->brush().faces().front();
    CHECK(face.attributes().materialName() == "changed");
    CHECK(face.attributes().offset() == vm::vec2f{12.0f, 24.0f});
    CHECK(face.attributes().scale() == vm::vec2f{0.5f, 0.25f});
    CHECK(face.attributes().rotation() == 45.0f);
    CHECK(face.attributes().surfaceContents() == std::optional<int>{7});
    CHECK(face.attributes().surfaceFlags() == std::optional<int>{11});
    CHECK(face.attributes().surfaceValue() == std::optional<float>{3.5f});
  }

  SECTION("loads v2 brush builder example plugin")
  {
    const auto pluginDir = std::filesystem::path{"python/examples/v2/brush_builder"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    REQUIRE(manager.loadPlugins(window));

    const auto& selectedBrushes = window.document().map().selection().brushes;
    REQUIRE(selectedBrushes.size() == 1u);
    CHECK(selectedBrushes.front()->brush().faceCount() == 6u);
    manager.unloadPlugins(window);
  }

  SECTION("runs v2 hello panel through legacy script entry")
  {
    const auto helloPanelScript =
      std::filesystem::path{"python/examples/v2/hello_panel/main.py"};
    REQUIRE(std::filesystem::exists(helloPanelScript));
    REQUIRE(pluginPanels(window).empty());
    CAPTURE(PythonRuntime::instance().lastError());
    CHECK(PythonScripting::instance().runScript(window, helloPanelScript));
    CHECK_FALSE(pluginPanels(window).empty());
  }

  SECTION("runs generated v2 panels through direct script entry")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "direct_v2_import.py",
      R"(
import tb2 as tb
)");
    env.createFile(
      "direct_v2_panel.py",
      R"(
import tb2 as tb

panel = tb.create_plugin_panel("V2 Hello")
)");
    env.createFile(
      "direct_v2_label.py",
      R"(
import tb2 as tb

panel = tb.create_plugin_panel("V2 Hello")
panel.add_label("Hello from a direct script.")
)");
    env.createFile(
      "direct_v2_button.py",
      R"(
import tb2 as tb

panel = tb.create_plugin_panel("V2 Hello")
panel.add_label("Hello from a direct script.")
panel.add_button("Print", lambda: print("clicked"))
)");

    CHECK(
      PythonScripting::instance().runScript(window, env.dir() / "direct_v2_import.py"));
    CHECK(
      PythonScripting::instance().runScript(window, env.dir() / "direct_v2_panel.py"));
    CHECK(
      PythonScripting::instance().runScript(window, env.dir() / "direct_v2_label.py"));
    CAPTURE(PythonRuntime::instance().lastError());
    CHECK(
      PythonScripting::instance().runScript(window, env.dir() / "direct_v2_button.py"));
    CHECK_FALSE(pluginPanels(window).empty());
  }
}

} // namespace tb::ui
