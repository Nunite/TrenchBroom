#include <QApplication>
#include <QLabel>

#include "Logger.h"
#include "Result.h"
#include "fs/TestEnvironment.h"
#include "gl/GlManager.h"
#include "gl/ResourceManager.h"
#include "mdl/GameConfigFixture.h"
#include "mdl/MapFormat.h"
#include "ui/AppControllerFixture.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/python/PythonPluginManager.h"
#include "ui/python/PythonRuntime.h"

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

    manager.unloadPlugins(window);
    env.remove("python-v2-event-ok.txt");
    PythonRuntime::instance().emitEvent("selection_changed", window);
    CHECK_FALSE(std::filesystem::exists(env.dir() / "python-v2-event-ok.txt"));
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
}

} // namespace tb::ui
