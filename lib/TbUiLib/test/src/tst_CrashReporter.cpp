/*
 Copyright (C) 2026 XiangXtreme

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

#include "ui/CrashReporter.h"

#include <filesystem>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

TEST_CASE("CrashReporter")
{
  SECTION("makeCrashReportBasePath uses saved map directory")
  {
    const auto pathExists = [](const auto& path) {
      return path.filename() == "test-crash.txt";
    };

    CHECK(
      makeCrashReportBasePath("C:/maps/test.map", "C:/docs", pathExists)
      == std::filesystem::path{"C:/maps/test-crash-1"});
  }

  SECTION("makeCrashReportBasePath uses documents directory without saved map")
  {
    const auto pathExists = [](const auto&) { return false; };

    CHECK(
      makeCrashReportBasePath("", "C:/docs", pathExists)
      == std::filesystem::path{"C:/docs/trenchbroom-crash"});
  }

  SECTION("crashReportArtifactPath adds extension")
  {
    CHECK(
      crashReportArtifactPath("C:/docs/trenchbroom-crash", ".dmp")
      == std::filesystem::path{"C:/docs/trenchbroom-crash.dmp"});
  }
}

} // namespace
} // namespace tb::ui
