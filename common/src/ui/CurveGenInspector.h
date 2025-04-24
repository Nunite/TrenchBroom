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

#pragma once

#include <memory>
#include "ui/TabBook.h"    // Ensure this is included

#include <QProcess>  // Direct inclusion of QProcess definition

class QLineEdit;
class QPushButton;
class QFileDialog;
class QLabel;
class QTextEdit;
class QWidget;
class QComboBox; // Add QComboBox class forward declaration
class QListWidget;
class QTabWidget;
class QFrame;
class QVBoxLayout;
class QHBoxLayout;
class QGridLayout;
class QCheckBox;

namespace tb::ui {

class GLContextManager;
class MapDocument;
class Selection;
class Splitter;

class CurveGenInspector : public TabBookPage {
    Q_OBJECT
public:
    CurveGenInspector(std::weak_ptr<MapDocument> document, GLContextManager& contextManager, QWidget* parent = nullptr);
    ~CurveGenInspector();

public slots:
    void updateSelectionContent(const Selection& selection);

private slots:
    void browseToolPath();
    void executeExternalTool();
    void terminateExternalTool();
    void processFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void readProcessOutput();
    void processError(QProcess::ProcessError error);
    
    // Map2Curve preset editor slots
    void addCurve();
    void deleteCurve();
    void loadPreset();
    void curveSelectionChanged(int index);

private:
    std::weak_ptr<MapDocument> m_document;
    Splitter* m_splitter = nullptr;
    QTextEdit* m_contentView = nullptr;
    QTextEdit* m_selectionView = nullptr;
    
    // External tool execution components
    QWidget* m_toolPanel = nullptr;
    QLineEdit* m_toolPathEdit = nullptr;
    QLineEdit* m_toolArgsEdit = nullptr;
    QPushButton* m_browseButton = nullptr;
    QPushButton* m_executeButton = nullptr;
    QPushButton* m_terminateButton = nullptr;
    QProcess* m_process = nullptr;
    
    QString m_lastTempFile; // Track the last created temporary batch file
    QString m_lastTempDir;  // Track the last used temporary directory
    QString m_currentSelectionContent; // Store current selection content
    
    // Map2Curve preset editor components
    QWidget* m_presetPanel = nullptr;
    QTabWidget* m_tabWidget = nullptr;
    QComboBox* m_curveComboBox = nullptr;
    
    // Map2Curve global commands widgets
    QMap<QString, QLineEdit*> m_globalCommandEdits;
    QMap<QString, QComboBox*> m_globalCommandCombos;
    
    // Map2Curve curve parameters widgets
    QMap<QString, QLineEdit*> m_curveParamEdits;
    QMap<QString, QComboBox*> m_curveParamCombos;
    
    // Current curve data in editor
    int m_currentCurveIndex = -1;
    QList<QMap<QString, QString>> m_curvesList;
    
    void createGui(std::weak_ptr<MapDocument> document, GLContextManager& contextManager);
    void createToolExecutionPanel();
    QString getCurrentSelectionContent() const; // Get current selection content
    
    // Map2Curve preset editor methods
    void createPresetEditorPanel();
    void setupGlobalCommandsUI(QWidget* parent);
    void setupCurveParametersUI(QWidget* parent);
    void updateCurveEditor(); // Update the UI with current curve data
    
    // 辅助方法：构建预设文件内容（在自动执行过程中使用）
    QString buildPresetFileContent() const;
    // 辅助方法：获取适当的目标文件路径（在自动执行过程中使用）
    QString getTargetFilePath(const QString& sourcePath) const;

    bool isExecutingTool() const;
    QString getSelectedMapContent() const;
};

} // namespace tb::ui
