//Added by lws

#include "PythonConsole.h"
#include "python_interpreter/pythoninterpreter.h"

#include <QWidget>
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
#include <QProcessEnvironment>
#include <stdexcept> // For std::exception
#include <QFileInfo> // 添加 QFileInfo
#include <QScrollBar> // 添加 QScrollBar

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
  , m_interpreter{nullptr}
  , m_currentScriptPath{}
  , m_scriptsRootDir{}
{
  setupUI();
  setupConnections();
  setupPythonEnvironment();
}

PythonConsole::~PythonConsole()
{
  // unique_ptr will handle deletion
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
  // if (!m_interpreter) { // 注释掉检查
  //     appendOutput(tr("Python interpreter is not initialized."), true);
  //     return;
  // }

  QString scriptContent = m_codeEditor->toPlainText();
  if (scriptContent.isEmpty()) {
    appendOutput(tr("Cannot run an empty script."), true);
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
      // 检查保存是否成功（例如，用户可能取消了保存对话框）
      if (m_codeEditor->document()->isModified() && m_currentScriptPath.isEmpty()) {
          appendOutput(tr("Script must be saved before running without a temporary file (or implement temporary file execution)."), true);
          return;
      }
    } else if (result == QMessageBox::Cancel) {
      return;
    }
    // 如果选 No，继续执行（可能使用未保存的内容或上次保存的版本）
  }
  
  appendOutput(tr("Running script..."));

  std::optional<std::string> error = "Python execution temporarily disabled"; // 模拟错误
  // if (!m_currentScriptPath.isEmpty() && !m_codeEditor->document()->isModified()) { // 注释掉 Python 调用
      // 运行已保存且未修改的文件
      // error = m_interpreter->executeFile(m_currentScriptPath.toStdString());
  // } else {
      // 运行编辑器中的代码（可能是新脚本、未保存的修改或选择不保存就运行）
      // error = m_interpreter->executeCode(scriptContent.toStdString());
  // }

  if (error) {
    appendOutput(tr("Error: %1").arg(QString::fromStdString(*error)), true);
  } else {
    appendOutput(tr("Script execution finished."));
  }
  
  // **注意:** Python 的 print 输出目前不会显示在这里
  appendOutput(tr("(Note: Python print output currently goes to the system console, not this window.)"), false);
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

void PythonConsole::onCommandEntered()
{
  QString command = m_inputLine->text().trimmed();
  if (command.isEmpty()) {
    return;
  }
  m_inputLine->clear();
  
  // if (!m_interpreter) { // 注释掉检查
  //   appendOutput(tr("Python interpreter is not initialized."), true);
  //   return;
  // }

  appendOutput(QString(">>> %1").arg(command)); // 显示输入的命令
  auto error = std::optional<std::string>{"Python execution temporarily disabled"}; // 模拟错误
  // auto error = m_interpreter->executeCode(command.toStdString()); // 注释掉 Python 调用

  if (error) {
    appendOutput(tr("Error: %1").arg(QString::fromStdString(*error)), true);
  } 
  // **注意:** 命令的输出 (print 结果) 目前不会显示在这里
  appendOutput(tr("(Note: Command output currently goes to the system console.)"), false); 
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
  connect(m_toolBar->actions()[4], &QAction::triggered, this, &PythonConsole::onRunScript);
  
  // 文件浏览器连接
  connect(m_fileTreeView, &QTreeView::doubleClicked, this, &PythonConsole::onFileSelected);
  
  // 脚本列表连接
  connect(m_scriptListWidget, &QListWidget::currentRowChanged, this, &PythonConsole::onScriptSelected);
  
  // 命令输入行连接
  connect(m_inputLine, &QLineEdit::returnPressed, this, &PythonConsole::onCommandEntered);
}

void PythonConsole::setupPythonEnvironment()
{
  try {
      appendOutput(tr("Initializing embedded Python interpreter...")); 
      
      // 获取可执行文件路径和脚本根目录
      QString exePathQStr = QCoreApplication::applicationDirPath();
      ::std::string exePath = exePathQStr.toStdString();
      if (m_scriptsRootDir.isEmpty()) {
            QString scriptsDir = QCoreApplication::applicationDirPath() + "/Scripts";
            QDir dir(scriptsDir);
            if (!dir.exists()) {
                if (!dir.mkpath(".")) {
                    throw std::runtime_error("Could not create default scripts directory: " + scriptsDir.toStdString());
                }
            }
            m_scriptsRootDir = scriptsDir;
             if (m_fileSystemModel) {
                 m_fileSystemModel->setRootPath(m_scriptsRootDir);
                 m_fileTreeView->setRootIndex(m_fileSystemModel->index(m_scriptsRootDir));
             }
      }
      ::std::string scriptsRootDirStd = m_scriptsRootDir.toStdString();
      ::std::vector<::std::string> externalSearchPaths;
      externalSearchPaths.push_back(scriptsRootDirStd); 
      externalSearchPaths.push_back(QFileInfo(exePathQStr).dir().path().toStdString()); 

      // 恢复创建解释器实例
      m_interpreter = std::make_unique<PythonInterpreter>(exePath, externalSearchPaths, false); 

      appendOutput(tr("Embedded Python interpreter initialized successfully.")); // 恢复成功消息
      
  } catch (const std::exception& e) {
      appendOutput(tr("Failed to initialize embedded Python interpreter: %1").arg(e.what()), true);
      m_interpreter.reset(); // 恢复 reset
  } catch (...) {
      appendOutput(tr("Failed to initialize embedded Python interpreter due to an unknown error."), true);
      m_interpreter.reset(); // 恢复 reset
  }
}

void PythonConsole::executeCommand(const QString& command)
{
  // if (!m_interpreter) { // 注释掉检查
  //   appendOutput(tr("Python interpreter is not initialized."), true);
  //   return;
  // }
  appendOutput(QString(">>> %1").arg(command));
  auto error = std::optional<std::string>{"Python execution temporarily disabled"}; // 模拟错误
  // auto error = m_interpreter->executeCode(command.toStdString()); // 注释掉 Python 调用
  if (error) {
    appendOutput(tr("Error: %1").arg(QString::fromStdString(*error)), true);
  }
  appendOutput(tr("(Note: Command output currently goes to the system console.)"), false); 
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

// 新增方法：用于向UI控制台添加文本
void PythonConsole::appendOutput(const QString& text, bool isError)
{
    if (isError) {
        m_outputConsole->append(QString("<span style=\"color:red;\">%1</span>").arg(text.toHtmlEscaped()));
    } else {
        m_outputConsole->append(text.toHtmlEscaped());
    }
    // 滚动到底部 (现在 QScrollBar 已包含)
    m_outputConsole->verticalScrollBar()->setValue(m_outputConsole->verticalScrollBar()->maximum());
}

} // namespace tb::ui
