//Added by lws

#pragma once

#include "ui/TabBook.h"
#include "python_interpreter/pythoninterpreter.h"

#include <QSplitter>
#include <QTextEdit>
#include <QListWidget>
#include <QTreeView>
#include <QMap>
#include <QString>
#include <QProcess>
#include <memory>

class QToolBar;
class QPushButton;
class QFileSystemModel;
class QLineEdit;

namespace tb::ui
{

/**
 * A widget that provides a Python console interface with script editing capabilities.
 */
class PythonConsole : public TabBookPage
{
  Q_OBJECT
private:
  // 主分割器
  QSplitter* m_mainSplitter;
  
  // 左侧分割器
  QSplitter* m_leftSplitter;
  
  // 文件浏览器
  QTreeView* m_fileTreeView;
  QFileSystemModel* m_fileSystemModel;
  
  // 脚本列表
  QListWidget* m_scriptListWidget;
  
  // 代码编辑器
  QTextEdit* m_codeEditor;
  
  // 输出控制台
  QTextEdit* m_outputConsole;
  
  // 工具栏
  QToolBar* m_toolBar;
  
  // 输入控制台
  QLineEdit* m_inputLine;
  
  // 已打开的脚本
  QMap<QString, QString> m_openScripts;
  
  // 当前脚本路径
  QString m_currentScriptPath;
  
  // 脚本根目录
  QString m_scriptsRootDir;
  
  // 恢复 PythonInterpreter 实例
  std::unique_ptr<PythonInterpreter> m_interpreter;

public:
  /**
   * Creates a new Python console.
   *
   * @param parent the parent widget
   */
  explicit PythonConsole(QWidget* parent = nullptr);
  ~PythonConsole() override;
  
  /**
   * 返回当前脚本的内容
   */
  QString getCurrentScriptContent() const;
  
  /**
   * 设置脚本根目录
   */
  void setScriptsRootDirectory(const QString& path);
  
  QWidget* createTabBarPage(QWidget* parent = nullptr) override;

private slots:
  // 文件操作
  void onNewScript();
  void onOpenScript();
  void onSaveScript();
  void onSaveScriptAs();
  
  // 脚本执行
  void onRunScript();
  
  // 文件浏览器事件
  void onFileSelected(const QModelIndex& index);
  
  // 脚本列表事件
  void onScriptSelected(int row);
  
  // 命令行输入
  void onCommandEntered();

private:
  void setupUI();
  void setupConnections();
  void setupPythonEnvironment();
  
  void executeCommand(const QString& command);
  void updateScriptsList();
  
  // 添加一个用于在 UI 控制台显示状态/错误的方法
  void appendOutput(const QString& text, bool isError = false);
};

} // namespace tb::ui
