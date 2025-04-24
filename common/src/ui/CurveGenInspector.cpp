//Added by Lws

#include "CurveGenInspector.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QLabel>
#include <QProcess>
#include <QMessageBox>
#include <QTextEdit>
#include <QFont>
#include <QTextCursor>
#include <QApplication>
#include <exception>  // Standard exception library
#include <QTimer>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QComboBox>

#include "ui/MapDocument.h"
#include "ui/QtUtils.h"
#include "ui/Splitter.h"
#include "ui/Selection.h" // Ensure this is included
#include "ui/CompilationVariables.h" // Include CompilationVariables

namespace tb::ui
{

CurveGenInspector::CurveGenInspector(
  std::weak_ptr<MapDocument> document, GLContextManager& contextManager, QWidget* parent)
  : TabBookPage{parent}
  , m_document{document} // Added
  , m_process{nullptr}
{
  createGui(document, contextManager);
}

void CurveGenInspector::createGui(
  std::weak_ptr<MapDocument> document, GLContextManager& contextManager)
{
  m_splitter = new Splitter{Qt::Vertical};
  m_splitter->setObjectName("CurveGenInspector");

  // Original display area - for showing selected content
  m_selectionView = new QTextEdit{};
  m_selectionView->setReadOnly(true);
  m_splitter->addWidget(m_selectionView);

  // Create a collapsible panel labeled "External Tools"
  auto* toolsContainer = new QWidget{};
  toolsContainer->setMinimumHeight(150);  // Set minimum height
  
  auto* toolsContainerLayout = new QVBoxLayout{};
  toolsContainerLayout->setContentsMargins(0, 0, 0, 0);
  
  auto* toolHeader = new QWidget{};
  auto* toolHeaderLayout = new QHBoxLayout{};
  toolHeaderLayout->setContentsMargins(5, 5, 5, 5);
  
  auto* toolTitle = new QLabel{tr("External Tool Executor")};
  QFont titleFont = toolTitle->font();
  titleFont.setBold(true);
  toolTitle->setFont(titleFont);
  
  toolHeaderLayout->addWidget(toolTitle);
  toolHeaderLayout->addStretch(1);
  toolHeader->setLayout(toolHeaderLayout);
  
  // Add tool execution panel
  createToolExecutionPanel();
  
  toolsContainerLayout->addWidget(toolHeader);
  toolsContainerLayout->addWidget(m_toolPanel);
  toolsContainer->setLayout(toolsContainerLayout);
  
  m_splitter->addWidget(toolsContainer);

  // Tool output area
  m_contentView = new QTextEdit{};
  m_contentView->setReadOnly(true);
  m_contentView->setPlaceholderText(tr("Tool execution output will be displayed here"));
  m_splitter->addWidget(m_contentView);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(m_splitter, 1);
  setLayout(layout);
  
  // Set reasonable initial size proportions for the splitter
  m_splitter->setStretchFactor(0, 3); // Selection content area takes more space
  m_splitter->setStretchFactor(1, 1); // Tool panel takes less space
  m_splitter->setStretchFactor(2, 2); // Output area takes medium space

  // Setup parameter templates
  setupParameterTemplates();

  restoreWindowState(m_splitter);
}

void CurveGenInspector::setupParameterTemplates()
{
  // Only setup if the combo box exists
  if (!m_paramTemplatesCombo)
    return;
    
  // Clear existing items
  m_paramTemplatesCombo->clear();
  
  // Add default prompt item
  m_paramTemplatesCombo->addItem(tr("Add Template..."));
  
  // Add custom parameter for selected object
  m_paramTemplatesCombo->addItem(tr("Selected Object"), "${SELECTED_OBJECT}");
  m_paramTemplatesCombo->addItem(tr("Generate Map"), "${GENERATE_MAP}");
  m_paramTemplatesCombo->addItem(tr("Generate Rmf"), "${GENERATE_RMF}");
  
  // Connect signal to slot
  connect(m_paramTemplatesCombo, QOverload<int>::of(&QComboBox::activated),
          this, &CurveGenInspector::insertParameterTemplate);
}

void CurveGenInspector::insertParameterTemplate(int index)
{
  // Skip the first item (prompt)
  if (index <= 0 || !m_toolArgsEdit)
    return;
    
  // Get the template string from the item data
  QString templateString = m_paramTemplatesCombo->itemData(index).toString();
  
  // Insert at current cursor position or append if no selection
  m_toolArgsEdit->insert(templateString);
  
  // Reset combo box to first item
  m_paramTemplatesCombo->setCurrentIndex(0);
  
  // Set focus back to the args edit
  m_toolArgsEdit->setFocus();
}

void CurveGenInspector::createToolExecutionPanel()
{
  m_toolPanel = new QWidget{};
  
  // Create form layout
  auto* gridLayout = new QGridLayout{};
  gridLayout->setContentsMargins(10, 10, 10, 10);
  gridLayout->setSpacing(5);
  
  // Tool path selection
  auto* toolPathLabel = new QLabel{tr("Tool Path:")};
  m_toolPathEdit = new QLineEdit{};
  m_browseButton = new QPushButton{tr("Browse...")};
  
  // Parameter input
  auto* argsLabel = new QLabel{tr("Parameters:")};
  m_toolArgsEdit = new QLineEdit{};
  
  // Parameter templates combo box
  m_paramTemplatesCombo = new QComboBox{};
  m_paramTemplatesCombo->setToolTip(tr("Select to insert parameter template"));
  
  // Execute button
  m_executeButton = new QPushButton{tr("Execute")};
  
  // Terminate button
  m_terminateButton = new QPushButton{tr("Terminate")};
  m_terminateButton->setEnabled(false);  // Initially disabled
  m_terminateButton->setStyleSheet("QPushButton { color: red; font-weight: bold; }");
  
  // Add to layout
  gridLayout->addWidget(toolPathLabel, 0, 0);
  gridLayout->addWidget(m_toolPathEdit, 0, 1);
  gridLayout->addWidget(m_browseButton, 0, 2);
  
  gridLayout->addWidget(argsLabel, 1, 0);
  gridLayout->addWidget(m_toolArgsEdit, 1, 1);
  gridLayout->addWidget(m_paramTemplatesCombo, 1, 2);
  
  auto* buttonLayout = new QHBoxLayout{};
  buttonLayout->addStretch(1);
  buttonLayout->addWidget(m_executeButton);
  buttonLayout->addWidget(m_terminateButton);
  
  auto* mainLayout = new QVBoxLayout{};
  mainLayout->addLayout(gridLayout);
  mainLayout->addLayout(buttonLayout);
  
  m_toolPanel->setLayout(mainLayout);
  
  // Connect signals and slots
  connect(m_browseButton, &QPushButton::clicked, this, &CurveGenInspector::browseToolPath);
  connect(m_executeButton, &QPushButton::clicked, this, &CurveGenInspector::executeExternalTool);
  connect(m_terminateButton, &QPushButton::clicked, this, &CurveGenInspector::terminateExternalTool);
}

void CurveGenInspector::browseToolPath()
{
  QString filePath = QFileDialog::getOpenFileName(
    this, 
    tr("Select External Tool"), 
    QString{}, 
    tr("Executable Files (*.exe);;All Files (*.*)"));
    
  if (!filePath.isEmpty()) {
    m_toolPathEdit->setText(filePath);
  }
}

void CurveGenInspector::executeExternalTool()
{
  QString toolPath = m_toolPathEdit->text().trimmed();
  if (toolPath.isEmpty()) {
    QMessageBox::warning(this, tr("Error"), tr("Please specify tool path"));
    return;
  }
  
  // Ensure output area is visible
  m_contentView->show();
  
  // If a process is already running, clean up first
  if (m_process) {
    if (m_process->state() != QProcess::NotRunning) {
      m_process->kill();
      m_process->waitForFinished(500);
    }
    delete m_process;
    m_process = nullptr;
  }
  
  // Clear output area first
  m_contentView->clear();
  
  // Get working directory from the tool path
  QFileInfo fileInfo(toolPath);
  QString workDir = fileInfo.absolutePath();
  
  // Get parameters
  QString rawArgs = m_toolArgsEdit->text().trimmed();
  
  // Prepare argument list properly - don't use simple string splitting
  QStringList argsList;
  QString tempFilePath;
  
  // Check if we need to handle the selected object
  if (rawArgs.contains("${SELECTED_OBJECT}")) {
    // Get the current selection content
    QString selectionContent = getCurrentSelectionContent();
    
    // Create a M2C_Temp directory in the working directory
    QString tempDirPath = workDir + "/M2C_Temp";
    QDir tempDir(tempDirPath);
    
    // Create the directory if it doesn't exist
    if (!tempDir.exists()) {
      if (!tempDir.mkpath(".")) {
        m_contentView->append(tr("[ERROR] Failed to create temp directory: %1").arg(tempDirPath));
        return;
      }
      m_contentView->append(tr("Created temp directory: %1").arg(tempDirPath));
    }
    
    // Fixed file path for the selection data
    tempFilePath = tempDirPath + "/TempMap.map";
    QFile tempFile(tempFilePath);
    
    if (tempFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QTextStream out(&tempFile);
      out << selectionContent;
      tempFile.close();
      
      m_contentView->append(tr("Created selection data file: %1").arg(tempFilePath));
      
      // Store the temp file path for reference
      m_lastTempFile = tempFilePath;
      
      // If args is just the placeholder, directly use the file path
      if (rawArgs.trimmed() == "${SELECTED_OBJECT}") {
        argsList << tempFilePath;
      } else {
        // Otherwise, need to handle multiple arguments
        // Replace the placeholder with the path (without quotes)
        rawArgs.replace("${SELECTED_OBJECT}", tempFilePath);
        
        // Split the arguments properly
        bool inQuotes = false;
        QString currentArg;
        
        for (int i = 0; i < rawArgs.length(); i++) {
          QChar c = rawArgs[i];
          
          if (c == '"') {
            inQuotes = !inQuotes;
            currentArg += c; // Keep the quotes in the argument
          } else if (c == ' ' && !inQuotes) {
            // Space outside quotes - end of argument
            if (!currentArg.isEmpty()) {
              argsList << currentArg;
              currentArg.clear();
            }
          } else {
            currentArg += c;
          }
        }
        
        // Add the last argument if any
        if (!currentArg.isEmpty()) {
          argsList << currentArg;
        }
      }
    } else {
      m_contentView->append(tr("[ERROR] Failed to create selection data file"));
      return;
    }
  } else {
    // No template to replace, just parse the arguments normally
    if (!rawArgs.isEmpty()) {
      // Parse arguments properly, respecting quotes
      bool inQuotes = false;
      QString currentArg;
      
      for (int i = 0; i < rawArgs.length(); i++) {
        QChar c = rawArgs[i];
        
        if (c == '"') {
          inQuotes = !inQuotes;
          currentArg += c; // Keep the quotes in the argument
        } else if (c == ' ' && !inQuotes) {
          // Space outside quotes - end of argument
          if (!currentArg.isEmpty()) {
            argsList << currentArg;
            currentArg.clear();
          }
        } else {
          currentArg += c;
        }
      }
      
      // Add the last argument if any
      if (!currentArg.isEmpty()) {
        argsList << currentArg;
      }
    }
  }
  
  // Add header
  m_contentView->append(tr("===== Starting Execution of %1 =====").arg(toolPath));
  
  // Log the actual arguments
  m_contentView->append(tr("Arguments:"));
  for (const QString& arg : argsList) {
    m_contentView->append(tr("  %1").arg(arg));
  }
  
  // Toggle button states
  m_executeButton->setEnabled(false);  // Disable execute button
  m_terminateButton->setEnabled(true); // Enable terminate button
  
  m_contentView->append(tr("Working Directory: %1").arg(workDir));
  
  // Create new process
  m_process = new QProcess(this);
  m_process->setProcessChannelMode(QProcess::MergedChannels);
  m_process->setWorkingDirectory(workDir);
  
  // Connect process signals
  connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this, &CurveGenInspector::processFinished);
  connect(m_process, &QProcess::readyReadStandardOutput, 
          this, &CurveGenInspector::readProcessOutput);
  connect(m_process, &QProcess::readyReadStandardError, 
          this, &CurveGenInspector::readProcessOutput);
  connect(m_process, &QProcess::errorOccurred,
          this, &CurveGenInspector::processError);
  
#ifdef Q_OS_WIN
  try {
    // Start process and keep standard input open
    m_process->setInputChannelMode(QProcess::ManagedInputChannel);
    
    // Build the command line string for display
    QString cmdLine = toolPath;
    for (const QString& arg : argsList) {
      cmdLine += " " + arg;
    }
    m_contentView->append(tr("Executing command: %1").arg(cmdLine));
    
    // Actually start the process with the arguments list
    m_process->start(toolPath, argsList);
    
    if (!m_process->waitForStarted(3000)) {
      m_contentView->append(tr("[ERROR] Process start timed out"));
      m_executeButton->setEnabled(true);
      m_terminateButton->setEnabled(false);
    } else {
      // Start a timer to monitor output
      QTimer* checkTimer = new QTimer(this);
      connect(checkTimer, &QTimer::timeout, this, [this, checkTimer]() {
        if (m_process && m_process->state() == QProcess::Running) {
          QString output = m_contentView->toPlainText();
          
          // Check if output contains "Press any key" or similar prompts
          if (output.contains("Press any key") ||
              output.contains("press any key") ||
              output.contains("请按任意键继续")) {
            
            m_contentView->append(tr("Key prompt detected, sending Enter key..."));
            
            // Send Enter key
            m_process->write("\n");
            
            // Cancel timer to avoid repeated sending
            checkTimer->stop();
            checkTimer->deleteLater();
          }
        } else {
          // Process has ended, stop timer
          checkTimer->stop();
          checkTimer->deleteLater();
        }
      });
      
      // Check every 500 milliseconds
      checkTimer->start(500);
    }
  } catch (const std::exception& e) {
    m_contentView->append(tr("[CRITICAL ERROR] Process start exception: %1").arg(e.what()));
    m_executeButton->setEnabled(true);
    m_terminateButton->setEnabled(false);
  }
#else
  try {
    // Build the command line string for display
    QString cmdLine = toolPath;
    for (const QString& arg : argsList) {
      cmdLine += " " + arg;
    }
    m_contentView->append(tr("Executing command: %1").arg(cmdLine));
    
    // Actually start the process with the arguments list
    m_process->start(toolPath, argsList);
    
    if (!m_process->waitForStarted(3000)) {
      m_contentView->append(tr("[ERROR] Process start timed out"));
      m_executeButton->setEnabled(true);
      m_terminateButton->setEnabled(false);
    }
  } catch (const std::exception& e) {
    m_contentView->append(tr("[CRITICAL ERROR] Process start exception: %1").arg(e.what()));
    m_executeButton->setEnabled(true);
    m_terminateButton->setEnabled(false);
  }
#endif
}

