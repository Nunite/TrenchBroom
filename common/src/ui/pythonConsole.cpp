//Added by lws

#include "PythonConsole.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolBar>
#include <QPushButton>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSplitter>
#include <QTextEdit>
#include <QListWidget>
#include <QTreeView>
#include <QDir>
#include <QAction>
#include <QIcon>
#include <QFont>
#include <QFontMetrics>
#include <QApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QSettings>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QProcess>
#include <QTemporaryFile>
#include <QCoreApplication>

namespace tb::ui
{

PythonConsole::PythonConsole(QWidget* parent)
  : TabBookPage{parent}
  , m_mainSplitter{nullptr}
  , m_leftSplitter{nullptr}
  , m_fileTreeView{nullptr}
  , m_fileSystemModel{nullptr}
  , m_scriptListWidget{nullptr}
  , m_codeEditor{nullptr}
  , m_outputConsole{nullptr}
  , m_toolBar{nullptr}
  , m_inputLine{nullptr}
  , m_pythonProcess{nullptr}
  , m_currentScriptPath{}
  , m_scriptsRootDir{}
{
  setupUI();
  setupConnections();
  setupPythonEnvironment();
}

PythonConsole::~PythonConsole()
{
  if (m_pythonProcess) {
    if (m_pythonProcess->state() != QProcess::NotRunning) {
      m_pythonProcess->terminate();
      m_pythonProcess->waitForFinished(1000);
      if (m_pythonProcess->state() != QProcess::NotRunning) {
        m_pythonProcess->kill();
      }
    }
    delete m_pythonProcess;
  }
}

QString PythonConsole::getCurrentScriptContent() const
{
  return m_codeEditor->toPlainText();
}

void PythonConsole::setScriptsRootDirectory(const QString& path)
{
  m_scriptsRootDir = path;
  
  if (m_fileSystemModel) {
    m_fileSystemModel->setRootPath(path);
    m_fileTreeView->setRootIndex(m_fileSystemModel->index(path));
  }
  
  updateScriptsList();
}

QWidget* PythonConsole::createTabBarPage(QWidget* parent)
{
  return new QLabel{tr("Python"), parent};
}

void PythonConsole::onNewScript()
{
  // 检查是否需要保存当前脚本
  if (!m_currentScriptPath.isEmpty() && m_codeEditor->document()->isModified()) {
    QMessageBox::StandardButton result = QMessageBox::question(
      this,
      tr("Save Changes"),
      tr("Do you want to save changes to the current script?"),
      QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel
    );
    
    if (result == QMessageBox::Yes) {
      onSaveScript();
    } else if (result == QMessageBox::Cancel) {
      return;
    }
  }
  
  m_currentScriptPath.clear();
  m_codeEditor->clear();
  m_codeEditor->document()->setModified(false);
}

void PythonConsole::onOpenScript()
{
  QString filePath = QFileDialog::getOpenFileName(
    this,
    tr("Open Python Script"),
    m_scriptsRootDir.isEmpty() ? QDir::homePath() : m_scriptsRootDir,
    tr("Python Files (*.py);;All Files (*.*)")
  );
  
  if (!filePath.isEmpty()) {
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QTextStream in(&file);
      m_codeEditor->setPlainText(in.readAll());
      file.close();
      
      m_currentScriptPath = filePath;
      m_codeEditor->document()->setModified(false);
      
      // 将文件添加到脚本列表
      if (!m_openScripts.contains(filePath)) {
        m_openScripts[filePath] = QFileInfo(filePath).fileName();
        updateScriptsList();
      }
    } else {
      QMessageBox::warning(
        this,
        tr("Error"),
        tr("Could not open file: %1").arg(filePath)
      );
    }
  }
}

