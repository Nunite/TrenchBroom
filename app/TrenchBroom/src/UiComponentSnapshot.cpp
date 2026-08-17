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

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "UiComponentSnapshot.h"

#include <initializer_list>

namespace tb::ui
{
namespace
{

QWidget* makeControlRow(const std::initializer_list<QWidget*> controls)
{
  auto* row = new QWidget{};
  auto* layout = new QHBoxLayout{row};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);
  for (auto* control : controls)
  {
    layout->addWidget(control);
  }
  layout->addStretch(1);
  return row;
}

QGroupBox* makeSection(const QString& title)
{
  auto* section = new QGroupBox{title};
  auto* layout = new QFormLayout{section};
  layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  layout->setHorizontalSpacing(12);
  layout->setVerticalSpacing(8);
  return section;
}

QFormLayout* sectionLayout(QGroupBox& section)
{
  return static_cast<QFormLayout*>(section.layout());
}

} // namespace

std::unique_ptr<QWidget> createUiComponentSnapshot()
{
  auto result = std::make_unique<QWidget>();
  result->setObjectName(QStringLiteral("UiComponentSnapshot"));
  result->setWindowTitle(QStringLiteral("TrenchBroom UI Components"));

  auto* title = new QLabel{QStringLiteral("Foundational Components")};
  auto titleFont = title->font();
  titleFont.setBold(true);
  titleFont.setPointSize(titleFont.pointSize() + 3);
  title->setFont(titleFont);

  auto* subtitle = new QLabel{QStringLiteral(
    "Canonical normal, editable, read-only, selected, and disabled states")};
  subtitle->setObjectName(QStringLiteral("UiComponentSnapshot_Subtitle"));

  auto* inputSection = makeSection(QStringLiteral("Input"));
  auto* inputLayout = sectionLayout(*inputSection);

  auto* focusedLineEdit = new QLineEdit{QStringLiteral("Focused value")};
  focusedLineEdit->setObjectName(QStringLiteral("UiComponentSnapshot_FocusedInput"));
  auto* readOnlyLineEdit = new QLineEdit{QStringLiteral("Read-only value")};
  readOnlyLineEdit->setReadOnly(true);
  auto* disabledLineEdit = new QLineEdit{QStringLiteral("Disabled value")};
  disabledLineEdit->setEnabled(false);
  inputLayout->addRow(
    QStringLiteral("Text"),
    makeControlRow({focusedLineEdit, readOnlyLineEdit, disabledLineEdit}));

  auto* combo = new QComboBox{};
  combo->addItems(
    {QStringLiteral("Quake"), QStringLiteral("Quake II"), QStringLiteral("Half-Life")});
  auto* editableCombo = new QComboBox{};
  editableCombo->setEditable(true);
  editableCombo->addItems({QStringLiteral("Custom"), QStringLiteral("Inherited")});
  auto* disabledCombo = new QComboBox{};
  disabledCombo->addItem(QStringLiteral("Disabled"));
  disabledCombo->setEnabled(false);
  inputLayout->addRow(
    QStringLiteral("Choice"), makeControlRow({combo, editableCombo, disabledCombo}));

  auto* spinBox = new QSpinBox{};
  spinBox->setRange(-64, 64);
  spinBox->setValue(16);
  auto* doubleSpinBox = new QDoubleSpinBox{};
  doubleSpinBox->setRange(0.0, 360.0);
  doubleSpinBox->setValue(45.0);
  doubleSpinBox->setSuffix(QStringLiteral(" deg"));
  auto* disabledSpinBox = new QSpinBox{};
  disabledSpinBox->setValue(8);
  disabledSpinBox->setEnabled(false);
  inputLayout->addRow(
    QStringLiteral("Number"), makeControlRow({spinBox, doubleSpinBox, disabledSpinBox}));

  auto* plainTextEdit =
    new QPlainTextEdit{QStringLiteral("Multi-line input\nSecond line")};
  plainTextEdit->setFixedHeight(64);
  auto* readOnlyTextEdit = new QTextEdit{};
  readOnlyTextEdit->setPlainText(QStringLiteral("Read-only output\nSecond line"));
  readOnlyTextEdit->setReadOnly(true);
  readOnlyTextEdit->setFixedHeight(64);
  inputLayout->addRow(
    QStringLiteral("Text area"), makeControlRow({plainTextEdit, readOnlyTextEdit}));

  auto* selectionSection = makeSection(QStringLiteral("Selection"));
  auto* selectionLayout = sectionLayout(*selectionSection);

  auto* unchecked = new QCheckBox{QStringLiteral("Unchecked")};
  auto* checked = new QCheckBox{QStringLiteral("Checked")};
  checked->setChecked(true);
  auto* disabledChecked = new QCheckBox{QStringLiteral("Disabled")};
  disabledChecked->setChecked(true);
  disabledChecked->setEnabled(false);
  selectionLayout->addRow(
    QStringLiteral("Checkbox"), makeControlRow({unchecked, checked, disabledChecked}));

  auto* radioOff = new QRadioButton{QStringLiteral("Option A")};
  auto* radioOn = new QRadioButton{QStringLiteral("Option B")};
  radioOn->setChecked(true);
  auto* radioDisabled = new QRadioButton{QStringLiteral("Disabled")};
  radioDisabled->setEnabled(false);
  selectionLayout->addRow(
    QStringLiteral("Radio"), makeControlRow({radioOff, radioOn, radioDisabled}));

  auto* slider = new QSlider{Qt::Horizontal};
  slider->setRange(0, 100);
  slider->setValue(60);
  slider->setMinimumWidth(220);
  auto* disabledSlider = new QSlider{Qt::Horizontal};
  disabledSlider->setRange(0, 100);
  disabledSlider->setValue(35);
  disabledSlider->setMinimumWidth(160);
  disabledSlider->setEnabled(false);
  selectionLayout->addRow(
    QStringLiteral("Range"), makeControlRow({slider, disabledSlider}));

  auto* actionSection = makeSection(QStringLiteral("Commands"));
  auto* actionLayout = sectionLayout(*actionSection);
  auto* primaryButton = new QPushButton{QStringLiteral("Apply")};
  primaryButton->setDefault(true);
  auto* button = new QPushButton{QStringLiteral("Reset")};
  auto* disabledButton = new QPushButton{QStringLiteral("Disabled")};
  disabledButton->setEnabled(false);
  actionLayout->addRow(
    QStringLiteral("Button"), makeControlRow({primaryButton, button, disabledButton}));

  auto* toolButton = new QToolButton{};
  toolButton->setIcon(result->style()->standardIcon(QStyle::SP_BrowserReload));
  toolButton->setToolTip(QStringLiteral("Reload"));
  auto* checkedToolButton = new QToolButton{};
  checkedToolButton->setIcon(result->style()->standardIcon(QStyle::SP_DialogApplyButton));
  checkedToolButton->setCheckable(true);
  checkedToolButton->setChecked(true);
  checkedToolButton->setToolTip(QStringLiteral("Enabled"));
  auto* disabledToolButton = new QToolButton{};
  disabledToolButton->setIcon(
    result->style()->standardIcon(QStyle::SP_DialogCancelButton));
  disabledToolButton->setEnabled(false);
  actionLayout->addRow(
    QStringLiteral("Tool"),
    makeControlRow({toolButton, checkedToolButton, disabledToolButton}));

  auto* layout = new QVBoxLayout{result.get()};
  layout->setContentsMargins(18, 16, 18, 18);
  layout->setSpacing(12);
  layout->addWidget(title);
  layout->addWidget(subtitle);
  layout->addWidget(inputSection);
  layout->addWidget(selectionSection);
  layout->addWidget(actionSection);
  layout->addStretch(1);

  focusedLineEdit->setFocus(Qt::OtherFocusReason);
  focusedLineEdit->selectAll();
  return result;
}

} // namespace tb::ui
