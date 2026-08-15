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

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QStatusBar>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QWidget>
#include <QtTest/QTest>

#include "TestEnvironment.h"
#include "base/PreferenceManager.h"
#include "base/Result.h"
#include "fs/TestEnvironment.h"
#include "gl/GlManager.h"
#include "gl/Resource.h"
#include "gl/ResourceManager.h"
#include "gl/TestGl.h"
#include "gl/TestUtils.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
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
#include "prefs/Preferences.h"
#include "ui/ActionExecutionContext.h"
#include "ui/AppControllerFixture.h"
#include "ui/CatchConfig.h"
#include "ui/CommandPaletteDialog.h"
#include "ui/InfoPanel.h"
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
  setPref(Preferences::DefaultPluginPaths, "");
  setPref(Preferences::PythonPluginDirectories, "");

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

  SECTION("uses modern workbench surfaces and status controls")
  {
    auto* editorSurface = window.findChild<QWidget*>("MapWindow_EditorSurface");
    auto* infoPanelSurface = window.findChild<QWidget*>("MapWindow_InfoPanelSurface");
    auto* inspectorSurface = window.findChild<QWidget*>("MapWindow_InspectorSurface");
    REQUIRE(editorSurface != nullptr);
    REQUIRE(infoPanelSurface != nullptr);
    REQUIRE(inspectorSurface != nullptr);

    auto* gridChoice = window.findChild<QComboBox*>("MapWindow_GridChoice");
    auto* snapToggle = window.findChild<QToolButton*>("MapWindow_SnapToggle");
    REQUIRE(gridChoice != nullptr);
    REQUIRE(snapToggle != nullptr);
    CHECK(gridChoice->parentWidget() == window.statusBar());
    CHECK(snapToggle->parentWidget() == window.statusBar());
    CHECK(snapToggle->isChecked() == window.document().map().grid().snap());

    QTest::mouseClick(snapToggle, Qt::LeftButton);
    CHECK_FALSE(window.document().map().grid().snap());
    CHECK_FALSE(snapToggle->isChecked());

    const auto infoPanelWasHidden = infoPanelSurface->isHidden();
    window.toggleInfoPanel();
    CHECK(infoPanelSurface->isHidden() != infoPanelWasHidden);
    window.toggleInfoPanel();
    CHECK(infoPanelSurface->isHidden() == infoPanelWasHidden);

    const auto inspectorWasHidden = inspectorSurface->isHidden();
    window.toggleInspector();
    CHECK(inspectorSurface->isHidden() != inspectorWasHidden);
    window.toggleInspector();
    CHECK(inspectorSurface->isHidden() == inspectorWasHidden);
  }

  SECTION("switches to the supporting Assets surface")
  {
    auto* infoPanel = window.findChild<InfoPanel*>("MapWindow_InfoPanel");
    REQUIRE(infoPanel != nullptr);

    window.switchToInfoPanelPage(InfoPanelPage::Assets);

    CHECK(infoPanel->currentPage() == InfoPanelPage::Assets);
    CHECK_FALSE(window.findChild<QWidget*>("MapWindow_InfoPanelSurface")->isHidden());
    CHECK(window.findChild<QWidget*>("InfoPanel_Assets") != nullptr);
    CHECK(window.findChild<QWidget*>("ModelBrowser_Controls") != nullptr);
    CHECK(window.findChild<QWidget*>("ModelBrowser_FolderTree") != nullptr);
  }

  SECTION("uses a focused command palette structure")
  {
    auto context =
      ActionExecutionContext{appController, &window, window.currentMapViewBase()};
    auto dialog = CommandPaletteDialog{
      appController.actionManager(),
      context,
      std::filesystem::path{"Menu/View/Command Palette..."},
      &window};

    CHECK(dialog.objectName() == "CommandPalette_Dialog");
    auto* searchBox = dialog.findChild<QLineEdit*>("CommandPalette_SearchBox");
    auto* actionList = dialog.findChild<QListWidget*>("CommandPalette_ActionList");
    REQUIRE(searchBox != nullptr);
    REQUIRE(actionList != nullptr);
    CHECK(searchBox->placeholderText() == "Search commands...");
    CHECK_FALSE(actionList->alternatingRowColors());
    CHECK(actionList->selectionMode() == QAbstractItemView::SingleSelection);
    CHECK(actionList->count() > 0);
    CHECK(actionList->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff);
    CHECK(actionList->uniformItemSizes());
    CHECK(actionList->itemDelegate()->objectName() == "CommandPalette_ItemDelegate");

    const auto* firstItem = actionList->item(0);
    REQUIRE(firstItem != nullptr);
    CHECK_FALSE(firstItem->data(Qt::UserRole + 1).toString().isEmpty());
    CHECK_FALSE(firstItem->data(Qt::UserRole + 2).toString().isEmpty());
    CHECK_FALSE(firstItem->text().contains('\n'));

    auto itemOption = QStyleOptionViewItem{};
    itemOption.font = actionList->font();
    CHECK(
      actionList->itemDelegate()
        ->sizeHint(itemOption, actionList->model()->index(0, 0))
        .height()
      >= 46);
  }

  SECTION("copy prefixes worldspawn header when enabled")
  {
    auto& map = window.document().map();
    auto worldEntity = map.worldNode().entity();
    worldEntity.addOrUpdateProperty(mdl::EntityPropertyKeys::Wad, "textures/test.wad");
    map.worldNode().setEntity(std::move(worldEntity));

    const auto builder = mdl::BrushBuilder{
      map.worldNode().mapFormat(),
      map.worldBounds(),
      map.gameInfo().gameConfig.faceAttribsConfig.defaultUvAttributes,
      map.gameInfo().gameConfig.faceAttribsConfig.defaultSurfaceAttributes};
    auto* brushNode =
      new mdl::BrushNode{builder.createCube(64.0, "some_material") | kdl::value()};
    mdl::addNodes(map, {{map.worldNode().defaultLayer(), {brushNode}}});
    mdl::selectNodes(map, {brushNode});

    setPref(Preferences::PrefixWorldspawnHeaderOnCopy, true);
    window.copyToClipboard();

    CHECK(QApplication::clipboard()->text().contains(
      QStringLiteral("\"wad\" \"textures/test.wad\"")));
  }

  SECTION("load resets grid size dropdown")
  {
    auto* gridChoice = window.findChild<QComboBox*>("MapWindow_GridChoice");
    REQUIRE(gridChoice != nullptr);

    const auto changedGridSize = 5;
    const auto changedGridIndex = changedGridSize - mdl::Grid::MinSize;
    const auto path = getFixtureRoot() / "test/ui/MapDocument/emptyValveMap.map";

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
    auto* loadedGridChoice = window.findChild<QComboBox*>("MapWindow_GridChoice");
    REQUIRE(loadedGridChoice != nullptr);

    CHECK(loadedGridSize != changedGridSize);
    CHECK(loadedGridChoice->currentIndex() == loadedGridIndex);
    CHECK(loadedGridChoice->currentData().toInt() == loadedGridSize);
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
import tb2 as tb

doc = tb.current_document()
assert doc is not None
assert len(doc.entities) >= 1
assert isinstance(doc.materials, list)
for material in doc.materials:
    assert isinstance(material.name, str)
    assert isinstance(material.width, int)
    assert isinstance(material.height, int)
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
import tb2 as tb

doc = tb.current_document()
entity = doc.entities[0]
with doc.transaction("python smoke transaction"):
    entity.set("codex_python_smoke", "ok")
)");

    const auto currentPathGuard = CurrentPathGuard{env.dir()};

    CHECK(PythonScripting::instance().runScript(window, env.dir() / "transaction.py"));
    const auto* value =
      window.document().map().worldNode().entity().property("codex_python_smoke");
    REQUIRE(value != nullptr);
    CHECK(*value == "ok");
  }

  SECTION("runs Python selection_changed callbacks")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "selection_callback.py",
      R"(
