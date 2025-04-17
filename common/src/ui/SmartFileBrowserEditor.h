//Add by lws

#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QString>

#include "mdl/PropertyDefinition.h"
#include "ui/SmartPropertyEditor.h"

// 添加前向声明
QT_BEGIN_NAMESPACE
class QFileDialog;
QT_END_NAMESPACE

namespace tb {
namespace mdl {
class EntityNodeBase;
}

namespace ui
{

// 为文件类型定义自定义枚举
enum class FilePropertyType {
  ModelFile,
  SoundFile,
  SpriteFile,
  AnyFile
};

class SmartFileBrowserEditor : public SmartPropertyEditor 
{
  Q_OBJECT
private:
  QLineEdit* m_lineEdit;
  QPushButton* m_browseButton;
  
  FilePropertyType m_propertyType;
  QString m_basePath;
  
public:
  explicit SmartFileBrowserEditor(
    std::weak_ptr<MapDocument> document,
    QWidget* parent = nullptr);
  
  void setPropertyType(FilePropertyType type);
  
  void setupFileFilters(QFileDialog& dialog);
  QString getFileTypeFilter() const;
  QString getFileTypeDescription() const;
  
private:
  // 实现基类的纯虚函数
  void doUpdateVisual(const std::vector<mdl::EntityNodeBase*>& nodes) override;
  
private slots:
  void browseForFile();
};

} // namespace ui
} // namespace tb 