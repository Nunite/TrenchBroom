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

#include "UiSnapshotRunner.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDeadlineTimer>
#include <QDebug>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextStream>
#include <QTimer>
#include <QTreeWidget>

#include "base/PreferenceManager.h"
#include "fs/DiskIO.h"
#include "gl/Material.h"
#include "gl/MaterialManager.h"
#include "gl/Resource.h"
#include "gl/Texture.h"
#include "mdl/GameManager.h"
#include "mdl/Map.h"
#include "mdl/MapHeader.h"
#include "prefs/Preferences.h"
#include "ui/ActionExecutionContext.h"
#include "ui/AppController.h"
#include "ui/CommandPaletteDialog.h"
#include "ui/InfoPanel.h"
#include "ui/Inspector.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/MaterialBrowser.h"
#include "ui/PreferenceDialog.h"
#include "ui/QPathUtils.h"
#include "ui/ThemeRegistry.h"
#include "ui/UiSnapshot.h"
#include "ui/WelcomeWindow.h"

#include <filesystem>
#include <memory>
#include <variant>

namespace tb::ui
{
namespace
{

bool configureGamePath(
  AppController& appController,
  const UiSnapshotCommandLineOptions& options,
  const QStringList& fileNames)
{
  if (options.gamePath.isEmpty())
  {
    return true;
  }

  if (fileNames.empty())
  {
    qCritical() << "--ui-snapshot-game-path requires one map file";
    return false;
  }

  return fs::Disk::withInputStream(pathFromQString(fileNames.front()), mdl::readMapHeader)
         | kdl::transform([&](const auto& detectedGameAndFormat) {
             const auto& gameName = detectedGameAndFormat.first;
             if (!gameName)
             {
               qCritical() << "Could not detect the snapshot map's game";
               return false;
             }

             auto* gameInfo = appController.gameManager().gameInfo(*gameName);
             if (gameInfo == nullptr)
             {
               qCritical() << "Snapshot map uses an unavailable game:"
                           << QString::fromStdString(*gameName);
               return false;
             }

             setPref(gameInfo->gamePathPreference, pathFromQString(options.gamePath));
             return true;
           })
         | kdl::transform_error([](const auto& error) {
             qCritical().noquote() << "Could not read the snapshot map header:"
                                   << QString::fromStdString(error.msg);
             return false;
           })
         | kdl::value();
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
  if (
    auto* propertiesToggle = targetWidget.findChild<QAbstractButton*>(
      QStringLiteral("OutlinerInspector_PropertiesToggle")))
  {
    if (!propertiesToggle->isChecked())
    {
      propertiesToggle->click();
    }
  }

  if (
    auto* tree =
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

void configurePythonConsoleSnapshot(QWidget& targetWidget)
{
  auto* mapWindow = qobject_cast<MapWindow*>(&targetWidget);
  if (mapWindow == nullptr)
  {
    return;
  }

  mapWindow->switchToInfoPanelPage(InfoPanelPage::PythonConsole);
  if (
    auto* splitter = mapWindow->findChild<QSplitter*>(
      QStringLiteral("MapWindow_VerticalSplitterSplitter")))
  {
    splitter->setSizes(QList<int>{560, 340});
  }

  auto* input =
    mapWindow->findChild<QPlainTextEdit*>(QStringLiteral("PythonConsole_Input"));
  auto* runButton =
    mapWindow->findChild<QPushButton*>(QStringLiteral("PythonConsole_Run"));
  if (
    input != nullptr && runButton != nullptr
    && !mapWindow->property("uiSnapshotPythonConsoleExecuted").toBool())
  {
    input->setPlainText(QStringLiteral("tb2.current_document().entities[0].classname"));
    runButton->click();
    mapWindow->setProperty("uiSnapshotPythonConsoleExecuted", true);
  }

  if (input != nullptr)
  {
    input->setPlainText(QStringLiteral("print('Ready')"));
    input->setFocus(Qt::OtherFocusReason);
  }
}

bool isInspectorSnapshotTarget(const QString& targetName)
{
  return targetName == QStringLiteral("entity-browser")
         || targetName == QStringLiteral("entity-browser-empty")
         || targetName == QStringLiteral("face-inspector")
         || targetName == QStringLiteral("material-browser-empty")
         || targetName == QStringLiteral("plugin-inspector");
}

bool isMaterialBrowserSnapshotTarget(const QString& targetName)
{
  return targetName == QStringLiteral("face-inspector")
         || targetName == QStringLiteral("material-browser-empty");
}

enum class UiSnapshotReadinessState
{
  Ready,
  Pending,
  Failed,
};

struct UiSnapshotReadiness
{
  UiSnapshotReadinessState state;
  QString detail;
};

UiSnapshotReadiness uiSnapshotReadiness(QWidget& targetWidget, const QString& targetName)
{
  if (!isMaterialBrowserSnapshotTarget(targetName))
  {
    return {UiSnapshotReadinessState::Ready, {}};
  }

  auto* mapWindow = qobject_cast<MapWindow*>(&targetWidget);
  if (mapWindow == nullptr)
  {
    return {
      UiSnapshotReadinessState::Failed,
      QStringLiteral("Material browser snapshot target is not a map window")};
  }

  const auto& materials = mapWindow->document().map().materialManager().materials();
  if (materials.empty())
  {
    return {
      UiSnapshotReadinessState::Pending,
      QStringLiteral("Material browser snapshot has no loaded materials")};
  }

  auto readyCount = size_t{0};
  auto pendingCount = size_t{0};
  auto failedNames = QStringList{};
  for (const auto* material : materials)
  {
    const auto& resource = material->textureResource();
    if (const auto* failed = std::get_if<gl::ResourceFailed>(&resource.state()))
    {
      failedNames.push_back(QStringLiteral("%1 (%2)").arg(
        QString::fromStdString(material->name()), QString::fromStdString(failed->error)));
    }
    else if (const auto* texture = material->texture();
             texture != nullptr && texture->isReady())
    {
      ++readyCount;
    }
    else
    {
      ++pendingCount;
    }
  }

  if (!failedNames.empty())
  {
    return {
      UiSnapshotReadinessState::Failed,
      QStringLiteral("Material textures failed: %1")
        .arg(failedNames.mid(0, 3).join(QStringLiteral("; ")))};
  }

  const auto detail = QStringLiteral("Material textures ready: %1/%2; pending: %3")
                        .arg(readyCount)
                        .arg(materials.size())
                        .arg(pendingCount);
  return {
    pendingCount == 0 ? UiSnapshotReadinessState::Ready
                      : UiSnapshotReadinessState::Pending,
    detail};
}

void configureInspectorSnapshot(QWidget& targetWidget, const QString& targetName)
{
  auto* mapWindow = qobject_cast<MapWindow*>(&targetWidget);
  if (mapWindow == nullptr)
  {
    return;
  }

  const auto showPluginInspector = targetName == QStringLiteral("plugin-inspector");
  if (showPluginInspector)
  {
    mapWindow->switchToInspectorPage(InspectorPage::Plugin);
    if (
      mapWindow->findChild<QWidget*>(QStringLiteral("UiSnapshot_PluginPanelContent"))
      == nullptr)
    {
      auto* content = mapWindow->addPluginPanel(QStringLiteral("Chamfer Tool"));
      content->setObjectName(QStringLiteral("UiSnapshot_PluginPanelContent"));

      auto* status = new QLabel{QStringLiteral("Ready"), content};
      auto* distance = new QDoubleSpinBox{content};
      distance->setObjectName(QStringLiteral("UiSnapshot_PluginDistance"));
      distance->setDecimals(1);
      distance->setRange(0.1, 1024.0);
      distance->setValue(8.0);

      auto* segments = new QSpinBox{content};
      segments->setRange(1, 64);
      segments->setValue(1);

      auto* edgeButton = new QPushButton{QStringLiteral("Chamfer Edge Handles"), content};
      auto* vertexButton =
        new QPushButton{QStringLiteral("Chamfer Vertex Handles"), content};

      auto* panelLayout = new QFormLayout{};
      panelLayout->setContentsMargins(8, 8, 8, 8);
      panelLayout->addRow(QStringLiteral("Status:"), status);
      panelLayout->addRow(QStringLiteral("Distance:"), distance);
      panelLayout->addRow(QStringLiteral("Segments:"), segments);
      panelLayout->addRow(edgeButton);
      panelLayout->addRow(vertexButton);
      content->setLayout(panelLayout);
    }

    if (auto* distance =
          mapWindow->findChild<QDoubleSpinBox*>(QStringLiteral("UiSnapshot_PluginDistance")))
    {
      distance->setFocus(Qt::OtherFocusReason);
      distance->selectAll();
    }
  }
  else if (targetName == QStringLiteral("face-inspector"))
  {
    setPref(Preferences::MaterialBrowserIconSize, 5.0f);
    if (auto* materialBrowser = mapWindow->findChild<MaterialBrowser*>())
    {
      materialBrowser->setGroup(true);
    }
  }

  const auto showEntityBrowser = targetName.startsWith(QStringLiteral("entity-browser"));
  if (!showPluginInspector)
  {
    mapWindow->switchToInspectorPage(
      showEntityBrowser ? InspectorPage::Entity : InspectorPage::Face);
  }
  if (
    auto* splitter =
      mapWindow->findChild<QSplitter*>(QStringLiteral("MapWindow_HorizontalSplitter")))
  {
    splitter->setSizes(QList<int>{940, 500});
  }

  if (targetName.endsWith(QStringLiteral("-empty")))
  {
    const auto searchName = showEntityBrowser ? QStringLiteral("EntityBrowser_Search")
                                              : QStringLiteral("MaterialBrowser_Search");
    if (auto* search = mapWindow->findChild<QLineEdit*>(searchName))
    {
      const auto filterText = QStringLiteral("__ui_snapshot_no_match__");
      search->setText(filterText);
      search->textEdited(filterText);
    }
  }
}

void configurePreferencesSnapshot(
  QWidget& targetWidget, const QString& targetName, const QString& theme = {})
{
  if (
    auto* navigation = targetWidget.findChild<QListWidget*>(
      QStringLiteral("PreferenceDialog_NavigationList")))
  {
    const auto row = targetName == QStringLiteral("preferences-colors")     ? 2
                     : targetName == QStringLiteral("preferences-mouse")    ? 3
                     : targetName == QStringLiteral("preferences-keyboard") ? 4
                     : targetName == QStringLiteral("preferences-misc")     ? 5
                                                                            : 1;
    navigation->setCurrentRow(row);
    navigation->setFocus(Qt::OtherFocusReason);
  }

  if (auto* themeCombo =
        targetWidget.findChild<QComboBox*>(QStringLiteral("ViewPreference_ThemeCombo"));
      !theme.isEmpty() && themeCombo != nullptr)
  {
    const auto themeId = ThemeRegistry::instance().canonicalThemeId(theme);
    const auto index = themeCombo->findData(themeId);
    if (index >= 0)
    {
      themeCombo->setCurrentIndex(index);
    }
  }

  if (targetName == QStringLiteral("preferences-misc"))
  {
    if (auto* pages = targetWidget.findChild<QStackedWidget*>(
          QStringLiteral("PreferenceDialog_Pages"));
        pages != nullptr)
    {
      if (
        auto* scrollArea = pages->currentWidget()->findChild<QScrollArea*>(
          QStringLiteral("PreferencePane_ScrollArea")))
      {
        scrollArea->verticalScrollBar()->setValue(
          scrollArea->verticalScrollBar()->maximum());
      }
    }
  }
}

void configureSnapshot(QWidget& targetWidget, const QString& targetName)
{
  if (targetName == QStringLiteral("outliner"))
  {
    configureOutlinerSnapshot(targetWidget);
  }
  else if (targetName == QStringLiteral("supporting"))
  {
    configureSupportingSnapshot(targetWidget);
  }
  else if (targetName == QStringLiteral("python-console"))
  {
    configurePythonConsoleSnapshot(targetWidget);
  }
  else if (isInspectorSnapshotTarget(targetName))
  {
    configureInspectorSnapshot(targetWidget, targetName);
  }
  else if (targetName.startsWith(QStringLiteral("preferences")))
  {
    configurePreferencesSnapshot(targetWidget, targetName);
  }
}

void failUiSnapshot(
  QApplication& app,
  const UiSnapshotCommandLineOptions& options,
  const QString& message,
  const int exitCode)
{
  qCritical().noquote() << message;

  const auto outputInfo = QFileInfo{options.outputPath};
  if (QDir{}.mkpath(outputInfo.absolutePath()))
  {
    auto failureFile =
      QFile{outputInfo.dir().filePath(outputInfo.completeBaseName() + ".error.txt")};
    if (failureFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
      QTextStream{&failureFile} << message << Qt::endl;
    }
  }
  app.exit(exitCode);
}

void captureUiSnapshot(
  QApplication& app,
  const QPointer<QWidget>& guardedWidget,
  const QString& targetName,
  const UiSnapshotCommandLineOptions& options)
{
  if (guardedWidget.isNull())
  {
    failUiSnapshot(app, options, "UI snapshot target was destroyed before capture", 3);
    return;
  }

  const auto readiness = uiSnapshotReadiness(*guardedWidget, targetName);
  if (readiness.state != UiSnapshotReadinessState::Ready)
  {
    failUiSnapshot(
      app,
      options,
      QStringLiteral("UI snapshot state validation failed: %1").arg(readiness.detail),
      3);
    return;
  }

  auto error = QString{};
  const auto snapshotOptions = UiSnapshotOptions{
    options.outputPath,
    targetName,
    options.theme,
    qEnvironmentVariable("QT_SCALE_FACTOR", "system")};
  if (!saveUiSnapshot(*guardedWidget, snapshotOptions, &error))
  {
    failUiSnapshot(app, options, QStringLiteral("UI snapshot failed: %1").arg(error), 4);
    return;
  }

  qInfo().noquote() << "UI snapshot saved:" << options.outputPath;
  app.exit(0);
}

void waitForUiSnapshotReadiness(
  QApplication& app,
  const QPointer<QWidget>& guardedWidget,
  const QString& targetName,
  const UiSnapshotCommandLineOptions& options,
  const QDeadlineTimer deadline)
{
  if (guardedWidget.isNull())
  {
    failUiSnapshot(
      app, options, "UI snapshot target was destroyed while waiting for resources", 3);
    return;
  }

  const auto readiness = uiSnapshotReadiness(*guardedWidget, targetName);
  if (readiness.state == UiSnapshotReadinessState::Failed)
  {
    failUiSnapshot(
      app,
      options,
      QStringLiteral("UI snapshot resource validation failed: %1").arg(readiness.detail),
      3);
    return;
  }
  if (readiness.state == UiSnapshotReadinessState::Pending)
  {
    if (deadline.hasExpired())
    {
      failUiSnapshot(
        app,
        options,
        QStringLiteral("UI snapshot resource wait timed out: %1").arg(readiness.detail),
        3);
      return;
    }

    QTimer::singleShot(50, &app, [&app, guardedWidget, targetName, options, deadline]() {
      waitForUiSnapshotReadiness(app, guardedWidget, targetName, options, deadline);
    });
    return;
  }

  if (!readiness.detail.isEmpty())
  {
    qInfo().noquote() << "UI snapshot resources ready:" << readiness.detail;
  }
  guardedWidget->update();
  QTimer::singleShot(100, &app, [&app, guardedWidget, targetName, options]() {
    captureUiSnapshot(app, guardedWidget, targetName, options);
  });
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
      failUiSnapshot(
        app,
        options,
        QStringLiteral("UI snapshot target was destroyed before layout: %1")
          .arg(targetName),
        3);
      return;
    }

    configureSnapshot(*guardedWidget, targetName);
    guardedWidget->ensurePolished();
    guardedWidget->update();
    QTimer::singleShot(250, &app, [&app, guardedWidget, targetName, options]() {
      if (!guardedWidget.isNull())
      {
        configureSnapshot(*guardedWidget, targetName);
      }
      waitForUiSnapshotReadiness(
        app, guardedWidget, targetName, options, QDeadlineTimer{5000});
    });
  });
}

} // namespace

