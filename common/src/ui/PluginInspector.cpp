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

#include "PluginInspector.h"

#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include "io/PathQt.h"
#include "ui/ClickableTitleBar.h"
#include "ui/CollapsibleTitledPanel.h"
#include "ui/PythonScripting.h"
#include "ui/QtUtils.h"

namespace tb::ui
{

PluginInspector::PluginInspector(QWidget* parent)
  : TabBookPage{parent}
{
  m_scrollArea = new QScrollArea{};
  m_scrollArea->setWidgetResizable(true);

  m_container = new QWidget{};
  m_containerLayout = new QVBoxLayout{};
  m_containerLayout->setContentsMargins(6, 6, 6, 6);
  m_containerLayout->setSpacing(6);

  m_container->setStyleSheet(R"(
    QWidget#PluginInspector_PythonRunnerPanel,
    QWidget#PluginInspector_PluginPanel {
      background-color: rgba(255, 255, 255, 10);
      border: 2 solid rgba(0, 0, 0, 90);
      border-radius: 8;
    }
    QWidget#PluginInspector_PythonRunnerPanel:hover,
    QWidget#PluginInspector_PluginPanel:hover {
      background-color: rgba(255, 255, 255, 12);
      border: 2 solid rgba(0, 0, 0, 110);
    }

