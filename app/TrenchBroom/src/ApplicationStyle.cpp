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

#include "ApplicationStyle.h"

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QProxyStyle>
#include <QStyle>
#include <QStyleHints>
#include <QStyleOption>
#include <QTextStream>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/QPathUtils.h"
#include "ui/SystemPaths.h"
#include "ui/Theme.h"
#include "ui/ThemeRegistry.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace tb::ui
{
namespace
{

QColor blendColor(const QColor& background, const QColor& foreground, const double amount)
{
  const auto channel = [amount](const int backgroundValue, const int foregroundValue) {
    return int(std::lround(
      double(backgroundValue) * (1.0 - amount) + double(foregroundValue) * amount));
  };

  return {
    channel(background.red(), foreground.red()),
    channel(background.green(), foreground.green()),
    channel(background.blue(), foreground.blue())};
}

class TrenchBroomProxyStyle : public QProxyStyle
{
private:
  ThemeTokens m_themeTokens;

  void drawCheckBoxIndicator(const QStyleOption* option, QPainter* painter) const
  {
    const auto enabled = option->state.testFlag(QStyle::State_Enabled);
    const auto hovered = option->state.testFlag(QStyle::State_MouseOver);
    const auto pressed = option->state.testFlag(QStyle::State_Sunken);
    const auto focused = option->state.testFlag(QStyle::State_HasFocus);
    const auto selected = option->state.testFlag(QStyle::State_Selected);
    const auto checked = option->state.testFlag(QStyle::State_On);
    const auto mixed = option->state.testFlag(QStyle::State_NoChange);

    const auto side = std::min(option->rect.width(), option->rect.height());
    // QRect::center() rounds even-sized rectangles toward the top-left, which puts
    // half of the 1 px border outside the indicator's paint clip.
    const auto center = QRectF{option->rect}.center();
    const auto indicatorRect = QRectF{
      center.x() - side / 2.0 + 0.5,
      center.y() - side / 2.0 + 0.5,
      side - 1.0,
      side - 1.0};
    const auto scale = side / 18.0;

    const auto background = !enabled  ? m_themeTokens.windowBackground
                            : pressed ? m_themeTokens.pressedBackground
                            : hovered ? m_themeTokens.hoverBackground
                                      : m_themeTokens.inputBackground;
    const auto border =
      !enabled ? m_themeTokens.disabledText
      : focused
        ? m_themeTokens.focusBorder
        : blendColor(background, m_themeTokens.text, hovered || pressed ? 0.60 : 0.45);
    const auto foreground = !enabled   ? m_themeTokens.disabledText
                            : selected ? m_themeTokens.inverseText
                                       : m_themeTokens.text;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen{border, 1.0});
    painter->setBrush(background);
    painter->drawRoundedRect(indicatorRect, 3.0 * scale, 3.0 * scale);

    if (checked || mixed)
    {
      auto mark = QPainterPath{};
      if (mixed)
      {
        mark.moveTo(indicatorRect.left() + 4.0 * scale, indicatorRect.center().y());
        mark.lineTo(indicatorRect.right() - 4.0 * scale, indicatorRect.center().y());
      }
      else
      {
        mark.moveTo(
          indicatorRect.left() + 4.0 * scale, indicatorRect.top() + 9.0 * scale);
        mark.lineTo(
          indicatorRect.left() + 7.0 * scale, indicatorRect.top() + 12.0 * scale);
        mark.lineTo(
          indicatorRect.left() + 14.0 * scale, indicatorRect.top() + 5.0 * scale);
      }

      auto markPen = QPen{foreground, std::max(1.5, 2.0 * scale)};
      markPen.setCapStyle(Qt::RoundCap);
      markPen.setJoinStyle(Qt::RoundJoin);
      painter->setPen(markPen);
      painter->setBrush(Qt::NoBrush);
      painter->drawPath(mark);
    }
    painter->restore();
  }

public:
  TrenchBroomProxyStyle(const QString& key, const ThemeTokens& themeTokens)
    : QProxyStyle{key}
    , m_themeTokens{themeTokens}
  {
  }

  explicit TrenchBroomProxyStyle(const ThemeTokens& themeTokens, QStyle* style = nullptr)
    : QProxyStyle{style}
    , m_themeTokens{themeTokens}
  {
  }

  void setThemeTokens(const ThemeTokens& themeTokens) { m_themeTokens = themeTokens; }

  void drawPrimitive(
    const PrimitiveElement element,
    const QStyleOption* option,
    QPainter* painter,
    const QWidget* widget = nullptr) const override
  {
    if (
      element == QStyle::PE_IndicatorCheckBox
      || element == QStyle::PE_IndicatorItemViewItemCheck)
    {
      drawCheckBoxIndicator(option, painter);
      return;
    }

    QProxyStyle::drawPrimitive(element, option, painter, widget);
  }

  int styleHint(
    StyleHint hint,
    const QStyleOption* option = nullptr,
    const QWidget* widget = nullptr,
    QStyleHintReturn* returnData = nullptr) const override
  {
    return hint == QStyle::SH_MenuBar_AltKeyNavigation
             ? 0
             : QProxyStyle::styleHint(hint, option, widget, returnData);
  }

  int pixelMetric(
    PixelMetric metric,
    const QStyleOption* option = nullptr,
    const QWidget* widget = nullptr) const override
  {
    switch (metric)
    {
    case QStyle::PM_IndicatorWidth:
    case QStyle::PM_IndicatorHeight:
      return 18;
    case QStyle::PM_SmallIconSize:
    case QStyle::PM_ButtonIconSize:
    case QStyle::PM_TabBarIconSize:
      return 16;
    case QStyle::PM_ToolBarIconSize:
      return 20;
    case QStyle::PM_ToolBarItemSpacing:
    case QStyle::PM_ToolBarItemMargin:
      return 2;
    case QStyle::PM_FocusFrameHMargin:
    case QStyle::PM_FocusFrameVMargin:
      return 1;
    default:
      return QProxyStyle::pixelMetric(metric, option, widget);
    }
  }
};

struct ApplicationStyleState
{
  QApplication* app = nullptr;
  QString systemStyleName;
  QPalette systemPalette;
  Qt::ColorScheme systemColorScheme = Qt::ColorScheme::Unknown;
  QString activeThemeId;
};

ApplicationStyleState& applicationStyleState()
{
  static auto state = ApplicationStyleState{};
  return state;
}

void initializeApplicationStyleState(QApplication& app)
{
  auto& state = applicationStyleState();
  if (state.app == &app)
  {
    return;
  }

  state = ApplicationStyleState{
    &app,
    app.style()->name(),
    app.palette(),
    app.styleHints()->colorScheme(),
    {}};
}

ThemeTokens themeTokens(
  const Theme& theme, const ApplicationStyleState& state)
{
  return theme.appearance == ThemeAppearance::System
           ? makeSystemThemeTokens(state.systemPalette)
           : theme.tokens;
}

std::optional<QString> expandedApplicationStyleSheet(
  const ThemeTokens& themeTokens, QString* error)
{
  const auto path = SystemPaths::findResourceFile("stylesheets/base.qss");
  auto file = QFile{pathAsQPath(path)};
  if (!file.exists())
  {
    if (error != nullptr)
    {
      *error = QStringLiteral("Could not find stylesheet: %1").arg(pathAsQString(path));
    }
    return std::nullopt;
  }

  if (!file.open(QFile::ReadOnly | QFile::Text))
  {
    if (error != nullptr)
    {
      *error = file.errorString();
    }
    return std::nullopt;
  }

  auto styleSheet = QTextStream{&file}.readAll();
  if (!expandThemeStyleSheet(styleSheet, themeTokens, error))
  {
    return std::nullopt;
  }

  return styleSheet;
}

bool applyResolvedTheme(
  QApplication& app, const Theme& theme, const QString& themeId, QString* error)
{
  initializeApplicationStyleState(app);
  auto& state = applicationStyleState();
  const auto tokens = themeTokens(theme, state);
  const auto styleSheet = expandedApplicationStyleSheet(tokens, error);
  if (!styleSheet)
  {
    return false;
  }

  // Alt is part of TrenchBroom's fundamental fly-navigation controls. The proxy style
  // prevents an Alt press and release from unexpectedly focusing the menu bar.
  if (theme.appearance == ThemeAppearance::System)
  {
    app.setStyle(new TrenchBroomProxyStyle{state.systemStyleName, tokens});
    app.setPalette(state.systemPalette);
    app.styleHints()->setColorScheme(state.systemColorScheme);
  }
  else
  {
    // Explicit themes use Fusion for deterministic cross-platform rendering.
    app.setStyle(new TrenchBroomProxyStyle{"Fusion", tokens});
    app.setPalette(makeThemePalette(tokens));
    app.styleHints()->setColorScheme(
      theme.appearance == ThemeAppearance::Dark ? Qt::ColorScheme::Dark
                                                : Qt::ColorScheme::Light);
  }

  QPixmapCache::clear();
  app.setStyleSheet(*styleSheet);
  state.activeThemeId = themeId;
  if (error != nullptr)
  {
    error->clear();
  }
  return true;
}

} // namespace

