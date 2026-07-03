/*
 Copyright (C) 2026

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

#include "PreferenceManager.h"
#include "Preferences.h"
#include "fs/TestEnvironment.h"
#include "ui/PrefabAsset.h"
#include "ui/SystemPaths.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("PrefabAsset")
{
  SECTION("recognizes prefab asset paths")
  {
    CHECK(isPrefabAssetPath("prefabs/crate.tbprefab"));
    CHECK(isPrefabAssetPath("prefabs/crate.TBPREFAB"));
    CHECK_FALSE(isPrefabAssetPath("models/crate.mdl"));
  }

  SECTION("writes and reads prefab text")
  {
    auto env = fs::TestEnvironment{};
    const auto path = env.dir() / "prefabs/crate.tbprefab";
    const auto text = std::string{"// entity 0\n{\n\"classname\" \"info_null\"\n}\n"};

    REQUIRE(writePrefabAsset(path, text));
    const auto readResult = readPrefabAsset(path);

    REQUIRE(readResult);
    CHECK(readResult.value() == text);
  }

  SECTION("uses user data prefab directory by default")
  {
    CHECK(defaultPrefabDirectory() == SystemPaths::userDataDirectory() / "prefabs");
  }

  SECTION("uses configured prefab directory when set")
  {
    auto env = fs::TestEnvironment{};
    auto& prefs = PreferenceManager::instance();
    prefs.set(Preferences::PrefabDirectory, env.dir());

    CHECK(configuredPrefabDirectory() == env.dir());

    prefs.resetToDefault(Preferences::PrefabDirectory);
  }

  SECTION("builds prefab path from a name")
  {
    CHECK(
      prefabPathForName("prefabs", "crate")
      == std::filesystem::path{"prefabs/crate.tbprefab"});
    CHECK(
      prefabPathForName("prefabs", "crate.tbprefab")
      == std::filesystem::path{"prefabs/crate.tbprefab"});
  }

  SECTION("builds thumbnail path from a prefab path")
  {
    CHECK(
      prefabThumbnailPath("prefabs/crate.tbprefab")
      == std::filesystem::path{"prefabs/crate.png"});
  }

  SECTION("rejects duplicate prefab names")
  {
    auto env = fs::TestEnvironment{};
    const auto path = env.dir() / "crate.tbprefab";

    REQUIRE(writePrefabAsset(path, "prefab"));

    CHECK_FALSE(checkPrefabNameAvailable(env.dir(), "crate"));
    CHECK(checkPrefabNameAvailable(env.dir(), "new_crate"));
  }
}

} // namespace tb::ui