    QWidget#PluginInspector_PythonRunnerPanel > ClickableTitleBar,
    QWidget#PluginInspector_PluginPanel > ClickableTitleBar {
      background-color: rgba(0, 0, 0, 18);
      border: none;
      border-top-left-radius: 8;
      border-top-right-radius: 8;
      border-bottom-left-radius: 0;
      border-bottom-right-radius: 0;
    }

    QWidget#PluginInspector_PythonRunnerPanel > QFrame,
    QWidget#PluginInspector_PluginPanel > QFrame {
      background-color: rgba(0, 0, 0, 70);
      border: none;
      min-height: 1px;
      max-height: 1px;
    }

    QWidget#PluginInspector_PythonRunnerPanel > QWidget,
    QWidget#PluginInspector_PluginPanel > QWidget {
      background-color: rgba(0, 0, 0, 14);
      border: none;
      border-bottom-left-radius: 8;
      border-bottom-right-radius: 8;
    }

    QToolButton#PluginInspector_CloseButton {
      border: none;
      background-color: transparent;
      color: rgba(255, 255, 255, 170);
      border-radius: 6;
      padding: 0px;
      margin-left: 6px;
      min-width: 18px;
      max-width: 18px;
      min-height: 18px;
      max-height: 18px;
    }

    QToolButton#PluginInspector_CloseButton:hover {
      background-color: rgba(255, 255, 255, 12);
      color: rgba(255, 255, 255, 235);
    }

    QToolButton#PluginInspector_CloseButton:pressed {
      background-color: rgba(0, 0, 0, 25);
    }

    QWidget#PluginInspector_PythonRunnerPanel QLineEdit,
    QWidget#PluginInspector_PluginPanel QLineEdit,
    QWidget#PluginInspector_PythonRunnerPanel QPlainTextEdit,
    QWidget#PluginInspector_PluginPanel QPlainTextEdit,
    QWidget#PluginInspector_PythonRunnerPanel QTextEdit,
    QWidget#PluginInspector_PluginPanel QTextEdit,
    QWidget#PluginInspector_PythonRunnerPanel QSpinBox,
    QWidget#PluginInspector_PluginPanel QSpinBox,
    QWidget#PluginInspector_PythonRunnerPanel QDoubleSpinBox,
    QWidget#PluginInspector_PluginPanel QDoubleSpinBox,
    QWidget#PluginInspector_PythonRunnerPanel QComboBox,
    QWidget#PluginInspector_PluginPanel QComboBox,
    QWidget#PluginInspector_PythonRunnerPanel QCheckBox,
    QWidget#PluginInspector_PluginPanel QCheckBox {
      background-color: rgba(0, 0, 0, 32);
      border: 1 solid rgba(0, 0, 0, 75);
      border-radius: 6;
      padding: 3px 6px;
      color: rgba(255, 255, 255, 230);
      selection-background-color: rgba(255, 255, 255, 35);
      selection-color: rgba(255, 255, 255, 255);
    }

    QWidget#PluginInspector_PythonRunnerPanel QLineEdit:focus,
    QWidget#PluginInspector_PluginPanel QLineEdit:focus,
    QWidget#PluginInspector_PythonRunnerPanel QPlainTextEdit:focus,
    QWidget#PluginInspector_PluginPanel QPlainTextEdit:focus,
    QWidget#PluginInspector_PythonRunnerPanel QTextEdit:focus,
    QWidget#PluginInspector_PluginPanel QTextEdit:focus,
    QWidget#PluginInspector_PythonRunnerPanel QSpinBox:focus,
    QWidget#PluginInspector_PluginPanel QSpinBox:focus,
    QWidget#PluginInspector_PythonRunnerPanel QDoubleSpinBox:focus,
    QWidget#PluginInspector_PluginPanel QDoubleSpinBox:focus,
    QWidget#PluginInspector_PythonRunnerPanel QComboBox:focus,
    QWidget#PluginInspector_PluginPanel QComboBox:focus,
    QWidget#PluginInspector_PythonRunnerPanel QCheckBox:focus,
    QWidget#PluginInspector_PluginPanel QCheckBox:focus {
      border: 1 solid rgba(255, 255, 255, 70);
      background-color: rgba(255, 255, 255, 10);
      color: rgba(255, 255, 255, 255);
    }
  )");

  {
    auto* panel = new CollapsibleTitledPanel{tr("Python"), true};
    panel->setObjectName("PluginInspector_PythonRunnerPanel");
    panel->setAttribute(Qt::WA_StyledBackground, true);
    if (auto* titleBar = panel->findChild<ClickableTitleBar*>())
    {
      titleBar->setAttribute(Qt::WA_StyledBackground, true);
    }
    auto* container = panel->getPanel();
    container->setAttribute(Qt::WA_StyledBackground, true);

    auto* v = new QVBoxLayout{};
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(4);
    container->setLayout(v);

    auto* pathEdit = new QLineEdit{};
    pathEdit->setClearButtonEnabled(true);
    pathEdit->setPlaceholderText(tr("Python script path (*.py)"));

    auto* browseButton = new QPushButton{tr("Browse...")};
    auto* loadButton = new QPushButton{tr("Load")};
    auto* saveButton = new QPushButton{tr("Save")};
    auto* runButton = new QPushButton{tr("Run")};

    auto* toolbar = new QHBoxLayout{};
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(4);
    toolbar->addWidget(pathEdit, 1);
    toolbar->addWidget(browseButton, 0);
    toolbar->addWidget(loadButton, 0);
    toolbar->addWidget(saveButton, 0);
    toolbar->addWidget(runButton, 0);
    v->addLayout(toolbar);

    auto* editor = new QPlainTextEdit{};
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    editor->setTabStopDistance(editor->fontMetrics().horizontalAdvance(' ') * 4.0);
    editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    v->addWidget(editor, 1);

    auto* info = new QLabel{tr("Output: View → Toggle Info Panel → Python Console")};
    makeInfo(info);
    v->addWidget(info, 0);

    const auto loadFileIntoEditor = [pathEdit, editor]() {
      const auto pathStr = pathEdit->text().trimmed();
      if (pathStr.isEmpty())
      {
        return false;
      }
      QFile f{pathStr};
      if (!f.open(QIODevice::ReadOnly))
      {
        return false;
      }

      editor->setUpdatesEnabled(false);
      const auto wasBlocked = editor->blockSignals(true);

      editor->setPlainText(QString::fromUtf8(f.readAll()));
      editor->document()->setModified(false);

      editor->blockSignals(wasBlocked);
      editor->setUpdatesEnabled(true);

      return true;
    };

    const auto saveEditorToFile = [this, pathEdit, editor]() {
      auto pathStr = pathEdit->text().trimmed();
      if (pathStr.isEmpty())
      {
        pathStr = QFileDialog::getSaveFileName(
          this,
          tr("Save Python Script"),
          fileDialogDefaultDirectory(FileDialogDir::Map),
          tr("Python Scripts (*.py);;All Files (*)"));
        if (pathStr.isEmpty())
        {
          return QString{};
        }
        pathEdit->setText(pathStr);
        updateFileDialogDefaultDirectoryWithFilename(FileDialogDir::Map, pathStr);
      }

      QFile f{pathStr};
      if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
      {
        return QString{};
      }
      const auto bytes = editor->toPlainText().toUtf8();
      if (f.write(bytes) != bytes.size())
      {
        return QString{};
      }
      editor->document()->setModified(false);
      return pathStr;
    };

    QObject::connect(
      browseButton,
      &QPushButton::clicked,
      container,
      [this, pathEdit, loadFileIntoEditor]() {
      const auto pathStr = QFileDialog::getOpenFileName(
        this,
        tr("Open Python Script"),
        fileDialogDefaultDirectory(FileDialogDir::Map),
        tr("Python Scripts (*.py);;All Files (*)"));
      if (pathStr.isEmpty())
      {
        return;
      }
      pathEdit->setText(pathStr);
      updateFileDialogDefaultDirectoryWithFilename(FileDialogDir::Map, pathStr);
      loadFileIntoEditor();
    });

    QObject::connect(loadButton, &QPushButton::clicked, container, [this, loadFileIntoEditor]() {
      if (!loadFileIntoEditor())
      {
        QMessageBox::warning(this, tr("Load Failed"), tr("Could not load the selected script."));
      }
    });

    QObject::connect(saveButton, &QPushButton::clicked, container, [this, saveEditorToFile]() {
      const auto saved = saveEditorToFile();
      if (saved.isEmpty())
      {
        QMessageBox::warning(this, tr("Save Failed"), tr("Could not save the script."));
      }
    });

    QObject::connect(runButton, &QPushButton::clicked, container, [this, saveEditorToFile]() {
      const auto savedPath = saveEditorToFile();
      if (savedPath.isEmpty())
      {
        return;
      }

      auto* frame = findMapFrame(this);
      if (frame == nullptr)
      {
        QMessageBox::warning(this, tr("Run Failed"), tr("No active map window."));
        return;
      }

      if (!PythonScripting::instance().runScript(*frame, io::pathFromQString(savedPath)))
      {
        QMessageBox::warning(
          this,
          tr("Python Script Failed"),
          tr("The Python script failed. See the Python console for details."));
      }
    });

    m_containerLayout->addWidget(panel, 0);
  }

  m_emptyLabel = new QLabel{
    tr("No plugin panels loaded.\nThis panel is reserved for plugin-provided UI.")};
  makeInfo(m_emptyLabel);
  m_containerLayout->addWidget(m_emptyLabel, 0);
  m_containerLayout->addStretch(1);

  m_container->setLayout(m_containerLayout);
  m_scrollArea->setWidget(m_container);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_scrollArea, 1);
  setLayout(layout);
}