void PythonConsole::onSaveScript()
{
  if (m_currentScriptPath.isEmpty()) {
    onSaveScriptAs();
    return;
  }
  
  QFile file(m_currentScriptPath);
  if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QTextStream out(&file);
    out << m_codeEditor->toPlainText();
    file.close();
    
    m_codeEditor->document()->setModified(false);
  } else {
    QMessageBox::warning(
      this,
      tr("Error"),
      tr("Could not save file: %1").arg(m_currentScriptPath)
    );
  }
}

void PythonConsole::onSaveScriptAs()
{
  QString filePath = QFileDialog::getSaveFileName(
    this,
    tr("Save Python Script"),
    m_scriptsRootDir.isEmpty() ? QDir::homePath() : m_scriptsRootDir,
    tr("Python Files (*.py);;All Files (*.*)")
  );
  
  if (!filePath.isEmpty()) {
    if (!filePath.endsWith(".py", Qt::CaseInsensitive)) {
      filePath += ".py";
    }
    
    m_currentScriptPath = filePath;
    onSaveScript();
    
    // 将文件添加到脚本列表
    if (!m_openScripts.contains(filePath)) {
      m_openScripts[filePath] = QFileInfo(filePath).fileName();
      updateScriptsList();
    }
  }
}

void PythonConsole::onRunScript()
{
  if (m_codeEditor->toPlainText().isEmpty()) {
    QMessageBox::warning(
      this,
      tr("Warning"),
      tr("Cannot run an empty script.")
    );
    return;
  }
  
  // 如果有未保存的修改，先保存
  if (m_codeEditor->document()->isModified()) {
    QMessageBox::StandardButton result = QMessageBox::question(
      this,
      tr("Save Changes"),
      tr("Do you want to save changes before running?"),
      QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel
    );
    
    if (result == QMessageBox::Yes) {
      onSaveScript();
    } else if (result == QMessageBox::Cancel) {
      return;
    }
  }
  
  // 确保 Python 进程未在运行
  if (m_pythonProcess && m_pythonProcess->state() != QProcess::NotRunning) {
    QMessageBox::warning(
      this,
      tr("Warning"),
      tr("A script is already running. Please stop it first.")
    );
    return;
  }
  
  // 清空输出控制台
  m_outputConsole->clear();
  
  // 创建 Python 进程（如果尚未创建）
  if (!m_pythonProcess) {
    m_pythonProcess = new QProcess(this);
    connect(m_pythonProcess, &QProcess::readyReadStandardOutput, this, &PythonConsole::onPythonOutputReady);
    connect(m_pythonProcess, &QProcess::readyReadStandardError, this, &PythonConsole::onPythonErrorReady);
    connect(m_pythonProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &PythonConsole::onPythonFinished);
  }
  
  // 如果脚本已保存，直接运行文件；否则，创建临时文件运行
  if (!m_currentScriptPath.isEmpty()) {
    m_pythonProcess->start("python", QStringList() << m_currentScriptPath);
  } else {
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(false);
    
    if (tempFile.open()) {
      QTextStream out(&tempFile);
      out << m_codeEditor->toPlainText();
      QString tempFilePath = tempFile.fileName();
      tempFile.close();
      
      m_pythonProcess->start("python", QStringList() << tempFilePath);
    } else {
      QMessageBox::warning(
        this,
        tr("Error"),
        tr("Could not create temporary file for script execution.")
      );
    }
  }
}

void PythonConsole::onStopScript()
{
  if (m_pythonProcess && m_pythonProcess->state() != QProcess::NotRunning) {
    m_pythonProcess->terminate();
    
    // 给一点时间让进程正常终止
    if (!m_pythonProcess->waitForFinished(2000)) {
      m_pythonProcess->kill();
    }
    
    m_outputConsole->append(tr("[Process terminated by user]"));
  }
}

