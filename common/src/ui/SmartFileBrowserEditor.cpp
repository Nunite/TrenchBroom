//Add by lws

#include "SmartFileBrowserEditor.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <unordered_set>

#include "io/PathQt.h" 
#include "io/ResourceUtils.h"
#include "ui/MapDocument.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/Entity.h"
#include "mdl/EntityDefinition.h"
#include "mdl/Game.h"

namespace tb::ui
{

SmartFileBrowserEditor::SmartFileBrowserEditor(
    std::weak_ptr<MapDocument> document,
    QWidget* parent)
  : SmartPropertyEditor(document, parent)
  , m_lineEdit(nullptr)
  , m_browseButton(nullptr)
  , m_propertyType(FilePropertyType::AnyFile)
  , m_basePath("")
{
  auto* layout = new QHBoxLayout();
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(3);
  
  m_lineEdit = new QLineEdit(this);
  m_browseButton = new QPushButton("Browse...", this);
  m_browseButton->setToolTip("Browse for file");
  
  layout->addWidget(m_lineEdit, 1);
  layout->addWidget(m_browseButton, 0);
  
  setLayout(layout);
  
  connect(m_lineEdit, &QLineEdit::textChanged, this, [this]() {
    addOrUpdateProperty(m_lineEdit->text().toStdString());
  });
  
  connect(m_browseButton, &QPushButton::clicked, this, &SmartFileBrowserEditor::browseForFile);
}

void SmartFileBrowserEditor::setPropertyType(FilePropertyType type) {
  m_propertyType = type;
}

void SmartFileBrowserEditor::setupFileFilters(QFileDialog& dialog) {
  switch (m_propertyType) {
    case FilePropertyType::ModelFile:
      dialog.setNameFilters({"Model Files (*.mdl)", "All Files (*.*)"});
      break;
    case FilePropertyType::SpriteFile:
      dialog.setNameFilters({"Sprite Files (*.spr)", "All Files (*.*)"});
      break;
    case FilePropertyType::SoundFile:
      dialog.setNameFilters({"Sound Files (*.wav)", "Wave Files (*.wav)", "All Files (*.*)"});
      break;
    default:
      // 不要使用多种文件类型的混合过滤器，选择一个默认的
      dialog.setNameFilters({"All Files (*.*)"});
      break;
  }
}

QString SmartFileBrowserEditor::getFileTypeFilter() const {
  switch (m_propertyType) {
    case FilePropertyType::ModelFile:
      return "*.mdl";
    case FilePropertyType::SoundFile:
      return "*.wav";
    case FilePropertyType::SpriteFile:
      return "*.spr";
    default:
      // 不要返回多种文件类型混合，默认返回一种
      return "*.*";
  }
}

QString SmartFileBrowserEditor::getFileTypeDescription() const {
  switch (m_propertyType) {
    case FilePropertyType::ModelFile:
      return "Model Files (*.mdl)";
    case FilePropertyType::SoundFile:
      return "Sound Files (*.wav)";
    case FilePropertyType::SpriteFile:
      return "Sprite Files (*.spr)";
    default:
      // 不要返回混合描述
      return "All Files";
  }
}

void SmartFileBrowserEditor::doUpdateVisual(const std::vector<mdl::EntityNodeBase*>& nodes) {
  if (!m_lineEdit) return;
  
  // 从节点获取属性值
  std::unordered_set<std::string> values;
  for (const auto* node : nodes) {
    const auto* propValue = node->entity().property(propertyKey());
    if (propValue) {
      values.insert(*propValue);
    }
  }
  
  if (values.size() == 1) {
    // 所有节点都有相同的值
    m_lineEdit->setText(QString::fromStdString(*values.begin()));
  } else {
    // 节点有不同的值或者没有值
    m_lineEdit->clear();
  }
}

void SmartFileBrowserEditor::browseForFile() {
  auto docPtr = document();
  if (!docPtr) return;
  
  // 获取游戏资源目录作为起始目录
  QString startDir = io::pathAsQString(docPtr->game()->gamePath());
  
  // 对于声音文件，通常在sound子目录
  if (m_propertyType == FilePropertyType::SoundFile) {
    QDir soundDir(startDir + "/sound");
    if (soundDir.exists()) {
      startDir = soundDir.absolutePath();
    }
  }
  
  QFileDialog dialog(m_lineEdit->window());
  
  // 在对话框标题中添加文件类型信息
  QString dialogTitle;
  switch (m_propertyType) {
    case FilePropertyType::ModelFile:
      dialogTitle = "Select Model File";
      break;
    case FilePropertyType::SpriteFile:
      dialogTitle = "Select Sprite File";
      break;
    case FilePropertyType::SoundFile:
      dialogTitle = "Select Sound File";
      break;
    default:
      dialogTitle = "Select File";
      break;
  }
  
  dialog.setWindowTitle(dialogTitle);
  dialog.setDirectory(startDir);
  dialog.setFileMode(QFileDialog::ExistingFile);
  setupFileFilters(dialog);
  
  if (dialog.exec() == QDialog::Accepted) {
    QStringList files = dialog.selectedFiles();
    if (!files.isEmpty()) {
      // 将选择的文件路径转换为相对于游戏资源目录的路径
      QString fullPath = files.first();
      QString relativePath = fullPath;
      
      // 改进的路径处理逻辑
      // 1. 检查是否有标准子目录名称（如"models/"、"sound/"、"sprites/"等）
      QStringList standardSubDirs = {"models", "sound", "sprites", "maps", "gfx"};
      bool foundSubDir = false;
      
      // 使用Qt的QFileInfo类来处理路径
      QFileInfo fileInfo(fullPath);
      QString fileName = fileInfo.fileName();
      QString filePath = fileInfo.path();
      
      // 遍历所有标准子目录名称
      for (const QString& subDir : standardSubDirs) {
        // 尝试在路径中查找这个子目录
        int index = fullPath.lastIndexOf("/" + subDir + "/", -1, Qt::CaseInsensitive);
        if (index == -1) {
          // 如果未找到带斜杠的格式，尝试查找可能在路径开头的情况
          index = fullPath.lastIndexOf("\\" + subDir + "\\", -1, Qt::CaseInsensitive);
        }
        
        // 如果找到了子目录
        if (index != -1) {
          // 提取子目录开始的部分作为相对路径
          relativePath = fullPath.mid(index + 1); // +1 跳过开头的斜杠
          // 规范化斜杠为正斜杠
          relativePath.replace('\\', '/');
          foundSubDir = true;
          break;
        }
      }
      
      // 如果没有找到标准子目录，还是尝试相对于游戏目录的处理
      if (!foundSubDir) {
        // 将startDir路径中的反斜杠转换为正斜杠以确保一致性
        QString normalizedStartDir = startDir;
        normalizedStartDir.replace('\\', '/');
        
        // 将fullPath中的反斜杠转换为正斜杠
        QString normalizedFullPath = fullPath;
        normalizedFullPath.replace('\\', '/');
        
        // 检查文件是否在游戏资源目录下
        if (normalizedFullPath.startsWith(normalizedStartDir, Qt::CaseInsensitive)) {
          relativePath = normalizedFullPath.mid(normalizedStartDir.length());
          // 移除开头的斜杠
          if (relativePath.startsWith('/')) {
            relativePath = relativePath.mid(1);
          }
        }
      }
      
      m_lineEdit->setText(relativePath);
    }
  }
}

} // namespace tb::ui 