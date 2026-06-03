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

#include <QLabel>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/QPathUtils.h"
#include "ui/ClickableTitleBar.h"
#include "ui/CollapsibleTitledPanel.h"
#include "ui/QWidgetUtils.h"

namespace tb::ui
{

PluginInspector::PluginInspector(QWidget* parent)
  : TabBookPage{parent}
{
  m_scrollArea = new QScrollArea{};
  m_scrollArea->setWidgetResizable(true);

  m_container = new QWidget{};
  m_container->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::MinimumExpanding);
  m_containerLayout = new QVBoxLayout{};
  m_containerLayout->setContentsMargins(6, 6, 6, 6);
  m_containerLayout->setSpacing(6);

  m_container->setStyleSheet(R"(
    QWidget#PluginInspector_PluginPanel {
      background-color: rgba(255, 255, 255, 10);
      border: 2 solid rgba(0, 0, 0, 90);
      border-radius: 8;
    }
    QWidget#PluginInspector_PluginPanel:hover {
      background-color: rgba(255, 255, 255, 12);
      border: 2 solid rgba(0, 0, 0, 110);
    }

    QWidget#PluginInspector_PluginPanel > ClickableTitleBar {
      background-color: rgba(0, 0, 0, 18);
      border: none;
      border-top-left-radius: 8;
      border-top-right-radius: 8;
      border-bottom-left-radius: 0;
      border-bottom-right-radius: 0;
    }

    QWidget#PluginInspector_PluginPanel > QFrame {
      background-color: rgba(0, 0, 0, 70);
      border: none;
      min-height: 1px;
      max-height: 1px;
    }

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

    QWidget#PluginInspector_PluginPanel QLineEdit,
    QWidget#PluginInspector_PluginPanel QPlainTextEdit,
    QWidget#PluginInspector_PluginPanel QTextEdit,
    QWidget#PluginInspector_PluginPanel QSpinBox,
    QWidget#PluginInspector_PluginPanel QDoubleSpinBox,
    QWidget#PluginInspector_PluginPanel QComboBox,
    QWidget#PluginInspector_PluginPanel QCheckBox {
      background-color: rgba(0, 0, 0, 32);
      border: 1 solid rgba(0, 0, 0, 75);
      border-radius: 6;
      padding: 3px 6px;
      color: rgba(255, 255, 255, 230);
      selection-background-color: rgba(255, 255, 255, 35);
      selection-color: rgba(255, 255, 255, 255);
    }

    QWidget#PluginInspector_PluginPanel QLineEdit:focus,
    QWidget#PluginInspector_PluginPanel QPlainTextEdit:focus,
    QWidget#PluginInspector_PluginPanel QTextEdit:focus,
    QWidget#PluginInspector_PluginPanel QSpinBox:focus,
    QWidget#PluginInspector_PluginPanel QDoubleSpinBox:focus,
    QWidget#PluginInspector_PluginPanel QComboBox:focus,
    QWidget#PluginInspector_PluginPanel QCheckBox:focus {
      border: 1 solid rgba(255, 255, 255, 70);
      background-color: rgba(255, 255, 255, 10);
      color: rgba(255, 255, 255, 255);
    }
  )");



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