void PythonConsole::onFileSelected(const QModelIndex& index)
{
  if (!m_fileSystemModel) {
    return;
  }
  
  QString filePath = m_fileSystemModel->filePath(index);
  if (QFileInfo(filePath).isFile() && filePath.endsWith(".py", Qt::CaseInsensitive)) {
    // 检查是否需要保存当前脚本
    if (!m_currentScriptPath.isEmpty() && m_codeEditor->document()->isModified()) {
      QMessageBox::StandardButton result = QMessageBox::question(
        this,
        tr("Save Changes"),
        tr("Do you want to save changes to the current script?"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel
      );
      
      if (result == QMessageBox::Yes) {
        onSaveScript();
      } else if (result == QMessageBox::Cancel) {
        return;
      }
    }
    
    // 打开选定的文件
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QTextStream in(&file);
      m_codeEditor->setPlainText(in.readAll());
      file.close();
      
      m_currentScriptPath = filePath;
      m_codeEditor->document()->setModified(false);
      
      // 将文件添加到脚本列表
      if (!m_openScripts.contains(filePath)) {
        m_openScripts[filePath] = QFileInfo(filePath).fileName();
        updateScriptsList();
      }
    }
  }
}

void PythonConsole::onScriptSelected(int row)
{
  if (row < 0 || row >= m_scriptListWidget->count()) {
    return;
  }
  
  QListWidgetItem* item = m_scriptListWidget->item(row);
  QString filePath = item->data(Qt::UserRole).toString();
  
  // 检查是否需要保存当前脚本
  if (!m_currentScriptPath.isEmpty() && m_codeEditor->document()->isModified()) {
    QMessageBox::StandardButton result = QMessageBox::question(
      this,
      tr("Save Changes"),
      tr("Do you want to save changes to the current script?"),
      QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel
    );
    
    if (result == QMessageBox::Yes) {
      onSaveScript();
    } else if (result == QMessageBox::Cancel) {
      return;
    }
  }
  
  // 打开选定的文件
  QFile file(filePath);
  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&file);
    m_codeEditor->setPlainText(in.readAll());
    file.close();
    
    m_currentScriptPath = filePath;
    m_codeEditor->document()->setModified(false);
  }
}

void PythonConsole::onPythonOutputReady()
{
  if (!m_pythonProcess) {
    return;
  }
  
  QByteArray output = m_pythonProcess->readAllStandardOutput();
  m_outputConsole->append(QString::fromUtf8(output));
}

void PythonConsole::onPythonErrorReady()
{
  if (!m_pythonProcess) {
    return;
  }
  
  QByteArray error = m_pythonProcess->readAllStandardError();
  m_outputConsole->append(QString("<span style=\"color:red;\">%1</span>").arg(QString::fromUtf8(error)));
}

void PythonConsole::onPythonFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
  QString message = exitStatus == QProcess::NormalExit
    ? tr("[Process finished with exit code: %1]").arg(exitCode)
    : tr("[Process crashed]");
  
  m_outputConsole->append(message);
}

void PythonConsole::onCommandEntered()
{
  QString command = m_inputLine->text().trimmed();
  if (command.isEmpty()) {
    return;
  }
  
  m_inputLine->clear();
  executeCommand(command);
}