void CurveGenInspector::processFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
  m_contentView->append(tr("\n===== Process ended, exit code: %1 =====").arg(exitCode));
  
  if (exitStatus == QProcess::CrashExit) {
    m_contentView->append(tr("Process terminated abnormally"));
  }
  
  // Note: We no longer delete the selection data file as it's now stored in a fixed location

  // Find and delete possible temporary script files
  QDir tempDir(QDir::tempPath());
  QStringList nameFilters;
  
#ifdef Q_OS_WIN
  nameFilters << "tb_execute_*.bat";
#elif defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
  nameFilters << "tb_execute_*.sh";
#else
  nameFilters << "tb_execute_*";
#endif

  QStringList scriptFiles = tempDir.entryList(nameFilters, QDir::Files);
  
  for (const QString& file : scriptFiles) {
    // Only delete temporary files created more than 30 minutes ago
    QFileInfo fileInfo(QDir::tempPath() + "/" + file);
    
    // Use Qt 5 compatible way to check file time
    QDateTime fileTime = fileInfo.lastModified(); // Use modification time instead of creation time
    if (fileTime.secsTo(QDateTime::currentDateTime()) > 1800) {
      if (QFile::remove(fileInfo.absoluteFilePath())) {
        m_contentView->append(tr("Cleaned up temporary script: %1").arg(fileInfo.fileName()));
      }
    }
  }
  
  // Reset button states
  m_executeButton->setEnabled(true);
  m_terminateButton->setEnabled(false);
}

