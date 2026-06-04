/*
 Copyright (C) 2026 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include <QApplication>
#include <QComboBox>
#include <QDeadlineTimer>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QThread>
#include <QWidget>
#include <QtTest/QTest>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "Result.h"
#include "fs/TestEnvironment.h"
#include "gl/GlManager.h"
#include "gl/Resource.h"
#include "gl/ResourceManager.h"
#include "gl/TestGl.h"
#include "gl/TestUtils.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushFaceAttributes.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/GameConfigFixture.h"
#include "mdl/Grid.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/WorldNode.h"
#include "ui/AppControllerFixture.h"
#include "ui/CatchConfig.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/MapViewBase.h"
#include "ui/MapWindow.h"
#include "ui/PieMenu.h"
#include "ui/QPathUtils.h"
#include "ui/python/PythonScripting.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

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

void sendMouseMove(QWidget& widget, const QPoint& pos)
{
  auto event = QMouseEvent{
    QEvent::MouseMove,
    QPointF{pos},
    QPointF{widget.mapToGlobal(pos)},
    Qt::NoButton,
    Qt::NoButton,
    Qt::NoModifier};
  QApplication::sendEvent(&widget, &event);
}

void sendMouseRelease(QWidget& widget, const QPoint& pos)
{
  auto event = QMouseEvent{
    QEvent::MouseButtonRelease,
    QPointF{pos},
    QPointF{widget.mapToGlobal(pos)},
    Qt::LeftButton,
    Qt::LeftButton,
    Qt::NoModifier};
  QApplication::sendEvent(&widget, &event);
}

PieMenu* findVisiblePieMenu()
{
  for (auto* widget : QApplication::topLevelWidgets())
  {
    if (auto* menu = qobject_cast<PieMenu*>(widget); menu && menu->isVisible())
    {
      return menu;
    }
  }
  return nullptr;
}

} // namespace

// Simple resource type for testing resource processing
struct TestResource
{
  void upload(gl::Gl&) const {}
  void drop(gl::Gl&) const {}
};

TEST_CASE("MapWindow")
{
  UNSCOPED_INFO("creating AppControllerFixture");
  auto appControllerFixture = AppControllerFixture{};
  auto& appController = appControllerFixture.appController();

  UNSCOPED_INFO("creating MapDocument");
  auto document = MapDocument::createDocument(
                    appController.environmentConfig(),
                    mdl::QuakeGameInfo,
                    mdl::MapFormat::Valve,
                    vm::bbox3d{8192.0},
                    appController.taskManager(),
                    appController.glManager().resourceManager())
                  | kdl::value();

  UNSCOPED_INFO("creating MapWindow");
  auto window = MapWindow{appController, std::move(document)};
  UNSCOPED_INFO("MapWindow created");

  SECTION("load resets grid size dropdown")
  {
    auto* gridChoice = window.findChild<QComboBox*>("MapWindow_GridChoice");
    REQUIRE(gridChoice != nullptr);

    const auto changedGridSize = 5;
    const auto changedGridIndex = changedGridSize - mdl::Grid::MinSize;
    const auto path = pathFromQString(QApplication::applicationDirPath())
                      / "fixture/test/ui/MapDocument/emptyValveMap.map";

    window.setGridSize(changedGridSize);
    QApplication::processEvents();

    REQUIRE(window.document().map().grid().size() == changedGridSize);
    REQUIRE(gridChoice->currentIndex() == changedGridIndex);

    REQUIRE(window.document().load(
      appController.environmentConfig(),
      mdl::QuakeGameInfo,
      mdl::MapFormat::Unknown,
      vm::bbox3d{8192.0},
      path));

    QApplication::processEvents();

    const auto loadedGridSize = window.document().map().grid().size();
    const auto loadedGridIndex = loadedGridSize - mdl::Grid::MinSize;

    CHECK(loadedGridSize != changedGridSize);
    CHECK(gridChoice->currentIndex() == loadedGridIndex);
    CHECK(gridChoice->currentData().toInt() == loadedGridSize);
  }

  SECTION("canReloadMaterialCollections returns false when resources need processing")
  {
    using TestResourceT = gl::Resource<TestResource>;

    // Verify initial state: no resources pending processing
    REQUIRE(!appController.glManager().resourceManager().needsProcessing());
    CHECK(window.canReloadMaterialCollections());

    // Add a resource that needs processing
    auto testResource = std::make_shared<TestResourceT>(
      []() { return Result<TestResource>{TestResource{}}; });
    appController.glManager().resourceManager().addResource(testResource);

    REQUIRE(appController.glManager().resourceManager().needsProcessing());
    CHECK(!window.canReloadMaterialCollections());

    // Process all resources synchronously
    auto testGl = gl::TestGl{};
    const auto processContext = gl::ProcessContext{testGl, [](auto, auto) {}};
    gl::processResourcesSync(appController.glManager().resourceManager(), processContext);

    REQUIRE(!appController.glManager().resourceManager().needsProcessing());
    CHECK(window.canReloadMaterialCollections());
  }

  SECTION("canReloadEntityDefinitions returns false when resources need processing")
  {
    using TestResourceT = gl::Resource<TestResource>;

    // Verify initial state: no resources pending processing
    REQUIRE(!appController.glManager().resourceManager().needsProcessing());
    CHECK(window.canReloadEntityDefinitions());

    // Add a resource that needs processing
    auto testResource = std::make_shared<TestResourceT>(
      []() { return Result<TestResource>{TestResource{}}; });
    appController.glManager().resourceManager().addResource(testResource);

    REQUIRE(appController.glManager().resourceManager().needsProcessing());
    CHECK(!window.canReloadEntityDefinitions());

    // Process all resources synchronously
    auto testGl = gl::TestGl{};
    const auto processContext = gl::ProcessContext{testGl, [](auto, auto) {}};
    gl::processResourcesSync(appController.glManager().resourceManager(), processContext);

    REQUIRE(!appController.glManager().resourceManager().needsProcessing());
    CHECK(window.canReloadEntityDefinitions());
  }

  SECTION("runs a minimal Python script with the active document")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "smoke.py",
      R"(
import tb

doc = tb.current_document()
assert doc is not None
assert len(doc.entities) >= 1
assert isinstance(doc.materials, list)
assert isinstance(doc.material_collections, list)
for material in doc.materials:
    assert isinstance(material.name, str)
    assert isinstance(material.collection_name, str)
    assert isinstance(material.width, int)
    assert isinstance(material.height, int)
for collection in doc.material_collections:
    assert isinstance(collection.name, str)
    assert isinstance(collection.materials, list)
print("python smoke ok")
with open("python-smoke-ok.txt", "w", encoding="utf-8") as f:
    f.write(doc.entities[0].classname)
)");

    const auto currentPathGuard = CurrentPathGuard{env.dir()};

    CHECK(PythonScripting::instance().runScript(window, env.dir() / "smoke.py"));
    CHECK(env.loadFile("python-smoke-ok.txt") == "worldspawn");
  }

  SECTION("runs a Python transaction against the active document")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "transaction.py",
      R"(
import tb

doc = tb.current_document()
with doc.transaction("python smoke transaction"):
    assert doc.selection.set_property("codex_python_smoke", "ok", True)
)"); // worldspawn is the default entity selection when no entity is explicitly selected.

    const auto currentPathGuard = CurrentPathGuard{env.dir()};

    CHECK(PythonScripting::instance().runScript(window, env.dir() / "transaction.py"));
    const auto* value =
      window.document().map().worldNode().entity().property("codex_python_smoke");
    REQUIRE(value != nullptr);
    CHECK(*value == "ok");
  }

  SECTION("runs a Python timer callback")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "timer.py",
      R"(
import tb

timer_id = None

def on_timer():
    global timer_id
    with open("python-timer-ok.txt", "w", encoding="utf-8") as f:
        f.write("ok")
    tb.clear_interval(timer_id)

timer_id = tb.set_interval(on_timer, 1)
)");

    const auto currentPathGuard = CurrentPathGuard{env.dir()};

    REQUIRE(PythonScripting::instance().runScript(window, env.dir() / "timer.py"));

    auto deadline = QDeadlineTimer{5000};
    while (!deadline.hasExpired()
           && !std::filesystem::exists(env.dir() / "python-timer-ok.txt"))
    {
      QApplication::processEvents(QEventLoop::AllEvents, 50);
      QThread::msleep(10);
    }

    CHECK(env.loadFile("python-timer-ok.txt") == "ok");
  }

  SECTION("runs Python selection_changed callbacks")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "selection_callback.py",
      R"(
import tb

count = 0

def on_selection_changed():
    global count
    count += 1
    with open("python-selection-callback-ok.txt", "w", encoding="utf-8") as f:
        f.write(str(count))

tb.register_callback("selection_changed", on_selection_changed)
tb.register_callback("selection_changed", on_selection_changed)
)");

    const auto currentPathGuard = CurrentPathGuard{env.dir()};

    REQUIRE(
      PythonScripting::instance().runScript(window, env.dir() / "selection_callback.py"));

    auto& map = window.document().map();
    auto* entityNode = new mdl::EntityNode{
      mdl::Entity{{{mdl::EntityPropertyKeys::Classname, "info_player_start"}}}};
    mdl::addNodes(map, {{map.worldNode().defaultLayer(), {entityNode}}});
    mdl::selectNodes(map, {entityNode});
    QApplication::processEvents();

    CHECK(env.loadFile("python-selection-callback-ok.txt") == "1");

    env.createFile(
      "unregister_selection_callback.py",
      R"(
import tb

tb.unregister_callback("selection_changed", on_selection_changed)
)");

    REQUIRE(PythonScripting::instance().runScript(
      window, env.dir() / "unregister_selection_callback.py"));

    mdl::deselectAll(map);
    QApplication::processEvents();

    CHECK(env.loadFile("python-selection-callback-ok.txt") == "1");
  }

  SECTION("executes Python action APIs against the active map window")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "actions.py",
      R"(
import tb

actions = tb.list_actions()
assert len(actions) > 0
assert any("Menu/" in action or "View/" in action for action in actions)

tb.execute_action("Menu/View/Grid/Set Grid Size 8")

try:
    tb.execute_action("Codex/Missing/Action")
except KeyError:
    with open("python-actions-ok.txt", "w", encoding="utf-8") as f:
        f.write("ok")
else:
    raise AssertionError("missing action did not raise KeyError")
)");

    const auto currentPathGuard = CurrentPathGuard{env.dir()};
    const auto initialGridSize = window.document().map().grid().size();
    REQUIRE(initialGridSize != 3);

    CHECK(PythonScripting::instance().runScript(window, env.dir() / "actions.py"));
    CHECK(env.loadFile("python-actions-ok.txt") == "ok");
    CHECK(window.document().map().grid().size() == 3);
    window.setGridSize(initialGridSize);
    CHECK(window.document().map().grid().size() == initialGridSize);
    CHECK_FALSE(window.document().map().modified());
  }

  SECTION("runs Python brush and face API smoke")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "brush_face.py",
      R"(
import tb

doc = tb.current_document()
brush = tb.create_brush([
    (-64, -64, -64),
    (-64, -64, 64),
    (-64, 64, -64),
    (-64, 64, 64),
    (64, -64, -64),
    (64, -64, 64),
    (64, 64, -64),
    (64, 64, 64),
])

assert brush is not None
assert len(doc.selection.brushes) == 1
assert len(doc.selection.brush_vertices()) == 1

faces = brush.faces()
assert len(faces) == 6
face = faces[0]
face.texture_name = "codex/test"
face.offset = (12.0, 24.0)
face.scale = (0.5, 0.25)
face.rotation = 45.0
face.surface_contents = 7
face.surface_flags = 11
face.surface_value = 3.5

assert face.texture_name == "codex/test"
assert face.offset == (12.0, 24.0)
assert face.scale == (0.5, 0.25)
assert face.rotation == 45.0
assert face.surface_contents == 7
assert face.surface_flags == 11
assert face.surface_value == 3.5
assert len(face.vertices) >= 3
assert face.normal is not None

with open("python-brush-face-ok.txt", "w", encoding="utf-8") as f:
    f.write(str(len(faces)))
)");

    const auto currentPathGuard = CurrentPathGuard{env.dir()};

    CHECK(PythonScripting::instance().runScript(window, env.dir() / "brush_face.py"));
    CHECK(env.loadFile("python-brush-face-ok.txt") == "6");

    const auto& selectedBrushes = window.document().map().selection().brushes;
    REQUIRE(selectedBrushes.size() == 1u);
    const auto& face = selectedBrushes.front()->brush().faces().front();
    CHECK(face.attributes().materialName() == "codex/test");
    CHECK(face.attributes().offset() == vm::vec2f{12.0f, 24.0f});
    CHECK(face.attributes().scale() == vm::vec2f{0.5f, 0.25f});
    CHECK(face.attributes().rotation() == 45.0f);
    CHECK(face.attributes().surfaceContents() == std::optional<int>{7});
    CHECK(face.attributes().surfaceFlags() == std::optional<int>{11});
    CHECK(face.attributes().surfaceValue() == std::optional<float>{3.5f});
  }

  SECTION("creates a plugin panel from a Python script")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "plugin_panel.py",
      R"(
import tb

panel = tb.create_plugin_panel("Codex Panel")
panel.add_label_named("status", "Ready")
panel.add_button("Run")
)");

    const auto currentPathGuard = CurrentPathGuard{env.dir()};

    REQUIRE(pluginPanels(window).empty());
    CHECK(PythonScripting::instance().runScript(window, env.dir() / "plugin_panel.py"));
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();

    const auto panels = pluginPanels(window);
    REQUIRE(panels.size() == 1u);

    auto* panel = panels.front();
    auto* label = panel->findChild<QLabel*>("tb_py_panel_label_status");
    const auto buttons = panel->findChildren<QPushButton*>();

    REQUIRE(label != nullptr);
    CHECK(label->text() == QStringLiteral("Ready"));
    CHECK(std::ranges::any_of(buttons, [](const auto* button) {
      return button->text() == QStringLiteral("Run");
    }));
  }

  SECTION("executes a configured pie menu action from the current map view")
  {
    setPref(Preferences::PieMenuAction, "Menu/View/Grid/Set Grid Size 8");

    auto* mapView = window.currentMapViewBase();
    REQUIRE(mapView != nullptr);

    const auto initialGridSize = window.document().map().grid().size();
    REQUIRE(initialGridSize != 3);
    REQUIRE_FALSE(window.document().map().modified());

    QCursor::setPos({300, 300});
    mapView->showPieMenu();
    QApplication::processEvents();

    auto* menu = findVisiblePieMenu();
    REQUIRE(menu != nullptr);
    REQUIRE(QTest::qWaitForWindowExposed(menu));

    const auto center = QPoint{menu->width() / 2, menu->height() / 2};
    sendMouseMove(*menu, center + QPoint{0, -80});
    sendMouseRelease(*menu, center + QPoint{0, -80});
    QApplication::processEvents();
    menu->setParent(nullptr);
    menu->deleteLater();
    QApplication::sendPostedEvents(menu, QEvent::DeferredDelete);

    CHECK(window.document().map().grid().size() == 3);
    window.setGridSize(initialGridSize);
    CHECK(window.document().map().grid().size() == initialGridSize);
    CHECK_FALSE(window.document().map().modified());
  }
}

} // namespace tb::ui