void PythonConsole::setupUI()
{
  // 创建主布局
  auto* mainLayout = new QVBoxLayout{};
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);
  
  // 创建工具栏
  m_toolBar = new QToolBar{this};
  
  // 添加文件操作按钮 - 不使用局部变量来存储这些操作
  m_toolBar->addAction(tr("New"));     // newAction
  m_toolBar->addAction(tr("Open"));    // openAction
  m_toolBar->addAction(tr("Save"));    // saveAction
  m_toolBar->addAction(tr("Save As")); // saveAsAction
  
  m_toolBar->addSeparator();
  
  // 添加执行按钮 - 不使用局部变量来存储这些操作
  m_toolBar->addAction(tr("Run"));     // runAction
  m_toolBar->addAction(tr("Stop"));    // stopAction
  
  // 设置按钮图标（实际应用中应使用真实图标）
  // m_toolBar->actions()[0]->setIcon(QIcon(":/icons/new.png"));
  // m_toolBar->actions()[1]->setIcon(QIcon(":/icons/open.png"));
  // 其他按钮图标设置...
  
  mainLayout->addWidget(m_toolBar);
  
  // 创建主分割器
  m_mainSplitter = new QSplitter{Qt::Horizontal, this};
  
  // 创建左侧分割器（文件浏览器和脚本列表）
  m_leftSplitter = new QSplitter{Qt::Vertical, m_mainSplitter};
  
  // 创建文件浏览器
  m_fileTreeView = new QTreeView{m_leftSplitter};
  m_fileSystemModel = new QFileSystemModel{m_fileTreeView};
  m_fileSystemModel->setNameFilters(QStringList() << "*.py");
  m_fileSystemModel->setNameFilterDisables(false);
  
  // 设置脚本根目录为 app/resources/Scripts 目录
  if (m_scriptsRootDir.isEmpty()) {
    // 使用资源目录下的 Scripts 文件夹
    QString scriptsDir = QCoreApplication::applicationDirPath() + "/Scripts";
    
    // 确保目录存在
    QDir dir(scriptsDir);
    if (!dir.exists()) {
      dir.mkpath(".");
    }
    
    m_scriptsRootDir = scriptsDir;
  }
  
  m_fileSystemModel->setRootPath(m_scriptsRootDir);
  m_fileTreeView->setModel(m_fileSystemModel);
  m_fileTreeView->setRootIndex(m_fileSystemModel->index(m_scriptsRootDir));
  
  // 只显示文件名列
  for (int i = 1; i < m_fileSystemModel->columnCount(); ++i) {
    m_fileTreeView->hideColumn(i);
  }
  
  // 创建脚本列表
  m_scriptListWidget = new QListWidget{m_leftSplitter};
  m_scriptListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
  
  // 创建右侧分割器（代码编辑器和输出控制台）
  auto* rightSplitter = new QSplitter{Qt::Vertical, m_mainSplitter};
  
  // 创建代码编辑器
  m_codeEditor = new QTextEdit{rightSplitter};
  
  // 使用等宽字体
  QFont codeFont = QFont("Courier New", 10);
  codeFont.setFixedPitch(true);
  m_codeEditor->setFont(codeFont);
  
  // 创建输出控制台区域
  auto* consoleContainer = new QWidget{rightSplitter};
  auto* consoleLayout = new QVBoxLayout{consoleContainer};
  consoleLayout->setContentsMargins(0, 0, 0, 0);
  
  m_outputConsole = new QTextEdit{consoleContainer};
  m_outputConsole->setReadOnly(true);
  
  // 为输出控制台设置深色背景和浅色文本
  QPalette p = m_outputConsole->palette();
  p.setColor(QPalette::Base, QColor(40, 40, 40));
  p.setColor(QPalette::Text, QColor(220, 220, 220));
  m_outputConsole->setPalette(p);
  
  // 创建命令输入行
  m_inputLine = new QLineEdit{consoleContainer};
  m_inputLine->setPlaceholderText(tr("Enter Python command..."));
  
  consoleLayout->addWidget(m_outputConsole);
  consoleLayout->addWidget(m_inputLine);
  
  // 设置分割器初始大小
  m_leftSplitter->setStretchFactor(0, 2); // 文件浏览器占 2/3
  m_leftSplitter->setStretchFactor(1, 1); // 脚本列表占 1/3
  
  rightSplitter->setStretchFactor(0, 2); // 代码编辑器占 2/3
  rightSplitter->setStretchFactor(1, 1); // 输出控制台占 1/3
  
  m_mainSplitter->setStretchFactor(0, 1); // 左侧区域占 1/4
  m_mainSplitter->setStretchFactor(1, 3); // 右侧区域占 3/4
  
  mainLayout->addWidget(m_mainSplitter);
  setLayout(mainLayout);
}