QWidget* PluginInspector::addPluginPanel(const QString& title)
{
  if (m_emptyLabel)
  {
    m_containerLayout->removeWidget(m_emptyLabel);
    delete m_emptyLabel;
    m_emptyLabel = nullptr;
  }

  auto insertIndex = m_containerLayout->count() - 1;
  if (insertIndex < 0)
  {
    insertIndex = 0;
  }

  auto* panel = new CollapsibleTitledPanel{title, true};
  panel->setObjectName("PluginInspector_PluginPanel");
  panel->setAttribute(Qt::WA_StyledBackground, true);
  if (auto* titleBar = panel->findChild<ClickableTitleBar*>())
  {
    titleBar->setAttribute(Qt::WA_StyledBackground, true);

    auto* closeButton = new QToolButton{titleBar};
    closeButton->setObjectName("PluginInspector_CloseButton");
    closeButton->setAutoRaise(true);
    closeButton->setText(QStringLiteral("×"));
    closeButton->setToolTip(tr("Close"));

    titleBar->layout()->addWidget(closeButton);

    QObject::connect(closeButton, &QToolButton::clicked, panel, [this, panel]() {
      m_containerLayout->removeWidget(panel);
      panel->deleteLater();

      auto hasAnyPluginPanel = false;
      for (auto i = 0; i < m_containerLayout->count(); ++i)
      {
        if (auto* item = m_containerLayout->itemAt(i))
        {
          if (auto* w = item->widget())
          {
            if (w->objectName() == QStringLiteral("PluginInspector_PluginPanel"))
            {
              hasAnyPluginPanel = true;
              break;
            }
          }
        }
      }

      if (!hasAnyPluginPanel && m_emptyLabel == nullptr)
      {
        m_emptyLabel = new QLabel{
          tr("No plugin panels loaded.\nThis panel is reserved for plugin-provided UI.")};
        makeInfo(m_emptyLabel);
        m_containerLayout->insertWidget(1, m_emptyLabel, 0);
      }
    });
  }
  panel->getPanel()->setAttribute(Qt::WA_StyledBackground, true);
  m_containerLayout->insertWidget(insertIndex, panel, 0);
  return panel->getPanel();
}

} // namespace tb::ui
