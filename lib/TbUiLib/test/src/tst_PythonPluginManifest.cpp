#include "fs/TestEnvironment.h"
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
}

} // namespace tb::ui