void PythonConsole::setupConnections()
{
  // 工具栏按钮连接
  connect(m_toolBar->actions()[0], &QAction::triggered, this, &PythonConsole::onNewScript);
  connect(m_toolBar->actions()[1], &QAction::triggered, this, &PythonConsole::onOpenScript);
  connect(m_toolBar->actions()[2], &QAction::triggered, this, &PythonConsole::onSaveScript);
  connect(m_toolBar->actions()[3], &QAction::triggered, this, &PythonConsole::onSaveScriptAs);
  connect(m_toolBar->actions()[5], &QAction::triggered, this, &PythonConsole::onRunScript);
  connect(m_toolBar->actions()[6], &QAction::triggered, this, &PythonConsole::onStopScript);
  
  // 文件浏览器连接
  connect(m_fileTreeView, &QTreeView::doubleClicked, this, &PythonConsole::onFileSelected);
  
  // 脚本列表连接
  connect(m_scriptListWidget, &QListWidget::currentRowChanged, this, &PythonConsole::onScriptSelected);
  
  // 命令输入行连接
  connect(m_inputLine, &QLineEdit::returnPressed, this, &PythonConsole::onCommandEntered);
}

void PythonConsole::setupPythonEnvironment()
{
  // 检查 Python 是否可用
  QProcess pythonCheck;
  pythonCheck.start("python", QStringList() << "--version");
  
  if (!pythonCheck.waitForStarted(1000) || !pythonCheck.waitForFinished(2000)) {
    m_outputConsole->append(tr("<span style=\"color:red;\">Error: Could not start Python. Please ensure Python is installed and available in the system PATH.</span>"));
    return;
  }
  
  QString pythonVersion = QString::fromUtf8(pythonCheck.readAllStandardOutput()).trimmed();
  if (pythonVersion.isEmpty()) {
    pythonVersion = QString::fromUtf8(pythonCheck.readAllStandardError()).trimmed();
  }
  
  if (!pythonVersion.isEmpty()) {
    m_outputConsole->append(tr("Using %1").arg(pythonVersion));
  } else {
    m_outputConsole->append(tr("Python detected, but version could not be determined."));
  }
  
  m_outputConsole->append(tr("Python console ready."));
}

void PythonConsole::executeCommand(const QString& command)
{
  m_outputConsole->append(QString("<span style=\"color:green;\">>>> %1</span>").arg(command));
  
  // 如果 Python 进程未运行，启动一个交互式会话
  if (!m_pythonProcess || m_pythonProcess->state() == QProcess::NotRunning) {
    if (m_pythonProcess) {
      delete m_pythonProcess;
    }
    
    m_pythonProcess = new QProcess(this);
    connect(m_pythonProcess, &QProcess::readyReadStandardOutput, this, &PythonConsole::onPythonOutputReady);
    connect(m_pythonProcess, &QProcess::readyReadStandardError, this, &PythonConsole::onPythonErrorReady);
    connect(m_pythonProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &PythonConsole::onPythonFinished);
    
    // 启动 Python 解释器
    m_pythonProcess->start("python", QStringList() << "-c" << command);
  } else {
    // 向现有进程写入命令
    m_pythonProcess->write((command + "\n").toUtf8());
  }
}

void PythonConsole::updateScriptsList()
{
  m_scriptListWidget->clear();
  
  for (auto it = m_openScripts.begin(); it != m_openScripts.end(); ++it) {
    QListWidgetItem* item = new QListWidgetItem(it.value(), m_scriptListWidget);
    item->setData(Qt::UserRole, it.key());
  }
  
  // 如果当前脚本在列表中，选中它
  if (!m_currentScriptPath.isEmpty()) {
    for (int i = 0; i < m_scriptListWidget->count(); ++i) {
      QListWidgetItem* item = m_scriptListWidget->item(i);
      if (item->data(Qt::UserRole).toString() == m_currentScriptPath) {
        m_scriptListWidget->setCurrentRow(i);
        break;
      }
    }
  }
}

} // namespace tb::ui