void CurveGenInspector::readProcessOutput()
{
  if (!m_process) return;
  
  // Read standard output
  QByteArray outputData = m_process->readAllStandardOutput();
  if (!outputData.isEmpty()) {
    QString output = QString::fromLocal8Bit(outputData);
    m_contentView->append(output);
    
    // Check if output contains key prompt
    if (output.contains("Press any key") ||
        output.contains("press any key") ||
        output.contains("请按任意键继续")) {
      
      m_contentView->append(tr("Key prompt detected, sending Enter key..."));
      
      // Send Enter key
      m_process->write("\n");
    }
    
    // Ensure scrolling to bottom to show latest output
    QTextCursor cursor = m_contentView->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_contentView->setTextCursor(cursor);
    m_contentView->ensureCursorVisible();
  }
  
  // Read standard error
  QByteArray errorData = m_process->readAllStandardError();
  if (!errorData.isEmpty()) {
    QString errorOutput = tr("[ERROR] ") + QString::fromLocal8Bit(errorData);
    m_contentView->append(errorOutput);
    // Ensure scrolling to bottom to show latest output
    QTextCursor cursor = m_contentView->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_contentView->setTextCursor(cursor);
    m_contentView->ensureCursorVisible();
  }
  
  // Force event processing to ensure UI updates
  QApplication::processEvents();
}