int runUiSnapshot(
  QApplication& app,
  AppController& appController,
  const UiSnapshotCommandLineOptions& options,
  const QStringList& fileNames,
  const OpenFilesForUiSnapshot& openFiles)
{
  if (!configureGamePath(appController, options, fileNames))
  {
    return 3;
  }

  auto* targetWidget = static_cast<QWidget*>(nullptr);
  auto targetName = QString{};
  auto preferencesDialog = std::unique_ptr<PreferenceDialog>{};
  auto commandPaletteDialog = std::unique_ptr<CommandPaletteDialog>{};

  if (options.page.startsWith(QStringLiteral("preferences")))
  {
    preferencesDialog = std::make_unique<PreferenceDialog>(appController, nullptr);
    targetWidget = preferencesDialog.get();
    targetName = options.page;
    targetWidget->resize(
      targetName == QStringLiteral("preferences-misc") ? QSize{920, 560}
                                                       : QSize{960, 640});
    configurePreferencesSnapshot(*targetWidget, targetName, options.theme);
  }
  else if (fileNames.empty())
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
    targetName =
      options.page == QStringLiteral("map") ? QStringLiteral("workbench") : options.page;
    if (openFiles(fileNames))
    {
      auto* mapWindow = appController.mapWindowManager().topMapWindow();
      if (mapWindow != nullptr)
      {
        if (options.page == QStringLiteral("command-palette"))
        {
          auto context = ActionExecutionContext{
            appController, mapWindow, mapWindow->currentMapViewBase()};
          commandPaletteDialog = std::make_unique<CommandPaletteDialog>(
            appController.actionManager(),
            context,
            std::filesystem::path{"Menu/View/Command Palette..."},
            mapWindow);
          targetWidget = commandPaletteDialog.get();
          targetWidget->resize(640, 480);
        }
        else
        {
          targetWidget = mapWindow;
          mapWindow->resize(1440, 900);
          if (targetName == QStringLiteral("outliner"))
          {
            mapWindow->switchToInspectorPage(InspectorPage::Outliner);
          }
          configureSnapshot(*mapWindow, targetName);
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
  scheduleUiSnapshot(app, *targetWidget, targetName, options);
  return app.exec();
}

} // namespace tb::ui
