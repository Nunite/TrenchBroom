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

#pragma once

#include <QString>

#include "ui/Theme.h"

#include <filesystem>
#include <optional>
#include <vector>

class QPalette;

namespace tb::ui
{

enum class ThemeAppearance
{
  System,
  Light,
  Dark,
};

struct Theme
{
  QString id;
  QString name;
  QString author;
  ThemeAppearance appearance = ThemeAppearance::System;
  ThemeTokens tokens;
  bool builtIn = false;
  QString source;
};

struct ThemeDiagnostic
{
  QString source;
  QString message;
};

class ThemeRegistry
{
private:
  std::vector<Theme> m_themes;
  std::vector<ThemeDiagnostic> m_diagnostics;

public:
  explicit ThemeRegistry(
    const QPalette& systemPalette,
    std::optional<std::filesystem::path> userThemeDirectory = std::nullopt);

  static const ThemeRegistry& instance();

  const std::vector<Theme>& themes() const;
  const std::vector<ThemeDiagnostic>& diagnostics() const;

  /**
   * Returns the theme for a stable ID or a legacy built-in name.
   */
  const Theme* findTheme(const QString& idOrLegacyName) const;

  /**
   * Resolves a theme and falls back to builtin.system when it is unavailable.
   */
  const Theme& resolveTheme(const QString& idOrLegacyName) const;

  /**
   * Converts legacy built-in names and CLI aliases to stable IDs.
   * Unknown values are returned trimmed so temporarily missing themes remain selected.
   */
  QString canonicalThemeId(const QString& idOrLegacyName) const;
};

} // namespace tb::ui
