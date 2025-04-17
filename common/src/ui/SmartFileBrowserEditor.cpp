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
  dialog.setNameFilter(getFileTypeDescription());
  
  // 根据属性类型设置不同的过滤器
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
      // 对于不确定类型，提供所有可能的文件类型
      dialog.setNameFilters({
        "Model Files (*.mdl)",
        "Sprite Files (*.spr)",
        "Sound Files (*.wav)",
        "All Files (*.*)"
      });
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
      return "*.mdl *.spr *.wav";  // 混合类型提供多种选择
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
      return "All Supported Files (*.mdl *.spr *.wav)";
  }
}

void SmartFileBrowserEditor::doUpdateVisual(const std::vector<mdl::EntityNodeBase*>& nodes) {
  if (!m_lineEdit) return;
  
  // 从节点获取属性值，这里我们只取一个值
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
  // 使用gamePath()方法而不是path()
  QString startDir = io::pathAsQString(docPtr->game()->gamePath());
  
  // 对于声音文件，通常在sound子目录
  if (m_propertyType == FilePropertyType::SoundFile) {
    QDir soundDir(startDir + "/sound");
    if (soundDir.exists()) {
      startDir = soundDir.absolutePath();
    }
  }
  
  QFileDialog dialog(m_lineEdit->window());
  dialog.setWindowTitle("Select File");
  dialog.setDirectory(startDir);
  dialog.setFileMode(QFileDialog::ExistingFile);
  setupFileFilters(dialog);
  
  if (dialog.exec() == QDialog::Accepted) {
    QStringList files = dialog.selectedFiles();
    if (!files.isEmpty()) {
      // 将选择的文件路径转换为相对于游戏资源目录的路径
      QString fullPath = files.first();
      QString relativePath = fullPath;
      
      // 如果文件在游戏资源目录下，转换为相对路径
      if (fullPath.startsWith(startDir)) {
        relativePath = fullPath.mid(startDir.length());
        // 移除开头的斜杠
        if (relativePath.startsWith('/') || relativePath.startsWith('\\')) {
          relativePath = relativePath.mid(1);
        }
      }
      
      m_lineEdit->setText(relativePath);
    }
  }
}

} // namespace tb::ui 