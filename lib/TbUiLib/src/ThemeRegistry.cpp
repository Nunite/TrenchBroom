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

#include "ui/ThemeRegistry.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPalette>
#include <QRegularExpression>
#include <QSet>

#include "ui/QPathUtils.h"
#include "ui/SystemPaths.h"

#include <algorithm>
#include <array>
#include <functional>
#include <utility>

static void initializeThemeResources()
{
  Q_INIT_RESOURCE(themes);
}

namespace tb::ui
{
namespace
{

constexpr auto MaxThemeFileSize = qint64{256 * 1024};
constexpr auto MaxUserThemeCount = 256;

struct ThemeTokenEntry
{
  const char* name;
  QColor ThemeTokens::* member;
};

const auto ThemeTokenEntries = std::array{
  ThemeTokenEntry{"windowBackground", &ThemeTokens::windowBackground},
  ThemeTokenEntry{"editorBackground", &ThemeTokens::editorBackground},
  ThemeTokenEntry{"sidebarBackground", &ThemeTokens::sidebarBackground},
  ThemeTokenEntry{"panelBackground", &ThemeTokens::panelBackground},
  ThemeTokenEntry{"elevatedBackground", &ThemeTokens::elevatedBackground},
  ThemeTokenEntry{"inputBackground", &ThemeTokens::inputBackground},
  ThemeTokenEntry{"alternateBackground", &ThemeTokens::alternateBackground},
  ThemeTokenEntry{"buttonBackground", &ThemeTokens::buttonBackground},
  ThemeTokenEntry{"hoverBackground", &ThemeTokens::hoverBackground},
  ThemeTokenEntry{"pressedBackground", &ThemeTokens::pressedBackground},
  ThemeTokenEntry{"selectionBackground", &ThemeTokens::selectionBackground},
  ThemeTokenEntry{
    "inactiveSelectionBackground", &ThemeTokens::inactiveSelectionBackground},
  ThemeTokenEntry{"border", &ThemeTokens::border},
  ThemeTokenEntry{"strongBorder", &ThemeTokens::strongBorder},
  ThemeTokenEntry{"focusBorder", &ThemeTokens::focusBorder},
  ThemeTokenEntry{"text", &ThemeTokens::text},
  ThemeTokenEntry{"secondaryText", &ThemeTokens::secondaryText},
  ThemeTokenEntry{"disabledText", &ThemeTokens::disabledText},
  ThemeTokenEntry{"inverseText", &ThemeTokens::inverseText},
  ThemeTokenEntry{"accent", &ThemeTokens::accent},
  ThemeTokenEntry{"error", &ThemeTokens::error},
  ThemeTokenEntry{"warning", &ThemeTokens::warning},
  ThemeTokenEntry{"success", &ThemeTokens::success},
};

struct ThemeDeclaration
{
  QString id;
  QString name;
  QString author;
  ThemeAppearance appearance = ThemeAppearance::System;
  QString inherits;
  QHash<QString, QColor> colors;
  bool builtIn = false;
  QString source;
};

void addDiagnostic(
  std::vector<ThemeDiagnostic>& diagnostics,
  const QString& source,
  const QString& message)
{
  diagnostics.push_back(ThemeDiagnostic{source, message});
}

std::optional<ThemeAppearance> parseAppearance(const QString& value)
{
  if (value == QStringLiteral("system"))
  {
    return ThemeAppearance::System;
  }
  if (value == QStringLiteral("light"))
  {
    return ThemeAppearance::Light;
  }
  if (value == QStringLiteral("dark"))
  {
    return ThemeAppearance::Dark;
  }
  return std::nullopt;
}

bool isKnownToken(const QString& name)
{
  for (const auto& entry : ThemeTokenEntries)
  {
    if (name == QString::fromLatin1(entry.name))
    {
      return true;
    }
  }
  return false;
}

std::optional<ThemeDeclaration> parseTheme(
  const QByteArray& contents,
  const QString& source,
  const bool builtIn,
  std::vector<ThemeDiagnostic>& diagnostics)
{
  const auto fail = [&](const QString& message) -> std::optional<ThemeDeclaration> {
    addDiagnostic(diagnostics, source, message);
    return std::nullopt;
  };

  auto parseError = QJsonParseError{};
  const auto document = QJsonDocument::fromJson(contents, &parseError);
  if (parseError.error != QJsonParseError::NoError)
  {
    return fail(QStringLiteral("Invalid JSON: %1").arg(parseError.errorString()));
  }
  if (!document.isObject())
  {
    return fail(QStringLiteral("The theme root must be a JSON object"));
  }

  const auto object = document.object();
  const auto allowedFields = QSet<QString>{
    QStringLiteral("schemaVersion"),
    QStringLiteral("id"),
    QStringLiteral("name"),
    QStringLiteral("author"),
    QStringLiteral("appearance"),
    QStringLiteral("inherits"),
    QStringLiteral("colors"),
  };
  for (const auto& field : object.keys())
  {
    if (!allowedFields.contains(field))
    {
      return fail(QStringLiteral("Unknown field: %1").arg(field));
    }
  }

  if (
    !object.value(QStringLiteral("schemaVersion")).isDouble()
    || object.value(QStringLiteral("schemaVersion")).toInt(-1) != 1)
  {
    return fail(QStringLiteral("schemaVersion must be 1"));
  }

  const auto requiredString = [&](const QString& field) -> std::optional<QString> {
    const auto value = object.value(field);
    if (!value.isString() || value.toString().trimmed().isEmpty())
    {
      addDiagnostic(
        diagnostics, source, QStringLiteral("%1 must be a non-empty string").arg(field));
      return std::nullopt;
    }
    return value.toString().trimmed();
  };

  const auto id = requiredString(QStringLiteral("id"));
  const auto name = requiredString(QStringLiteral("name"));
  const auto appearanceName = requiredString(QStringLiteral("appearance"));
  if (!id || !name || !appearanceName)
  {
    return std::nullopt;
  }

  const auto idExpression =
    QRegularExpression{QStringLiteral(R"(^[a-z0-9]+(?:[._-][a-z0-9]+)*$)")};
  if (id->size() > 128 || !idExpression.match(*id).hasMatch())
  {
    return fail(
      QStringLiteral("id must contain at most 128 lowercase letters, digits, dots, "
                     "dashes, or underscores"));
  }
  if (!builtIn && id->startsWith(QStringLiteral("builtin.")))
  {
    return fail(QStringLiteral("Theme IDs beginning with builtin. are reserved"));
  }
  if (name->size() > 80)
  {
    return fail(QStringLiteral("name must contain at most 80 characters"));
  }

  const auto appearance = parseAppearance(*appearanceName);
  if (!appearance)
  {
    return fail(QStringLiteral("appearance must be system, light, or dark"));
  }
  if (!builtIn && *appearance == ThemeAppearance::System)
  {
    return fail(
      QStringLiteral("Only the built-in System theme may use system appearance"));
  }

  auto declaration = ThemeDeclaration{};
  declaration.id = *id;
  declaration.name = *name;
  declaration.appearance = *appearance;
  declaration.builtIn = builtIn;
  declaration.source = source;

  if (const auto authorValue = object.value(QStringLiteral("author"));
      !authorValue.isUndefined())
  {
    if (!authorValue.isString() || authorValue.toString().trimmed().isEmpty())
    {
      return fail(QStringLiteral("author must be a non-empty string when present"));
    }
    declaration.author = authorValue.toString().trimmed();
    if (declaration.author.size() > 120)
    {
      return fail(QStringLiteral("author must contain at most 120 characters"));
    }
  }

  if (const auto inheritsValue = object.value(QStringLiteral("inherits"));
      !inheritsValue.isUndefined())
  {
    if (
      !inheritsValue.isString()
      || !idExpression.match(inheritsValue.toString().trimmed()).hasMatch())
    {
      return fail(QStringLiteral("inherits must be a valid theme ID when present"));
    }
    declaration.inherits = inheritsValue.toString().trimmed();
  }

  const auto colorsValue = object.value(QStringLiteral("colors"));
  if (!colorsValue.isObject())
  {
    return fail(QStringLiteral("colors must be a JSON object"));
  }

  const auto colorExpression = QRegularExpression{QStringLiteral(R"(^#[0-9a-fA-F]{6}$)")};
  const auto colors = colorsValue.toObject();
  for (const auto& colorName : colors.keys())
  {
    if (!isKnownToken(colorName))
    {
      return fail(QStringLiteral("Unknown color token: %1").arg(colorName));
    }

    const auto colorValue = colors.value(colorName);
    if (
      !colorValue.isString() || !colorExpression.match(colorValue.toString()).hasMatch())
    {
      return fail(
        QStringLiteral("Color token %1 must use #RRGGBB format").arg(colorName));
    }
    declaration.colors.insert(colorName, QColor{colorValue.toString()});
  }

  return declaration;
}

void loadThemeFile(
  const QString& path,
  const bool builtIn,
  std::vector<ThemeDeclaration>& declarations,
  std::vector<ThemeDiagnostic>& diagnostics)
{
  auto file = QFile{path};
  if (!file.open(QFile::ReadOnly))
  {
    addDiagnostic(diagnostics, path, file.errorString());
    return;
  }
  if (file.size() > MaxThemeFileSize)
  {
    addDiagnostic(diagnostics, path, QStringLiteral("Theme file exceeds 256 KiB"));
    return;
  }

  if (auto declaration = parseTheme(file.readAll(), path, builtIn, diagnostics))
  {
    declarations.push_back(std::move(*declaration));
  }
}

void applyColors(ThemeTokens& tokens, const QHash<QString, QColor>& colors)
{
  for (const auto& entry : ThemeTokenEntries)
  {
    const auto name = QString::fromLatin1(entry.name);
    if (const auto it = colors.constFind(name); it != colors.cend())
    {
      tokens.*(entry.member) = *it;
    }
  }
}

bool hasEveryColor(const QHash<QString, QColor>& colors)
{
  for (const auto& entry : ThemeTokenEntries)
  {
    if (!colors.contains(QString::fromLatin1(entry.name)))
    {
      return false;
    }
  }
  return true;
}

} // namespace

ThemeRegistry::ThemeRegistry(
  const QPalette& systemPalette,
  const std::optional<std::filesystem::path> userThemeDirectory)
{
  initializeThemeResources();

  auto declarations = std::vector<ThemeDeclaration>{};
  const auto builtInDirectory = QDir{QStringLiteral(":/themes")};
  const auto builtInFiles = builtInDirectory.entryList(
    QStringList{QStringLiteral("*.tbtheme")}, QDir::Files, QDir::Name);
  for (const auto& file : builtInFiles)
  {
    loadThemeFile(builtInDirectory.filePath(file), true, declarations, m_diagnostics);
  }

  if (userThemeDirectory)
  {
    const auto directoryPath = pathAsQString(*userThemeDirectory);
    const auto directory = QDir{directoryPath};
    if (directory.exists())
    {
      const auto files = directory.entryList(
        QStringList{QStringLiteral("*.tbtheme")},
        QDir::Files | QDir::Readable,
        QDir::Name);
      if (files.size() > MaxUserThemeCount)
      {
        addDiagnostic(
          m_diagnostics,
          directoryPath,
          QStringLiteral("Only the first %1 theme files are loaded")
            .arg(MaxUserThemeCount));
      }
      for (auto i = qsizetype{0};
           i < std::min(files.size(), qsizetype{MaxUserThemeCount});
           ++i)
      {
        loadThemeFile(directory.filePath(files[i]), false, declarations, m_diagnostics);
      }
    }
  }

  auto declarationIndices = QHash<QString, int>{};
  auto uniqueDeclarations = std::vector<ThemeDeclaration>{};
  uniqueDeclarations.reserve(declarations.size());
  for (auto& declaration : declarations)
  {
    if (declarationIndices.contains(declaration.id))
    {
      addDiagnostic(
        m_diagnostics,
        declaration.source,
        QStringLiteral("Duplicate theme ID ignored: %1").arg(declaration.id));
      continue;
    }
    declarationIndices.insert(declaration.id, int(uniqueDeclarations.size()));
    uniqueDeclarations.push_back(std::move(declaration));
  }

  auto states = std::vector<int>(uniqueDeclarations.size(), 0);
  auto resolved = std::vector<std::optional<Theme>>(uniqueDeclarations.size());
  auto resolve = std::function<std::optional<Theme>(int)>{};
  resolve = [&](const int index) -> std::optional<Theme> {
    if (states[index] == 2)
    {
      return resolved[index];
    }

    const auto& declaration = uniqueDeclarations[index];
    if (states[index] == 1)
    {
      addDiagnostic(
        m_diagnostics,
        declaration.source,
        QStringLiteral("Cyclic theme inheritance involving %1").arg(declaration.id));
      return std::nullopt;
    }

    states[index] = 1;
    auto tokens = ThemeTokens{};
    if (!declaration.inherits.isEmpty())
    {
      const auto baseIt = declarationIndices.constFind(declaration.inherits);
      if (baseIt == declarationIndices.cend())
      {
        addDiagnostic(
          m_diagnostics,
          declaration.source,
          QStringLiteral("Inherited theme not found: %1").arg(declaration.inherits));
        states[index] = 2;
        return std::nullopt;
      }

      const auto base = resolve(*baseIt);
      if (!base)
      {
        addDiagnostic(
          m_diagnostics,
          declaration.source,
          QStringLiteral("Inherited theme could not be resolved: %1")
            .arg(declaration.inherits));
        states[index] = 2;
        return std::nullopt;
      }
      tokens = base->tokens;
    }
    else if (declaration.appearance == ThemeAppearance::System)
    {
      tokens = makeSystemThemeTokens(systemPalette);
    }
    else if (!hasEveryColor(declaration.colors))
    {
      addDiagnostic(
        m_diagnostics,
        declaration.source,
        QStringLiteral("A theme without inherits must define every color token"));
      states[index] = 2;
      return std::nullopt;
    }

    applyColors(tokens, declaration.colors);
    resolved[index] = Theme{
      declaration.id,
      declaration.name,
      declaration.author,
      declaration.appearance,
      tokens,
      declaration.builtIn,
      declaration.source};
    states[index] = 2;
    return resolved[index];
  };

  for (auto i = 0; i < int(uniqueDeclarations.size()); ++i)
  {
    if (auto theme = resolve(i))
    {
      m_themes.push_back(std::move(*theme));
    }
  }

  if (findTheme(QStringLiteral("builtin.system")) == nullptr)
  {
    addDiagnostic(
      m_diagnostics,
      QStringLiteral(":/themes"),
      QStringLiteral("Built-in System theme was unavailable; using generated fallback"));
    m_themes.insert(
      m_themes.begin(),
      Theme{
        QStringLiteral("builtin.system"),
        QStringLiteral("System"),
        QStringLiteral("TrenchBroom"),
        ThemeAppearance::System,
        makeSystemThemeTokens(systemPalette),
        true,
        QStringLiteral("<generated>")});
  }
}

const ThemeRegistry& ThemeRegistry::instance()
{
  static const auto registry = ThemeRegistry{
    QGuiApplication::palette(), SystemPaths::userDataDirectory() / "themes"};
  return registry;
}

const std::vector<Theme>& ThemeRegistry::themes() const
{
  return m_themes;
}

const std::vector<ThemeDiagnostic>& ThemeRegistry::diagnostics() const
{
  return m_diagnostics;
}

const Theme* ThemeRegistry::findTheme(const QString& idOrLegacyName) const
{
  const auto id = canonicalThemeId(idOrLegacyName);
  for (const auto& theme : m_themes)
  {
    if (theme.id == id)
    {
      return &theme;
    }
  }
  return nullptr;
}

const Theme& ThemeRegistry::resolveTheme(const QString& idOrLegacyName) const
{
  if (const auto* theme = findTheme(idOrLegacyName))
  {
    return *theme;
  }
  if (const auto* systemTheme = findTheme(QStringLiteral("builtin.system")))
  {
    return *systemTheme;
  }
  return m_themes.front();
}

QString ThemeRegistry::canonicalThemeId(const QString& idOrLegacyName) const
{
  const auto value = idOrLegacyName.trimmed();
  if (
    value.compare(QStringLiteral("system"), Qt::CaseInsensitive) == 0
    || value == QStringLiteral("builtin.system"))
  {
    return QStringLiteral("builtin.system");
  }
  if (
    value.compare(QStringLiteral("light"), Qt::CaseInsensitive) == 0
    || value == QStringLiteral("builtin.light"))
  {
    return QStringLiteral("builtin.light");
  }
  if (
    value.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0
    || value == QStringLiteral("builtin.dark"))
  {
    return QStringLiteral("builtin.dark");
  }
  if (
    value.compare(QStringLiteral("blender"), Qt::CaseInsensitive) == 0
    || value == QStringLiteral("builtin.blender"))
  {
    return QStringLiteral("builtin.blender");
  }
  return value;
}

} // namespace tb::ui