void CurveGenInspector::processError(QProcess::ProcessError error)
{
  QString errorMessage;
  
  switch (error) {
    case QProcess::FailedToStart:
      errorMessage = tr("Failed to start process. Please check if the tool path is correct and if you have execution permissions.");
      break;
    case QProcess::Crashed:
      errorMessage = tr("Process crashed.");
      break;
    case QProcess::Timedout:
      errorMessage = tr("Process operation timed out.");
      break;
    case QProcess::WriteError:
      errorMessage = tr("Error writing data to process.");
      break;
    case QProcess::ReadError:
      errorMessage = tr("Error reading data from process.");
      break;
    default:
      errorMessage = tr("Unknown error.");
      break;
  }
  
  m_contentView->append(tr("\n[ERROR] ") + errorMessage);
  m_executeButton->setEnabled(true);
}

void CurveGenInspector::terminateExternalTool()
{
  if (!m_process || m_process->state() == QProcess::NotRunning) {
    m_terminateButton->setEnabled(false);
    return;
  }
  
  m_contentView->append(tr("\n===== Terminating process... ====="));
  
  // Record process ID for use if standard method fails
  qint64 pid = m_process->processId();
  
  // First try normal termination
  m_process->terminate();
  
  // Wait for process to end, wait at most 2 seconds
  if (!m_process->waitForFinished(2000)) {
    // If after 2 seconds the process is still running, force kill
    m_contentView->append(tr("[WARNING] Process not responding to termination request, forcing termination..."));
    m_process->kill();
    
    // Wait another second
    if (!m_process->waitForFinished(1000) && pid > 0) {
      // If still unable to terminate, try using system commands to force terminate
      m_contentView->append(tr("[WARNING] Process still not terminated, trying system commands to force terminate..."));
      
#ifdef Q_OS_WIN
      // Windows platform uses taskkill command
      QProcess::execute(QString("taskkill /F /PID %1").arg(pid));
#else
      // Unix platform uses kill command
      QProcess::execute(QString("kill -9 %1").arg(pid));
#endif
    }
  }
  
  m_contentView->append(tr("===== Process terminated ====="));
  
  // Delete process object and set to null to prevent subsequent access
  if (m_process) {
    m_process->disconnect(); // Disconnect all connections
    delete m_process;
    m_process = nullptr;
  }
  
  // Re-enable execute button, disable terminate button
  m_executeButton->setEnabled(true);
  m_terminateButton->setEnabled(false);
}

