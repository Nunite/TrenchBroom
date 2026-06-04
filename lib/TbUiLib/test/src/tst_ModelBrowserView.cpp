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

#include "ui/ModelBrowserView.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

namespace tb::ui
{
using namespace Catch::Matchers;

namespace
{
std::vector<std::string> entryTitles(const std::vector<ModelBrowserEntry>& entries)
{
  auto result = std::vector<std::string>{};
  for (const auto& entry : entries)
  {
    result.push_back(entry.title);
  }
  return result;
}

std::vector<BrowserCellType> entryTypes(const std::vector<ModelBrowserEntry>& entries)
{
  auto result = std::vector<BrowserCellType>{};
  for (const auto& entry : entries)
  {
    result.push_back(entry.cellData.type);
  }
  return result;
}

std::vector<std::string> entryPaths(const std::vector<ModelBrowserEntry>& entries)
{
  auto result = std::vector<std::string>{};
  for (const auto& entry : entries)
  {
    result.push_back(entry.cellData.path.generic_string());
  }
  return result;
}
} // namespace

TEST_CASE("ModelBrowserView")
{
  const auto root = std::filesystem::path{"models"};
  const auto models = std::vector<std::filesystem::path>{
    "models/barrel.mdl",
    "models/crates/wood.mdl",
    "models/crates/metal.mdl",
    "models/monsters/ogre.mdl",
  };

  SECTION("shows top-level folders and models")
  {
    const auto entries = modelBrowserEntries(root, models, {}, {});

    CHECK_THAT(
      entryTypes(entries),
      Equals(std::vector<BrowserCellType>{
        BrowserCellType::Folder,
        BrowserCellType::Folder,
        BrowserCellType::Model,
      }));
    CHECK_THAT(
      entryTitles(entries),
      Equals(std::vector<std::string>{"crates", "monsters", "barrel.mdl"}));
    CHECK_THAT(
      entryPaths(entries),
      Equals(std::vector<std::string>{"crates", "monsters", "models/barrel.mdl"}));
  }

  SECTION("shows parent entry and children inside a folder")
  {
    const auto entries = modelBrowserEntries(root, models, "crates", {});

    CHECK_THAT(
      entryTypes(entries),
      Equals(std::vector<BrowserCellType>{
        BrowserCellType::Folder,
        BrowserCellType::Model,
        BrowserCellType::Model,
      }));
    CHECK_THAT(
      entryTitles(entries),
      Equals(std::vector<std::string>{"..", "metal.mdl", "wood.mdl"}));
    CHECK_THAT(
      entryPaths(entries),
      Equals(std::vector<std::string>{
        "", "models/crates/metal.mdl", "models/crates/wood.mdl"}));
  }

  SECTION("search includes matching nested models and their containing folders")
  {
    const auto entries = modelBrowserEntries(root, models, {}, "wood");

    CHECK_THAT(
      entryTypes(entries),
      Equals(
        std::vector<BrowserCellType>{BrowserCellType::Folder, BrowserCellType::Model}));
    CHECK_THAT(
      entryTitles(entries),
      Equals(std::vector<std::string>{"crates", "crates/wood.mdl"}));
    CHECK_THAT(
      entryPaths(entries),
      Equals(std::vector<std::string>{"crates", "models/crates/wood.mdl"}));
  }

  SECTION("search includes folders with matching names")
  {
    const auto entries = modelBrowserEntries(root, models, {}, "monster");

    CHECK_THAT(
      entryTypes(entries), Equals(std::vector<BrowserCellType>{BrowserCellType::Folder}));
    CHECK_THAT(entryTitles(entries), Equals(std::vector<std::string>{"monsters"}));
    CHECK_THAT(entryPaths(entries), Equals(std::vector<std::string>{"monsters"}));
  }
}

} // namespace tb::ui
