#include "fs/TestEnvironment.h"
#include "ui/python/PythonPluginManager.h"
#include "ui/python/PythonPluginManifest.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("PythonPluginManifest")
{
  auto env = fs::TestEnvironment{};

  SECTION("loads a valid manifest")
  {
    env.createDirectory("plugin");
    env.createFile("plugin/main.py", "print('ok')\n");
    env.createFile(
      "plugin/trenchbroom-plugin.json",
      R"({
        "id": "codex.test",
        "name": "Codex Test",
        "version": "1.0.0",
        "apiVersion": 2,
        "entry": "main.py",
        "description": "Test plugin",
        "author": "Codex"
      })");

    auto result = loadPythonPluginManifest(env.dir() / "plugin");
    REQUIRE(result.manifest.has_value());
    CHECK(result.manifest->id == "codex.test");
    CHECK(result.manifest->entry == std::filesystem::path{"main.py"});
    CHECK_FALSE(result.error.has_value());
  }

  SECTION("reports missing required fields")
  {
    env.createDirectory("missing");
    env.createFile("missing/trenchbroom-plugin.json", R"({"id": "codex.missing"})");

    auto result = loadPythonPluginManifest(env.dir() / "missing");
    CHECK_FALSE(result.manifest.has_value());
    REQUIRE(result.error.has_value());
    CHECK(result.error->message.find("name") != std::string::npos);
  }

  SECTION("splits and joins plugin directories")
  {
    const auto paths = splitPythonPluginDirectories("C:/a|D:/b");
    REQUIRE(paths.size() == 2u);
    CHECK(joinPythonPluginDirectories(paths).find('|') != std::string::npos);
  }

  SECTION("manager discovers plugin manifests in child directories")
  {
    env.createDirectory("plugins/one");
    env.createFile("plugins/one/main.py", "print('one')\n");
    env.createFile(
      "plugins/one/trenchbroom-plugin.json",
      R"({
        "id": "codex.one",
        "name": "One",
        "version": "1.0.0",
        "apiVersion": 2,
        "entry": "main.py"
      })");

    env.createDirectory("plugins/two");
    env.createFile("plugins/two/main.py", "print('two')\n");
    env.createFile(
      "plugins/two/trenchbroom-plugin.json",
      R"({
        "id": "codex.two",
        "name": "Two",
        "version": "1.0.0",
        "apiVersion": 2,
        "entry": "main.py"
      })");

    auto manager = PythonPluginManager{};
    manager.reload({env.dir() / "plugins"});

    REQUIRE(manager.plugins().size() == 2u);
    CHECK(manager.plugins()[0].manifest.id == "codex.one");
    CHECK(manager.plugins()[1].manifest.id == "codex.two");
    CHECK(manager.errors().empty());
  }
}

} // namespace tb::ui