void CurveGenInspector::updateSelectionContent(const Selection& selection)
{
    auto doc = m_document.lock();
    if (!doc || !m_selectionView) return; // Prevent null pointer

    QString content;
    if (doc->hasSelectedNodes()) {
        content = QString::fromStdString(doc->serializeSelectedNodes());
    } else if (doc->hasSelectedBrushFaces()) {
        content = QString::fromStdString(doc->serializeSelectedBrushFaces());
    } else {
        content = tr("No selected content");
    }
    
    // Store the selection content for use with external tools
    m_currentSelectionContent = content;
    
    // Use separate text control to display selected content
    m_selectionView->setPlainText(content);
}

QString CurveGenInspector::getCurrentSelectionContent() const
{
    return m_currentSelectionContent;
}

CurveGenInspector::~CurveGenInspector()
{
  // Clean up process and timer
  if (m_process) {
    // First try normal closing
    if (m_process->state() != QProcess::NotRunning) {
      m_process->terminate();
      if (!m_process->waitForFinished(1000)) {
        // If unable to terminate normally, force terminate
        qint64 pid = m_process->processId();
        m_process->kill();
        
        // If still unable to terminate, use system commands
        if (!m_process->waitForFinished(500) && pid > 0) {
#ifdef Q_OS_WIN
          QProcess::execute(QString("taskkill /F /PID %1").arg(pid));
#else
          QProcess::execute(QString("kill -9 %1").arg(pid));
#endif
        }
      }
    }
    
    m_process->disconnect();
    delete m_process;
    m_process = nullptr;
  }
  
  saveWindowState(m_splitter);
}

} // namespace tb::ui