bool installApplicationStyle(
  QApplication& app, const std::optional<QString>& themeOverride, QString* error)
{
  initializeApplicationStyleState(app);

  const auto& registry = ThemeRegistry::instance();
  for (const auto& diagnostic : registry.diagnostics())
  {
    qWarning().noquote() << "Theme" << diagnostic.source << diagnostic.message;
  }

  const auto storedTheme = QString::fromStdString(pref(Preferences::Theme));
  const auto requestedTheme = themeOverride.value_or(storedTheme);
  const auto canonicalTheme = registry.canonicalThemeId(requestedTheme);
  const auto* selectedTheme = registry.findTheme(canonicalTheme);
  if (selectedTheme == nullptr)
  {
    qWarning().noquote() << "Theme unavailable, falling back to builtin.system:"
                         << requestedTheme;
  }
  const auto& theme = registry.resolveTheme(canonicalTheme);
  if (!applyResolvedTheme(app, theme, theme.id, error))
  {
    return false;
  }

  if (!themeOverride && canonicalTheme != storedTheme && selectedTheme != nullptr)
  {
    setPref(Preferences::Theme, canonicalTheme.toStdString());
  }
  return true;
}

bool applyApplicationTheme(QApplication& app, const QString& themeId, QString* error)
{
  initializeApplicationStyleState(app);
  const auto& registry = ThemeRegistry::instance();
  const auto canonicalTheme = registry.canonicalThemeId(themeId);
  const auto* theme = registry.findTheme(canonicalTheme);
  if (theme == nullptr)
  {
    if (error != nullptr)
    {
      *error = QStringLiteral("Unknown theme: %1").arg(themeId);
    }
    return false;
  }

  if (applicationStyleState().activeThemeId == canonicalTheme)
  {
    if (error != nullptr)
    {
      error->clear();
    }
    return true;
  }

  return applyResolvedTheme(app, *theme, canonicalTheme, error);
}

QString activeApplicationThemeId()
{
  return applicationStyleState().activeThemeId;
}

} // namespace tb::ui