import tb2 as tb

count = 0

def on_selection_changed():
    global count
    count += 1
    with open("python-selection-callback-ok.txt", "w", encoding="utf-8") as f:
        f.write(str(count))
    tb.unregister_callback(callback_token)

callback_token = tb.register_callback("selection_changed", on_selection_changed)
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
import tb2 as tb

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

  SECTION("creates a plugin panel from a Python script")
  {
    auto env = fs::TestEnvironment{};
    env.createFile(
      "plugin_panel.py",
      R"(
import tb2 as tb

panel = tb.create_plugin_panel("Codex Panel")
panel.add_label("Ready")
panel.add_button("Run", lambda: print("run"))
)");

    const auto currentPathGuard = CurrentPathGuard{env.dir()};

    REQUIRE(pluginPanels(window).empty());
    CHECK(PythonScripting::instance().runScript(window, env.dir() / "plugin_panel.py"));
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();

    const auto panels = pluginPanels(window);
    REQUIRE(panels.size() == 1u);

    auto* panel = panels.front();
    auto* label = static_cast<QLabel*>(nullptr);
    for (auto* candidate : panel->findChildren<QLabel*>())
    {
      if (candidate->text() == QStringLiteral("Ready"))
      {
        label = candidate;
      }
    }
    const auto buttons = panel->findChildren<QPushButton*>();

    REQUIRE(label != nullptr);
    CHECK(label->text() == QStringLiteral("Ready"));
    CHECK(std::ranges::any_of(buttons, [](const auto* button) {
      return button->text() == QStringLiteral("Run");
    }));
  }

  SECTION("reloads manifest plugins when plugin directory preferences change")
  {
    auto env = fs::TestEnvironment{};
    env.createDirectory("plugin");
    env.createFile(
      "plugin/trenchbroom-plugin.json",
      R"({
        "id": "codex.mapwindow.reload",
        "name": "Codex Reload",
        "version": "1.0.0",
        "apiVersion": 2,
        "pluginType": "ui",
        "entry": "main.py"
      })");
    env.createFile(
      "plugin/main.py",
      R"(
import tb2 as tb

panel = tb.create_plugin_panel("Reloaded Plugin")
panel.add_label("Loaded from preferences")
)");

    REQUIRE(pluginPanels(window).empty());

    setPref(Preferences::PythonPluginDirectories, (env.dir() / "plugin").string());
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();

    const auto panels = pluginPanels(window);
    REQUIRE(panels.size() == 1u);

    auto* label = static_cast<QLabel*>(nullptr);
    for (auto* candidate : panels.front()->findChildren<QLabel*>())
    {
      if (candidate->text() == QStringLiteral("Loaded from preferences"))
      {
        label = candidate;
      }
    }
    CHECK(label != nullptr);

    setPref(Preferences::PythonPluginDirectories, "");
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();

    CHECK(pluginPanels(window).empty());
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

    QTRY_VERIFY_WITH_TIMEOUT(findVisiblePieMenu() != nullptr, 1000);
    auto* menu = findVisiblePieMenu();
    REQUIRE(menu != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(menu->isVisible(), 1000);

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

  SECTION("updates checkable action state when preferences change")
  {
    auto* textureLockAction = window.findAction("Menu/Edit/Texture Lock");
    auto* uvLockAction = window.findAction("Menu/Edit/UV Lock");
    REQUIRE(textureLockAction != nullptr);
    REQUIRE(uvLockAction != nullptr);

    setPref(Preferences::AlignmentLock, true);
    setPref(Preferences::UvLock, true);
    QApplication::processEvents();
    CHECK(textureLockAction->isChecked());
    CHECK(uvLockAction->isChecked());

    setPref(Preferences::AlignmentLock, false);
    setPref(Preferences::UvLock, false);
    QApplication::processEvents();
    CHECK_FALSE(textureLockAction->isChecked());
    CHECK_FALSE(uvLockAction->isChecked());

    setPref(Preferences::AlignmentLock, true);
    setPref(Preferences::UvLock, false);
  }

  SECTION("opens the configured pie menu from the current map view shortcut")
  {
    setPref(Preferences::PieMenuAction, "Menu/View/Grid/Set Grid Size 8");

    auto* mapView = window.currentMapViewBase();
    REQUIRE(mapView != nullptr);

    QCursor::setPos({300, 300});
    auto event = QKeyEvent{QEvent::KeyPress, Qt::Key_QuoteLeft, Qt::NoModifier};
    QApplication::sendEvent(mapView, &event);

    REQUIRE(event.isAccepted());
    QTRY_VERIFY_WITH_TIMEOUT(findVisiblePieMenu() != nullptr, 1000);
    auto* menu = findVisiblePieMenu();
    REQUIRE(menu != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(menu->isVisible(), 1000);

    menu->close();
    menu->setParent(nullptr);
    menu->deleteLater();
    QApplication::sendPostedEvents(menu, QEvent::DeferredDelete);
  }
}

} // namespace tb::ui
