#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTextEdit>
#include <QThread>
#include <QTreeWidget>

#include "Logger.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "Result.h"
#include "fs/TestEnvironment.h"
#include "gl/GlManager.h"
#include "gl/Material.h"
#include "gl/MaterialCollection.h"
#include "gl/MaterialManager.h"
#include "gl/ResourceManager.h"
#include "gl/TextureResource.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/GameConfigFixture.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Brushes.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/VertexHandleManager.h"
#include "mdl/WorldNode.h"
#include "ui/AppControllerFixture.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/python/PythonHandleRegistry.h"
#include "ui/python/PythonPluginManager.h"
#include "ui/python/PythonPluginSession.h"
#include "ui/python/PythonRuntime.h"
#include "ui/python/PythonScripting.h"

#include "vm/approx.h"
#include "vm/bbox.h"

#include <fstream>
#include <map>
#include <vector>

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

vm::vec2f textureCoords(const mdl::BrushFace& face, const vm::vec3d& point)
{
  return vm::vec2f{
    face.toUVCoordSystemMatrix(face.attributes().offset(), face.attributes().scale())
    * point};
}

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
  setPref(Preferences::DefaultPluginPaths, "");
  setPref(Preferences::PythonPluginDirectories, "");

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
assert isinstance(doc.material_collections, list)
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
        "pluginType": "ui",
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
        "pluginType": "ui",
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
        "pluginType": "ui",
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
        "pluginType": "ui",
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
        "pluginType": "ui",
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
    const auto buttonPointer = QPointer<QPushButton>{button};
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
    CHECK(buttonPointer.isNull());
    CHECK(pluginPanels(window).empty());
    env.remove("python-v2-button-ok.txt");
    CHECK_FALSE(std::filesystem::exists(env.dir() / "python-v2-button-ok.txt"));
  }

  SECTION("creates named plugin panel fields")
  {
    auto env = fs::TestEnvironment{};
    auto currentPathGuard = CurrentPathGuard{env.dir()};
    env.createFile(
      "fields.py",
      R"(
import tb2 as tb

panel = tb.create_plugin_panel("Fields")
panel.add_label_named("status", "Ready")
panel.set_label_text("status", "Updated")
panel.add_text_field("name", "Name", "world", "placeholder")
panel.add_int_field("count", "Count", 7, 1, 16)
panel.add_float_field("scale", "Scale", 0.5, 0.1, 2.0, 2, 0.1)
panel.add_combo_box("texture", "Texture", ["a", "b"], current="b")
panel.add_checkbox("enabled", "Enabled", True)
assert panel.get_text_field("name") == "world"
assert panel.get_int_field("count") == 7
assert panel.get_float_field("scale") == 0.5
assert panel.get_combo_box_text("texture") == "b"
assert panel.get_checkbox("enabled") is True
panel.add_button_callback("Write", lambda: open("fields-ok.txt", "w", encoding="utf-8").write(panel.get_text_field("name")))
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "fields.py";
    REQUIRE(PythonRuntime::instance().runScript(context, context.scriptPath));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();
    auto* label = panel->findChild<QLabel*>(QStringLiteral("tb2_panel_label_status"));
    REQUIRE(label != nullptr);
    CHECK(label->text() == QStringLiteral("Updated"));
    auto* button = panel->findChild<QPushButton*>();
    REQUIRE(button != nullptr);
    button->click();
    CHECK(env.loadFile("fields-ok.txt") == "world");
  }

  SECTION("supports Vec3 math and color fields")
  {
    auto env = fs::TestEnvironment{};
    auto currentPathGuard = CurrentPathGuard{env.dir()};
    env.createFile(
      "vec3_color.py",
      R"(
import tb2 as tb

a = tb.Vec3(1, 0, 0)
b = tb.Vec3(0, 1, 0)
assert a.dot(b) == 0
assert tuple(a.cross(b)) == (0.0, 0.0, 1.0)
assert (a + b).length() > 1.4
assert tuple((a * 2) / 2) == (1.0, 0.0, 0.0)
assert tuple(a.normalize()) == (1.0, 0.0, 0.0)

plane = tb.Plane.from_points(tb.Vec3(0, 0, 0), tb.Vec3(1, 0, 0), tb.Vec3(0, 1, 0))
assert tuple(plane.normal) == (0.0, 0.0, 1.0)
assert plane.dist == 0.0
assert plane.distance(tb.Vec3(0, 0, 5)) == 5.0
assert tuple(plane.project(tb.Vec3(1, 2, 5))) == (1.0, 2.0, 0.0)

panel = tb.create_plugin_panel("Vec3 Color")
panel.add_color_field("color", "Color", (1, 2, 3))
assert panel.get_color_field("color") == (1, 2, 3)
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "vec3_color.py";
    const auto scriptSucceeded =
      PythonRuntime::instance().runScript(context, context.scriptPath);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(scriptSucceeded);
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

  SECTION("exposes selected triangle UVs to Python")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode = new mdl::BrushNode{
      builder.createBrush(
        std::vector<vm::vec3d>{
          vm::vec3d{-32, 0, 0},
          vm::vec3d{32, 0, 0},
          vm::vec3d{0, 0, 64},
          vm::vec3d{-32, 64, 0},
          vm::vec3d{32, 64, 0},
          vm::vec3d{0, 64, 64},
        },
        "original")
      | kdl::value()};
    mdl::addNodes(map, {{mdl::parentForNodes(map), {brushNode}}});

    const auto faceIndex = brushNode->brush().findFace(vm::vec3d{0, -1, 0});
    REQUIRE(faceIndex);
    mdl::selectBrushFaces(map, {{brushNode, *faceIndex}});

    auto env = fs::TestEnvironment{};
    env.createFile(
      "v2_triangle_uv.py",
      R"(
import tb2 as tb

doc = tb.current_document()
payload = doc.selection.triangle_uvs()
triangles = payload["triangles"]
assert len(triangles) == 1, f"triangle count: {len(triangles)}"
tri = triangles[0]
assert len(tri["vertices"]) == 3, f"vertex count: {len(tri['vertices'])}"
assert len(tri["loops"]) == 3, f"loop count: {len(tri['loops'])}"

uvs = [(10.0, 20.0), (40.0, 20.0), (10.0, 60.0)]
for loop, uv in zip(tri["loops"], uvs):
    loop["uv"] = uv

assert doc.set_triangle_uvs(payload), "set_triangle_uvs failed"
updated = doc.selection.triangle_uvs()["triangles"][0]
assert [loop["uv"] for loop in updated["loops"]] == uvs, updated
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "v2_triangle_uv.py";

    const auto scriptSucceeded =
      PythonRuntime::instance().runScript(context, context.scriptPath);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(scriptSucceeded);
  }

  SECTION("exposes face vertices and UV loops to Python")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "original") | kdl::value()};
    mdl::addNodes(map, {{mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    auto env = fs::TestEnvironment{};
    env.createFile(
      "v2_face_uv.py",
      R"(
import tb2 as tb

doc = tb.current_document()
face = doc.selection.brushes[0].faces()[0]
assert len(face.vertices) == 4, face.vertices
loops = face.uv_loops
assert len(loops) == 4, loops
uvs = [(16.0, 8.0), (80.0, 8.0), (80.0, 40.0), (16.0, 40.0)]
for loop, uv in zip(loops, uvs):
    loop["uv"] = uv
assert face.set_uv_loops(loops)
assert [loop["uv"] for loop in face.uv_loops] == uvs, face.uv_loops
bad = face.uv_loops
bad[3]["uv"] = (23.0, 40.0)
assert not face.set_uv_loops(bad)
assert [loop["uv"] for loop in face.uv_loops] == uvs, face.uv_loops
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "v2_face_uv.py";

    const auto scriptSucceeded =
      PythonRuntime::instance().runScript(context, context.scriptPath);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(scriptSucceeded);
  }

  SECTION("reads selected brush and vertex tool vertices")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "original") | kdl::value()};
    mdl::addNodes(map, {{mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    map.vertexHandles().add(vm::vec3d{1.0, 2.0, 3.0});
    map.vertexHandles().select(vm::vec3d{1.0, 2.0, 3.0});

    auto env = fs::TestEnvironment{};
    env.createFile(
      "v2_selection_vertices.py",
      R"(
import tb2 as tb

doc = tb.current_document()
assert len(doc.vertex_tool_vertices()) == 1
assert tuple(doc.vertex_tool_vertices()[0]) == (1.0, 2.0, 3.0)

verts_by_brush = doc.selection.brush_vertices()
assert len(verts_by_brush) == 1
assert len(verts_by_brush[0]) == 8
assert all(isinstance(v, tb.Vec3) for v in verts_by_brush[0])
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "v2_selection_vertices.py";

    REQUIRE(PythonRuntime::instance().runScript(context, context.scriptPath));
  }

  SECTION("edits selection and transforms selected objects")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNodeA =
      new mdl::BrushNode{builder.createCube(64.0, "move_a") | kdl::value()};
    auto* brushNodeB =
      new mdl::BrushNode{builder.createCube(64.0, "move_b") | kdl::value()};
    mdl::addNodes(map, {{mdl::parentForNodes(map), {brushNodeA, brushNodeB}}});

    auto env = fs::TestEnvironment{};
    env.createFile(
      "v2_selection_transform.py",
      R"(
import tb2 as tb

doc = tb.current_document()
world = doc.entities[0]
brush_a = world.brushes[0]
brush_b = world.brushes[1]

doc.selection.set([brush_a])
assert len(doc.selection.brushes) == 1
doc.selection.add([brush_b])
assert len(doc.selection.brushes) == 2
doc.selection.deselect_all()
assert len(doc.selection.brushes) == 0

doc.selection.set([brush_a])
assert doc.selection.translate(128, 0, 0)
assert doc.selection.rotate(0, 0, 1, 90, 0, 0, 0)
assert doc.selection.scale(1, 1, 1, 0, 0, 0)
doc.selection.duplicate()
assert len(doc.selection.brushes) == 1
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "v2_selection_transform.py";

    const auto scriptSucceeded =
      PythonRuntime::instance().runScript(context, context.scriptPath);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(scriptSucceeded);
    CHECK(map.selection().brushes.size() == 1u);
    CHECK(map.worldNode().defaultLayer()->childCount() == 3u);
  }

  SECTION("chamfers selected vertex and edge handles")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* vertexBrush =
      new mdl::BrushNode{builder.createCube(64.0, "chamfer_vertex") | kdl::value()};
    auto* edgeBrush =
      new mdl::BrushNode{builder.createCube(64.0, "chamfer_edge") | kdl::value()};
    mdl::addNodes(map, {{mdl::parentForNodes(map), {vertexBrush, edgeBrush}}});

    map.vertexHandles().addHandles(*vertexBrush);
    const auto vertexHandles = map.vertexHandles().allHandles();
    REQUIRE_FALSE(vertexHandles.empty());
    map.vertexHandles().select(vertexHandles.front());

    auto env = fs::TestEnvironment{};
    env.createFile(
      "v2_chamfer_vertex.py",
      R"(
import tb2 as tb

doc = tb.current_document()
doc.selection.set([doc.entities[0].brushes[0]])
assert doc.selection.chamfer_vertices(4.0)
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "v2_chamfer_vertex.py";

    auto scriptSucceeded =
      PythonRuntime::instance().runScript(context, context.scriptPath);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(scriptSucceeded);
    CHECK(vertexBrush->brush().vertexCount() > 8u);

    map.vertexHandles().clear();
    map.edgeHandles().addHandles(*edgeBrush);
    const auto edgeHandles = map.edgeHandles().allHandles();
    REQUIRE_FALSE(edgeHandles.empty());
    map.edgeHandles().select(edgeHandles.front());

    env.createFile(
      "v2_chamfer_edge.py",
      R"(
import tb2 as tb

doc = tb.current_document()
doc.selection.set([doc.entities[0].brushes[1]])
assert doc.selection.chamfer_edges(4.0, 2)
)");
    context.scriptPath = env.dir() / "v2_chamfer_edge.py";

    scriptSucceeded = PythonRuntime::instance().runScript(context, context.scriptPath);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(scriptSucceeded);
    CHECK(edgeBrush->brush().faceCount() > 6u);
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

  SECTION("runs v2 brush builder example script")
  {
    const auto pluginDir = std::filesystem::path{"python/examples/v2/brush_builder"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(PythonScripting::instance().runScript(window, pluginDir / "main.py"));

    const auto& selectedBrushes = window.document().map().selection().brushes;
    REQUIRE(selectedBrushes.size() == 1u);
    CHECK(selectedBrushes.front()->brush().faceCount() == 6u);
  }

  SECTION("loads v2 brush manager example plugin")
  {
    const auto pluginDir = std::filesystem::path{"python/examples/v2/brush_manager"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();

    auto* createButton = static_cast<QPushButton*>(nullptr);
    auto* applyButton = static_cast<QPushButton*>(nullptr);
    auto* analyzeButton = static_cast<QPushButton*>(nullptr);
    for (auto* button : panel->findChildren<QPushButton*>())
    {
      if (button->text() == QStringLiteral("Create Cube"))
      {
        createButton = button;
      }
      if (button->text() == QStringLiteral("Apply to Selection"))
      {
        applyButton = button;
      }
      if (button->text() == QStringLiteral("Analyze Selection"))
      {
        analyzeButton = button;
      }
    }
    REQUIRE(createButton != nullptr);
    REQUIRE(applyButton != nullptr);
    REQUIRE(analyzeButton != nullptr);

    createButton->click();
    auto& selection = window.document().map().selection();
    REQUIRE(selection.brushes.size() == 1u);
    auto* brushNode = selection.brushes.front();
    CHECK(brushNode->brush().faceCount() == 6u);

    applyButton->click();
    CHECK(brushNode->brush().face(0).attributes().materialName() == "common/caulk");
    CHECK(brushNode->brush().face(0).attributes().scale() == vm::vec2f{1.0f, 1.0f});

    analyzeButton->click();
    auto* status = panel->findChild<QLabel*>(QStringLiteral("tb2_panel_label_status"));
    REQUIRE(status != nullptr);
    CAPTURE(status->text().toStdString());
    CHECK(status->text().contains(QStringLiteral("6 faces")));
    manager.unloadPlugins(window);
  }

  SECTION("loads v2 texture replacer example plugin")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode = new mdl::BrushNode{builder.createCube(64.0, "old") | kdl::value()};
    mdl::addNodes(map, {{mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    const auto pluginDir = std::filesystem::path{"python/examples/v2/texture_replacer"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();
    auto* find = panel->findChild<QLineEdit*>(QStringLiteral("tb2_panel_text_find"));
    auto* replace =
      panel->findChild<QLineEdit*>(QStringLiteral("tb2_panel_text_replace"));
    REQUIRE(find != nullptr);
    REQUIRE(replace != nullptr);
    find->setText(QStringLiteral("old"));
    replace->setText(QStringLiteral("new"));

    const auto buttons = panel->findChildren<QPushButton*>();
    auto* button = static_cast<QPushButton*>(nullptr);
    for (auto* candidate : buttons)
    {
      if (candidate->text() == QStringLiteral("Replace All in Selection"))
      {
        button = candidate;
      }
    }
    REQUIRE(button != nullptr);
    button->click();
    auto* status = panel->findChild<QLabel*>(QStringLiteral("tb2_panel_label_status"));
    REQUIRE(status != nullptr);
    CAPTURE(status->text().toStdString());
    CHECK(brushNode->brush().face(0).attributes().materialName() == "new");
    manager.unloadPlugins(window);
  }

  SECTION("loads v2 texture browser example plugin")
  {
    auto material =
      gl::Material{"example/stone", gl::createTextureResource(gl::Texture{64u, 32u})};
    auto materials = std::vector<gl::Material>{};
    materials.push_back(std::move(material));
    auto collections = std::vector<gl::MaterialCollection>{};
    collections.emplace_back(
      std::filesystem::path{"textures/example"}, std::move(materials));
    window.document().map().materialManager().setMaterialCollections(
      std::move(collections));

    const auto pluginDir = std::filesystem::path{"python/examples/v2/texture_browser"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();
    auto* status = panel->findChild<QLabel*>(QStringLiteral("tb2_panel_label_status"));
    REQUIRE(status != nullptr);
    const auto text = status->text();
    CAPTURE(text.toStdString());
    CHECK(text.contains(QStringLiteral("textures/example")));
    CHECK(text.contains(QStringLiteral("example/stone (64x32)")));
    manager.unloadPlugins(window);
  }

  SECTION("loads v2 blender brush sync example plugin")
  {
    const auto syncDir =
      std::filesystem::temp_directory_path() / "trenchbroom-blender-sync";
    const auto requestPath = syncDir / "request.json";
    const auto responsePath = syncDir / "response.json";
    std::filesystem::remove(requestPath);
    std::filesystem::remove(responsePath);

    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "old_sync") | kdl::value()};
    mdl::addNodes(map, {{mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    const auto pluginDir = std::filesystem::path{"python/examples/v2/blender_brush_sync"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();
    auto* sendButton = static_cast<QPushButton*>(nullptr);
    auto* applyButton = static_cast<QPushButton*>(nullptr);
    for (auto* button : panel->findChildren<QPushButton*>())
    {
      if (button->text() == QStringLiteral("Send Selection"))
      {
        sendButton = button;
      }
      if (button->text() == QStringLiteral("Apply Pending"))
      {
        applyButton = button;
      }
    }
    REQUIRE(sendButton != nullptr);
    REQUIRE(applyButton != nullptr);

    sendButton->click();

    REQUIRE(std::filesystem::exists(requestPath));
    auto requestStream = std::ifstream{requestPath};
    const auto requestJson =
      std::string{std::istreambuf_iterator<char>{requestStream}, {}};
    CHECK(requestJson.find(R"("schema": "tb.blenderBrushSync.v1")") != std::string::npos);
    CHECK(requestJson.find(R"("brushes")") != std::string::npos);
    CHECK(requestJson.find(R"("faces")") != std::string::npos);

    const auto vertices = brushNode->brush().face(0).vertexPositions();
    REQUIRE(vertices.size() == 4u);

    const auto sessionKey = std::string{R"("sessionId": )"};
    const auto sessionStart = requestJson.find(sessionKey);
    REQUIRE(sessionStart != std::string::npos);
    const auto sessionValueStart =
      requestJson.find('"', sessionStart + sessionKey.size());
    REQUIRE(sessionValueStart != std::string::npos);
    const auto sessionValueEnd = requestJson.find('"', sessionValueStart + 1);
    REQUIRE(sessionValueEnd != std::string::npos);
    const auto sessionValue =
      requestJson.substr(sessionValueStart, sessionValueEnd - sessionValueStart + 1);

    auto response = std::ofstream{responsePath};
    response << R"({
  "schema": "tb.blenderBrushSync.v1",
  "sessionId": )"
             << sessionValue << R"(,
  "faces": [
    {
      "brushId": "brush0",
      "faceId": "face0",
      "material": "new_sync",
      "loops": [
        {"vertex": 0, "uv": [16.0, 8.0]},
        {"vertex": 1, "uv": [80.0, 8.0]},
        {"vertex": 2, "uv": [80.0, 40.0]},
        {"vertex": 3, "uv": [16.0, 40.0]}
      ]
    }
  ],
  "warnings": []
})";
    response.close();

    applyButton->click();
    const auto& face = brushNode->brush().face(0);
    CHECK(face.attributes().materialName() == "new_sync");
    const auto expectedUVs = std::array<vm::vec2f, 4>{
      vm::vec2f{16.0f, 8.0f},
      vm::vec2f{80.0f, 8.0f},
      vm::vec2f{80.0f, 40.0f},
      vm::vec2f{16.0f, 40.0f},
    };
    for (size_t i = 0; i < vertices.size(); ++i)
    {
      const auto uv = textureCoords(face, vertices[i]);
      CHECK(uv.x() == vm::approx{expectedUVs[i].x()});
      CHECK(uv.y() == vm::approx{expectedUVs[i].y()});
    }
    manager.unloadPlugins(window);
    QApplication::processEvents();
    auto ignoredError = std::error_code{};
    std::filesystem::remove(requestPath, ignoredError);
    ignoredError.clear();
    std::filesystem::remove(responsePath, ignoredError);
  }

  SECTION("runs v2 event callback example script")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "event_callback") | kdl::value()};
    mdl::addNodes(map, {{mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    const auto pluginDir = std::filesystem::path{"python/examples/v2/event_callback"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(PythonScripting::instance().runScript(window, pluginDir / "main.py"));

    PythonRuntime::instance().emitEvent("selection_changed", window);
    CHECK(PythonRuntime::instance().lastError().empty());
  }

  SECTION("loads v2 advanced panel example plugin")
  {
    const auto pluginDir = std::filesystem::path{"python/examples/v2/advanced_panel"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();

    auto* updateButton = static_cast<QPushButton*>(nullptr);
    for (auto* button : panel->findChildren<QPushButton*>())
    {
      if (button->text() == QStringLiteral("Update"))
      {
        updateButton = button;
      }
    }
    REQUIRE(updateButton != nullptr);
    updateButton->click();

    auto* name = panel->findChild<QLineEdit*>(QStringLiteral("tb2_panel_text_name"));
    auto* message =
      panel->findChild<QTextEdit*>(QStringLiteral("tb2_panel_text_area_message"));
    auto* table =
      panel->findChild<QTableWidget*>(QStringLiteral("tb2_panel_table_table"));
    auto* tree = panel->findChild<QTreeWidget*>(QStringLiteral("tb2_panel_tree_tree"));
    auto* status = panel->findChild<QLabel*>(QStringLiteral("tb2_panel_label_status"));
    REQUIRE(name != nullptr);
    REQUIRE(message != nullptr);
    REQUIRE(table != nullptr);
    REQUIRE(tree != nullptr);
    REQUIRE(status != nullptr);

    CHECK(name->text() == QStringLiteral("Run 1"));
    CHECK(message->toPlainText().contains(QStringLiteral("Counter=1")));
    REQUIRE(table->rowCount() == 5);
    REQUIRE(table->item(0, 1) != nullptr);
    CHECK(table->item(0, 1)->text() == QStringLiteral("Item 1 (run 1)"));
    CHECK(tree->topLevelItemCount() == 5);
    CHECK(tree->topLevelItem(0)->text(0) == QStringLiteral("Node 1 (run 1)"));

    table->setCurrentCell(0, 1);
    CHECK(status->text() == QStringLiteral("Table selection: row=0, column=1"));
    tree->setCurrentItem(tree->topLevelItem(0));
    CHECK(status->text() == QStringLiteral("Tree selection: row=0"));
    manager.unloadPlugins(window);
  }

  SECTION("creates v2 html views and exposes document path")
  {
    auto env = fs::TestEnvironment{};
    auto currentPathGuard = CurrentPathGuard{env.dir()};
    env.createFile(
      "v2_html_view.py",
      R"(
import tb2 as tb

doc = tb.current_document()
assert doc.path is None or isinstance(doc.path, str)
assert callable(doc.save)
assert callable(doc.reload)

panel = tb.create_plugin_panel("HTML View")

def on_link(link):
    with open("python-v2-html-link-ok.txt", "w", encoding="utf-8") as f:
        f.write(link)

panel.add_html_view("history", '<a href="tb://history/123">History</a>', 120, on_link)
panel.set_html_view("history", '<a href="tb://history/456">Updated</a>')
)");

    auto context = PythonExecutionContext{};
    context.mapWindow = &window;
    context.document = &window.document();
    context.appController = &window.appController();
    context.currentMapView = window.currentMapViewBase();
    context.logger = &window.pythonLogger();
    context.scriptPath = env.dir() / "v2_html_view.py";

    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(PythonRuntime::instance().runScript(context, context.scriptPath));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* htmlView = panels.back()->findChild<QTextBrowser*>(
      QStringLiteral("tb2_panel_html_view_history"));
    REQUIRE(htmlView != nullptr);
    CHECK(htmlView->toPlainText().contains(QStringLiteral("Updated")));

    emit htmlView->anchorClicked(QUrl{QStringLiteral("tb://history/456")});
    CHECK(env.loadFile("python-v2-html-link-ok.txt") == "tb://history/456");
  }

  SECTION("loads v2 git plugin example")
  {
    const auto pluginDir = std::filesystem::path{"python/examples/v2/git_plugin"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();
    auto* noDocMessage =
      panel->findChild<QLabel*>(QStringLiteral("tb2_panel_label_no_doc_msg"));
    REQUIRE(noDocMessage != nullptr);
    CHECK(noDocMessage->text().contains(QStringLiteral("Please save the map")));

    manager.unloadPlugins(window);
    QApplication::processEvents();
  }

  SECTION("loads v2 vec3 color demo example plugin")
  {
    auto& map = window.document().map();
    auto* entityNode = new mdl::EntityNode{
      mdl::Entity{{{mdl::EntityPropertyKeys::Classname, "info_player_start"}}}};
    auto nodesToAdd = std::map<mdl::Node*, std::vector<mdl::Node*>>{};
    nodesToAdd.emplace(
      static_cast<mdl::Node*>(map.worldNode().defaultLayer()),
      std::vector<mdl::Node*>{static_cast<mdl::Node*>(entityNode)});
    mdl::addNodes(map, nodesToAdd);

    auto nodesToSelect = std::vector<mdl::Node*>{static_cast<mdl::Node*>(entityNode)};
    mdl::selectNodes(map, nodesToSelect);

    const auto pluginDir = std::filesystem::path{"python/examples/v2/vec3_color_demo"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();

    auto* dotButton = static_cast<QPushButton*>(nullptr);
    auto* colorButton = static_cast<QPushButton*>(nullptr);
    for (auto* button : panel->findChildren<QPushButton*>())
    {
      if (button->text() == QStringLiteral("Calculate Dot (A . B)"))
      {
        dotButton = button;
      }
      if (button->text() == QStringLiteral("Apply Color to Selection (_color)"))
      {
        colorButton = button;
      }
    }
    REQUIRE(dotButton != nullptr);
    REQUIRE(colorButton != nullptr);

    dotButton->click();
    auto* result = panel->findChild<QLabel*>(QStringLiteral("tb2_panel_label_result"));
    REQUIRE(result != nullptr);
    CHECK(result->text().contains(QStringLiteral("Dot Product")));

    colorButton->click();
    const auto* colorProperty = entityNode->entity().property("_color");
    REQUIRE(colorProperty != nullptr);
    CHECK(*colorProperty == "1.0 0.0 0.0");
    manager.unloadPlugins(window);
  }

  SECTION("loads v2 plane selection example plugin")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "plane") | kdl::value()};
    mdl::addNodes(map, {{mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    const auto pluginDir =
      std::filesystem::path{"python/examples/v2/plane_selection_demo"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();
    auto* label =
      panel->findChild<QLabel*>(QStringLiteral("tb2_panel_label_selection_info"));
    REQUIRE(label != nullptr);

    auto* inspectButton = static_cast<QPushButton*>(nullptr);
    for (auto* button : panel->findChildren<QPushButton*>())
    {
      if (button->text() == QStringLiteral("Inspect Selected Brush Vertices"))
      {
        inspectButton = button;
      }
    }
    REQUIRE(inspectButton != nullptr);
    inspectButton->click();
    CHECK(label->text().contains(QStringLiteral("Brushes: 1")));
    manager.unloadPlugins(window);
  }

  SECTION("runs v2 print selected vertices example script")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "verts") | kdl::value()};
    mdl::addNodes(map, {{mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    const auto pluginDir =
      std::filesystem::path{"python/examples/v2/print_selected_vertices"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(PythonScripting::instance().runScript(window, pluginDir / "main.py"));
  }

  SECTION("loads v2 plane builder example plugin")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "plane_builder") | kdl::value()};
    mdl::addNodes(map, {{mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    const auto pluginDir = std::filesystem::path{"python/examples/v2/plane_builder"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    REQUIRE(manager.loadPlugins(window));
    CHECK(map.worldNode().defaultLayer()->childCount() == 10u);
    manager.unloadPlugins(window);
  }

  SECTION("runs v2 spin entity generator example script")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* entityNode = new mdl::EntityNode{mdl::Entity{
      {{mdl::EntityPropertyKeys::Classname, "func_detail"},
       {"_angle", "90"},
       {"_count", "2"},
       {"_pivot", "0 0 0"},
       {"_axis", "0 0 1"}}}};
    auto* brushNode = new mdl::BrushNode{builder.createCube(64.0, "spin") | kdl::value()};

    auto entityNodesToAdd = std::map<mdl::Node*, std::vector<mdl::Node*>>{};
    entityNodesToAdd.emplace(
      static_cast<mdl::Node*>(map.worldNode().defaultLayer()),
      std::vector<mdl::Node*>{static_cast<mdl::Node*>(entityNode)});
    mdl::addNodes(map, entityNodesToAdd);

    auto brushNodesToAdd = std::map<mdl::Node*, std::vector<mdl::Node*>>{};
    brushNodesToAdd.emplace(
      static_cast<mdl::Node*>(entityNode),
      std::vector<mdl::Node*>{static_cast<mdl::Node*>(brushNode)});
    mdl::addNodes(map, brushNodesToAdd);
    mdl::selectNodes(map, {entityNode});

    const auto pluginDir =
      std::filesystem::path{"python/examples/v2/generator_spin_entity"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(PythonScripting::instance().runScript(window, pluginDir / "main.py"));
    CHECK(map.selection().nodes.size() == 1u);
  }

  SECTION("loads v2 chamfer example plugins")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "chamfer_example") | kdl::value()};
    mdl::addNodes(map, {{mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});
    map.vertexHandles().addHandles(*brushNode);
    const auto vertexHandles = map.vertexHandles().allHandles();
    REQUIRE_FALSE(vertexHandles.empty());
    map.vertexHandles().select(vertexHandles.front());

    auto manager = PythonPluginManager{};
    const auto chamferToolDir = std::filesystem::path{"python/examples/v2/chamfer_tool"};
    manager.reload({chamferToolDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();
    auto* vertexButton = static_cast<QPushButton*>(nullptr);
    for (auto* button : panel->findChildren<QPushButton*>())
    {
      if (button->text() == QStringLiteral("Chamfer Vertex Handles"))
      {
        vertexButton = button;
      }
    }
    REQUIRE(vertexButton != nullptr);
    vertexButton->click();
    CHECK(brushNode->brush().vertexCount() > 8u);
    manager.unloadPlugins(window);

    const auto simpleChamferDir =
      std::filesystem::path{"python/examples/v2/simple_chamfer_edge"};
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(PythonScripting::instance().runScript(window, simpleChamferDir / "main.py"));
  }

  SECTION("loads v2 transform tool example plugin")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "transform_tool") | kdl::value()};
    mdl::addNodes(map, {{mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});
    map.vertexHandles().addHandles(*brushNode);
    const auto vertexHandles = map.vertexHandles().allHandles();
    REQUIRE_FALSE(vertexHandles.empty());
    map.vertexHandles().select(vertexHandles.front());

    const auto pluginDir = std::filesystem::path{"python/examples/v2/transform_tool"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();

    auto* recordButton = static_cast<QPushButton*>(nullptr);
    auto* applyButton = static_cast<QPushButton*>(nullptr);
    for (auto* button : panel->findChildren<QPushButton*>())
    {
      if (button->text() == QStringLiteral("Record Pivot From Vertex Handles"))
      {
        recordButton = button;
      }
      if (button->text() == QStringLiteral("Apply Duplicate + Rotate"))
      {
        applyButton = button;
      }
    }
    REQUIRE(recordButton != nullptr);
    REQUIRE(applyButton != nullptr);

    auto* duplicateCount =
      panel->findChild<QSpinBox*>(QStringLiteral("tb2_panel_int_dup_count"));
    REQUIRE(duplicateCount != nullptr);
    duplicateCount->setValue(1);

    recordButton->click();
    applyButton->click();
    CHECK(map.worldNode().defaultLayer()->childCount() == 2u);
    manager.unloadPlugins(window);
  }

  SECTION("loads v2 distribute tool example plugin")
  {
    auto& map = window.document().map();
    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "distribute_tool") | kdl::value()};
    mdl::addNodes(map, {{mdl::parentForNodes(map), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    map.vertexHandles().add(vm::vec3d{0.0, 0.0, 0.0});
    map.vertexHandles().add(vm::vec3d{128.0, 0.0, 0.0});
    map.vertexHandles().select(vm::vec3d{0.0, 0.0, 0.0});
    map.vertexHandles().select(vm::vec3d{128.0, 0.0, 0.0});

    const auto pluginDir = std::filesystem::path{"python/examples/v2/distribute_tool"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    auto manager = PythonPluginManager{};
    manager.reload({pluginDir});
    REQUIRE(manager.errors().empty());
    REQUIRE(manager.plugins().size() == 1u);
    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(manager.loadPlugins(window));

    const auto panels = pluginPanels(window);
    REQUIRE_FALSE(panels.empty());
    auto* panel = panels.back();

    auto* recordButton = static_cast<QPushButton*>(nullptr);
    auto* distributeButton = static_cast<QPushButton*>(nullptr);
    for (auto* button : panel->findChildren<QPushButton*>())
    {
      if (button->text() == QStringLiteral("Record Path From Selection"))
      {
        recordButton = button;
      }
      if (button->text() == QStringLiteral("Distribute Selected Objects"))
      {
        distributeButton = button;
      }
    }
    REQUIRE(recordButton != nullptr);
    REQUIRE(distributeButton != nullptr);

    recordButton->click();
    distributeButton->click();
    CHECK(map.worldNode().defaultLayer()->childCount() > 1u);
    manager.unloadPlugins(window);
  }

  SECTION("runs v2 entity brush modifier example script")
  {
    auto& map = window.document().map();
    auto entity = mdl::Entity{};
    entity.setClassname("func_detail");
    auto* entityNode = new mdl::EntityNode{std::move(entity)};

    auto builder = mdl::BrushBuilder{mdl::MapFormat::Valve, vm::bbox3d{8192.0}};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "entity_brush") | kdl::value()};

    mdl::addNodes(
      map,
      {{static_cast<mdl::Node*>(map.worldNode().defaultLayer()),
        {static_cast<mdl::Node*>(entityNode)}}});
    mdl::addNodes(
      map, {{static_cast<mdl::Node*>(entityNode), {static_cast<mdl::Node*>(brushNode)}}});
    mdl::selectNodes(map, {entityNode});

    const auto oldOffset = brushNode->brush().face(0).attributes().offset();
    const auto pluginDir =
      std::filesystem::path{"python/examples/v2/entity_brush_modifier"};
    REQUIRE(std::filesystem::exists(pluginDir / "trenchbroom-plugin.json"));

    CAPTURE(PythonRuntime::instance().lastError());
    REQUIRE(PythonScripting::instance().runScript(window, pluginDir / "main.py"));

    const auto newOffset = brushNode->brush().face(0).attributes().offset();
    CHECK(newOffset.x() == oldOffset.x() + 16.0f);
    CHECK(newOffset.y() == oldOffset.y());
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
