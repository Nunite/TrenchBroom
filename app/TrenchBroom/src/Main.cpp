/*
 Copyright (C) 2010 Kristian Duske

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

#include <QAbstractButton>
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QEvent>
#include <QFile>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QProxyStyle>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QString>
#include <QStyleHints>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QTranslator>
#include <QTreeWidget>
#include <QtGlobal>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/Action.h"
#include "ui/ActionBuilder.h"
#include "ui/ActionExecutionContext.h"
#include "ui/AppController.h"
#include "ui/Contracts.h"
#include "ui/CrashReporter.h"
#include "ui/FileEventFilter.h"
#include "ui/InfoPanel.h"
#include "ui/Inspector.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/PreferenceDialog.h"
#include "ui/QPathUtils.h"
#include "ui/QPreferenceStore.h"
#include "ui/RecentDocuments.h"
#include "ui/SystemPaths.h"
#include "ui/Theme.h"
#include "ui/UiSnapshot.h"
#include "ui/WelcomeWindow.h"

#include <memory>
#include <optional>

using namespace tb;
using namespace tb::ui;

static_assert(
  QT_VERSION >= QT_VERSION_CHECK(6, 8, 0), "TrenchBroom requires Qt 6.8.0 or later");

extern void qt_set_sequence_auto_mnemonic(bool b);

namespace
{

struct UiSnapshotCommandLineOptions
{
  QString outputPath;
  QString theme;
  QString page;
};

struct CommandLineOptions
{
  bool enableDraftReleaseUpdates = false;
  QStringList fileNames;
  std::optional<UiSnapshotCommandLineOptions> uiSnapshot;
};

bool loadStyleSheets(const ThemeTokens& themeTokens, QString* error = nullptr)
{
  const auto path = SystemPaths::findResourceFile("stylesheets/base.qss");
  if (auto file = QFile{pathAsQPath(path)}; file.exists())
  {
    // closed automatically by destructor
    if (!file.open(QFile::ReadOnly | QFile::Text))
    {
      if (error != nullptr)
      {
        *error = file.errorString();
      }
      return false;
    }
    auto styleSheet = QTextStream{&file}.readAll();
    if (!expandThemeStyleSheet(styleSheet, themeTokens, error))
    {
      return false;
    }

    qApp->setStyleSheet(styleSheet);
    if (error != nullptr)
    {
      error->clear();
    }
    return true;
  }

  if (error != nullptr)
  {
    *error = QStringLiteral("Could not find stylesheet: %1").arg(pathAsQString(path));
  }
  return false;
}

void loadTranslations(QApplication& app)
{
  if (pref(Preferences::Language) != Preferences::languageChinese())
  {
    return;
  }

  static auto trenchBroomTranslator = QTranslator{};
  if (trenchBroomTranslator.load(":/translations/trenchbroom_zh_CN"))
  {
    app.installTranslator(&trenchBroomTranslator);
  }
}

ThemeTokens loadStyle(
  QApplication& app, const std::optional<QString>& themeOverride = std::nullopt)
{
  // We can't use auto mnemonics in TrenchBroom. e.g. by default with Qt, Alt+D opens
  // the "Debug" menu, Alt+S activates the "Show default properties" checkbox in the
  // entity inspector. Flying with Alt held down and pressing WASD is a fundamental
  // behaviour in TB, so we can't have shortcuts randomly activating.
  //
  // Previously were calling `qt_set_sequence_auto_mnemonic(false);` in main(), but it
  // turns out we also need to suppress an Alt press followed by release from focusing
  // the menu bar (https://github.com/TrenchBroom/TrenchBroom/issues/3140), so the
  // following QProxyStyle disables that completely.

  class TrenchBroomProxyStyle : public QProxyStyle
  {
  public:
    explicit TrenchBroomProxyStyle(const QString& key)
      : QProxyStyle{key}
    {
    }

    explicit TrenchBroomProxyStyle(QStyle* style = nullptr)
      : QProxyStyle{style}
    {
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

  const auto useDarkTheme =
    (themeOverride && *themeOverride == QStringLiteral("dark"))
    || (!themeOverride && pref(Preferences::Theme) == Preferences::DarkTheme);
  const auto useLightTheme = themeOverride && *themeOverride == QStringLiteral("light");

  // Explicit themes use Fusion for deterministic cross-platform rendering.
  if (useDarkTheme)
  {
    app.setStyle(new TrenchBroomProxyStyle{"Fusion"});
    const auto themeTokens = makeDarkThemeTokens();
    app.setPalette(makeThemePalette(themeTokens));
    app.styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    return themeTokens;
  }

  if (useLightTheme)
  {
    app.setStyle(new TrenchBroomProxyStyle{"Fusion"});
    const auto themeTokens = makeLightThemeTokens();
    app.setPalette(makeThemePalette(themeTokens));
    app.styleHints()->setColorScheme(Qt::ColorScheme::Light);
    return themeTokens;
  }

  app.setStyle(new TrenchBroomProxyStyle{});
  return makeSystemThemeTokens(app.palette());
}

auto createAppController(const bool snapshotMode = false)
{
  const auto options = AppControllerOptions{!snapshotMode, !snapshotMode};
  return AppController::create(options) | kdl::if_error([](auto e) {
           const auto msg =
             fmt::format(R"(Game configurations could not be loaded: {})", e.msg);

           QMessageBox::critical(
             nullptr, "TrenchBroom", QString::fromStdString(msg), QMessageBox::Ok);
           QCoreApplication::exit(1);
         })
         | kdl::value();
}


[[maybe_unused]] void populateMainMenu(AppController& appController)
{
  auto* menuBar = new QMenuBar{};
  auto actionMap = std::unordered_map<const Action*, QAction*>{};

  auto menuBuilderResult = populateMenuBar(
    appController.actionManager(), *menuBar, actionMap, [&](const Action& action) {
      auto context = ActionExecutionContext{appController, nullptr, nullptr};
      action.execute(context);
    });

  appController.recentDocuments().addMenu(*menuBuilderResult.recentDocumentsMenu);

  auto context = ActionExecutionContext{appController, nullptr, nullptr};
  for (auto [tbAction, qtAction] : actionMap)
  {
    qtAction->setEnabled(tbAction->enabled(context));
    if (qtAction->isCheckable())
    {
      qtAction->setChecked(tbAction->checked(context));
    }
  }
}

[[maybe_unused]] void installFileEventFilter(AppController& appController)
{
  qApp->installEventFilter(new FileEventFilter{appController, qApp});
}

bool openFiles(AppController& appController, const QStringList& fileNames)
{
  const auto filesToOpen = AppController::useSDI && !fileNames.empty()
                             ? QStringList{fileNames.front()}
                             : fileNames;

  auto anyDocumentOpened = false;
  for (const auto& fileName : filesToOpen)
  {
    const auto path = ui::pathFromQString(fileName);
    if (!path.empty() && appController.openDocument(path))
    {
      anyDocumentOpened = true;
    }
  }

  return anyDocumentOpened;
}

std::optional<CommandLineOptions> parseCommandLine(QApplication& app)
{
  auto parser = QCommandLineParser{};
  parser.setApplicationDescription("TrenchBroom level editor");
  parser.addHelpOption();

  const auto portableOption = QCommandLineOption{
    QStringList{"portable"}, "Store application settings next to the executable."};
  const auto draftUpdatesOption = QCommandLineOption{
    QStringList{"enableDraftReleaseUpdates"}, "Enable draft release updates."};
  const auto uiSnapshotOption = QCommandLineOption{
    QStringList{"ui-snapshot"},
    "Capture a deterministic application surface and exit.",
    "path"};
  const auto uiSnapshotThemeOption = QCommandLineOption{
    QStringList{"ui-snapshot-theme"},
    "Override the snapshot theme with system, light, or dark.",
    "theme",
    "system"};
  const auto uiSnapshotPageOption = QCommandLineOption{
    QStringList{"ui-snapshot-page"},
    "Select the surface to capture: map, outliner, supporting, or preferences.",
    "page",
    "map"};

  parser.addOption(portableOption);
  parser.addOption(draftUpdatesOption);
  parser.addOption(uiSnapshotOption);
  parser.addOption(uiSnapshotThemeOption);
  parser.addOption(uiSnapshotPageOption);
  parser.addPositionalArgument("files", "Map files to open.", "[files...]");
  parser.process(app);

  auto options = CommandLineOptions{
    parser.isSet(draftUpdatesOption), parser.positionalArguments(), std::nullopt};

  if (!parser.isSet(uiSnapshotOption))
  {
    if (parser.isSet(uiSnapshotThemeOption) || parser.isSet(uiSnapshotPageOption))
    {
      qCritical() << "UI snapshot options require --ui-snapshot";
      return std::nullopt;
    }
    return options;
  }

  if (options.fileNames.size() > 1)
  {
    qCritical() << "--ui-snapshot accepts at most one map file";
    return std::nullopt;
  }

  const auto snapshotTheme = parser.value(uiSnapshotThemeOption).trimmed().toLower();
  if (
    snapshotTheme != QStringLiteral("system") && snapshotTheme != QStringLiteral("light")
    && snapshotTheme != QStringLiteral("dark"))
  {
    qCritical() << "Unsupported UI snapshot theme:" << snapshotTheme;
    return std::nullopt;
  }

  const auto snapshotPage = parser.value(uiSnapshotPageOption).trimmed().toLower();
  if (
    snapshotPage != QStringLiteral("map") && snapshotPage != QStringLiteral("outliner")
    && snapshotPage != QStringLiteral("supporting")
    && snapshotPage != QStringLiteral("preferences"))
  {
    qCritical() << "Unsupported UI snapshot page:" << snapshotPage;
    return std::nullopt;
  }
  if (
    options.fileNames.empty() && snapshotPage != QStringLiteral("map")
    && snapshotPage != QStringLiteral("preferences"))
  {
    qCritical() << "The selected UI snapshot page requires one map file";
    return std::nullopt;
  }

  options.uiSnapshot = UiSnapshotCommandLineOptions{
    parser.value(uiSnapshotOption), snapshotTheme, snapshotPage};
  return options;
}

WelcomeWindow* findWelcomeWindow()
{
  for (auto* widget : QApplication::topLevelWidgets())
  {
    if (auto* welcomeWindow = qobject_cast<WelcomeWindow*>(widget))
    {
      return welcomeWindow;
    }
  }
  return nullptr;
}

void configureOutlinerSnapshot(QWidget& targetWidget)
{
  if (auto* propertiesToggle = targetWidget.findChild<QAbstractButton*>(
        QStringLiteral("OutlinerInspector_PropertiesToggle")))
  {
    if (!propertiesToggle->isChecked())
    {
      propertiesToggle->click();
    }
  }

  if (auto* tree =
        targetWidget.findChild<QTreeWidget*>(QStringLiteral("OutlinerTreeWidget")))
  {
    tree->expandAll();
    if (auto* firstItem = tree->topLevelItem(0))
    {
      const auto signalBlocker = QSignalBlocker{tree};
      auto* snapshotItem = firstItem->childCount() > 0 ? firstItem->child(0) : firstItem;
      tree->clearSelection();
      tree->setCurrentItem(snapshotItem);
      snapshotItem->setSelected(true);
      tree->setFocus(Qt::OtherFocusReason);
    }
  }
}

void configureSupportingSnapshot(QWidget& targetWidget)
{
  auto* mapWindow = qobject_cast<MapWindow*>(&targetWidget);
  if (mapWindow == nullptr)
  {
    return;
  }

  mapWindow->switchToInfoPanelPage(InfoPanelPage::Assets);
  if (
    auto* splitter = mapWindow->findChild<QSplitter*>(
      QStringLiteral("MapWindow_VerticalSplitterSplitter")))
  {
    splitter->setSizes(QList<int>{560, 340});
  }
}

void configurePreferencesSnapshot(QWidget& targetWidget)
{
  if (
    auto* navigation = targetWidget.findChild<QListWidget*>(
      QStringLiteral("PreferenceDialog_NavigationList")))
  {
    navigation->setCurrentRow(1);
    navigation->setFocus(Qt::OtherFocusReason);
  }
}

void scheduleUiSnapshot(
  QApplication& app,
  QWidget& targetWidget,
  const QString& targetName,
  const UiSnapshotCommandLineOptions& options)
{
  const auto guardedWidget = QPointer<QWidget>{&targetWidget};
  QTimer::singleShot(0, &app, [&app, guardedWidget, targetName, options]() {
    if (guardedWidget.isNull())
    {
      qCritical() << "UI snapshot target was destroyed before layout:" << targetName;
      app.exit(3);
      return;
    }

    if (targetName == QStringLiteral("outliner"))
    {
      configureOutlinerSnapshot(*guardedWidget);
    }
    else if (targetName == QStringLiteral("supporting"))
    {
      configureSupportingSnapshot(*guardedWidget);
    }
    else if (targetName == QStringLiteral("preferences"))
    {
      configurePreferencesSnapshot(*guardedWidget);
    }
    guardedWidget->ensurePolished();
    guardedWidget->update();
    QTimer::singleShot(250, &app, [&app, guardedWidget, targetName, options]() {
      if (guardedWidget.isNull())
      {
        qCritical() << "UI snapshot target was destroyed before capture";
        app.exit(3);
        return;
      }

      if (targetName == QStringLiteral("outliner"))
      {
        configureOutlinerSnapshot(*guardedWidget);
      }
      else if (targetName == QStringLiteral("supporting"))
      {
        configureSupportingSnapshot(*guardedWidget);
      }
      else if (targetName == QStringLiteral("preferences"))
      {
        configurePreferencesSnapshot(*guardedWidget);
      }
      auto error = QString{};
      const auto snapshotOptions = UiSnapshotOptions{
        options.outputPath,
        targetName,
        options.theme,
        qEnvironmentVariable("QT_SCALE_FACTOR", "system")};
      if (!saveUiSnapshot(*guardedWidget, snapshotOptions, &error))
      {
        qCritical().noquote() << "UI snapshot failed:" << error;
        app.exit(4);
        return;
      }

      qInfo().noquote() << "UI snapshot saved:" << options.outputPath;
      app.exit(0);
    });
  });
}

void enableDraftReleaseUpdates()
{
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::EnableDraftReleaseUpdates, true);
  prefs.set(Preferences::IncludeDraftReleaseUpdates, true);
  prefs.saveChanges();
}


} // namespace

int main(int argc, char* argv[])
{
  // Set OpenGL defaults
  // Needs to be done here before QApplication is created
  // (see: https://doc.qt.io/qt-5/qsurfaceformat.html#setDefaultFormat)
  QSurfaceFormat format;
  format.setRenderableType(QSurfaceFormat::OpenGL);
  format.setVersion(2, 1);
  format.setProfile(QSurfaceFormat::CompatibilityProfile);
  format.setDepthBufferSize(24);
  format.setSamples(4);
  QSurfaceFormat::setDefaultFormat(format);

  // Makes all QOpenGLWidget in the application share a single context
  // (default behaviour would be for QOpenGLWidget's in a single top-level window to share
  // a context.) see: http://doc.qt.io/qt-5/qopenglwidget.html#context-sharing
  QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

  // Set up Hi DPI scaling
  // Enables non-integer scaling (e.g. 150% scaling on Windows)
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
    Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

  // When this flag is enabled, font and palette changes propagate as though the user
  // had manually called the corresponding QWidget methods.
  QGuiApplication::setAttribute(Qt::AA_UseStyleSheetPropagationInWidgetStyles);

  // Don't show icons in menus, they are scaled down and don't look very good.
  QGuiApplication::setAttribute(Qt::AA_DontShowIconsInMenus);

  // Store settings in INI format
  QSettings::setDefaultFormat(QSettings::IniFormat);

  // Workaround bug in Qt's Ctrl+Click = RMB emulation (a macOS feature.)
  // In Qt 5.13.0 / macOS 10.14.6, Ctrl+trackpad click+Drag produces no mouse events at
  // all, but it should produce RMB down/move events. This environment variable disables
  // Qt's emulation so we can implement it ourselves in InputEventRecorder::recordEvent
  qputenv("QT_MAC_DONT_OVERRIDE_CTRL_LMB", "1");

  // Disable Qt OpenGL buglist; since we require desktop OpenGL 2.1 there's no point in
  // having Qt disable it (also we've had reports of some Intel drivers being blocked that
  // actually work with TB.)
  qputenv("QT_OPENGL_BUGLIST", ":/opengl_buglist.json");

  // parse portable arg out manually at first to ensure it's set before any settings load
  if (argc > 1)
  {
    for (int i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], "--portable") == 0)
      {
        SystemPaths::setPortable();
        QSettings::setPath(
          QSettings::IniFormat, QSettings::UserScope, QString("./config"));
      }
    }
  }

  // Needs to be set before creating the preference manager
  QApplication::setApplicationName("TrenchBroom");
  // Needs to be "" otherwise Qt adds this to the paths returned by QStandardPaths
  // which would cause preferences to move from where they were with wx
  QApplication::setOrganizationName("");
  QApplication::setOrganizationDomain("io.github.trenchbroom");

  // QApplication must be created before QPreferenceStore because QPreferenceStore uses
  // QFileSystemWatcher, which requires a QApplication instance
  auto app = QApplication{argc, argv};

  installCrashHandlers();

  const auto commandLineOptions = parseCommandLine(app);
  if (!commandLineOptions)
  {
    return 2;
  }

  auto snapshotSettingsDirectory = std::unique_ptr<QTemporaryDir>{};
  auto preferenceFilePath = pathAsQString(SystemPaths::preferenceFilePath());
  if (commandLineOptions->uiSnapshot)
  {
    snapshotSettingsDirectory = std::make_unique<QTemporaryDir>();
    if (!snapshotSettingsDirectory->isValid())
    {
      qCritical() << "Could not create isolated UI snapshot settings directory";
      return 2;
    }

    QSettings::setPath(
      QSettings::IniFormat, QSettings::UserScope, snapshotSettingsDirectory->path());
    preferenceFilePath = snapshotSettingsDirectory->filePath("Preferences.json");
  }

  // PreferenceManager is destroyed by TrenchBroomApp::~TrenchBroomApp()
  PreferenceManager::createInstance(
    std::make_unique<QPreferenceStore>(preferenceFilePath));

  loadTranslations(app);

  // Styles must be loaded before creating the app controller, or they won't apply to
  // the welcome window, which the app controller creates.
  auto themeOverride = std::optional<QString>{};
  if (commandLineOptions->uiSnapshot)
  {
    themeOverride = commandLineOptions->uiSnapshot->theme;
  }
  const auto themeTokens = loadStyle(app, themeOverride);
  if (auto error = QString{}; !loadStyleSheets(themeTokens, &error))
  {
    qWarning().noquote() << "Could not load application stylesheet:" << error;
    if (commandLineOptions->uiSnapshot)
    {
      return 3;
    }
  }

  auto appController = createAppController(commandLineOptions->uiSnapshot.has_value());
  auto crashReporter = CrashReporter{*appController};
  setContractViolationHandler(crashReporter);

#ifdef __APPLE__
  app.setQuitOnLastWindowClosed(false);
  populateMainMenu(*appController);
  installFileEventFilter(*appController);
#endif

  if (commandLineOptions->enableDraftReleaseUpdates)
  {
    enableDraftReleaseUpdates();
  }

  if (commandLineOptions->uiSnapshot)
  {
    auto* targetWidget = static_cast<QWidget*>(nullptr);
    auto targetName = QString{};
    auto preferencesDialog = std::unique_ptr<PreferenceDialog>{};

    if (commandLineOptions->uiSnapshot->page == QStringLiteral("preferences"))
    {
      preferencesDialog = std::make_unique<PreferenceDialog>(*appController, nullptr);
      targetWidget = preferencesDialog.get();
      targetName = QStringLiteral("preferences");
      targetWidget->resize(960, 640);
      configurePreferencesSnapshot(*targetWidget);
    }
    else if (commandLineOptions->fileNames.empty())
    {
      targetWidget = findWelcomeWindow();
      targetName = QStringLiteral("welcome");
      if (targetWidget != nullptr)
      {
        targetWidget->resize(700, 500);
      }
    }
    else
    {
      targetName = commandLineOptions->uiSnapshot->page == QStringLiteral("map")
                     ? QStringLiteral("workbench")
                     : commandLineOptions->uiSnapshot->page;
      if (openFiles(*appController, commandLineOptions->fileNames))
      {
        auto* mapWindow = appController->mapWindowManager().topMapWindow();
        targetWidget = mapWindow;
        if (mapWindow != nullptr)
        {
          mapWindow->resize(1440, 900);
          if (commandLineOptions->uiSnapshot->page == QStringLiteral("outliner"))
          {
            mapWindow->switchToInspectorPage(InspectorPage::Outliner);
            configureOutlinerSnapshot(*mapWindow);
          }
          else if (commandLineOptions->uiSnapshot->page == QStringLiteral("supporting"))
          {
            configureSupportingSnapshot(*mapWindow);
          }
        }
      }
    }

    if (targetWidget == nullptr)
    {
      qCritical() << "UI snapshot target was not created:" << targetName;
      return 3;
    }
    targetWidget->setAttribute(Qt::WA_DontShowOnScreen);
    targetWidget->show();
    scheduleUiSnapshot(app, *targetWidget, targetName, *commandLineOptions->uiSnapshot);
    return app.exec();
  }

  appController->askForAutoUpdates();
  appController->triggerAutoUpdateCheck();

  if (!openFiles(*appController, commandLineOptions->fileNames))
  {
    appController->showWelcomeWindow();
  }

  return app.exec();
}
