#pragma once

#include <QIcon>

#include "ui/SmartPropertyEditor.h"

#include <filesystem>
#include <string>
#include <vector>

class QListWidget;
class QListWidgetItem;
class QPushButton;
class QWidget;

namespace tb::mdl
{
class EntityNodeBase;
class GameFileSystem;
} // namespace tb::mdl

namespace tb::ui
{
class MapDocument;

struct SmartSkyboxItem
{
  std::string name;
  std::filesystem::path previewPath;
};

std::optional<std::pair<std::string, std::string>> skyboxBaseAndSuffix(
  const std::filesystem::path& path);
std::vector<SmartSkyboxItem> findSmartSkyboxes(mdl::GameFileSystem& gameFileSystem);

class SmartSkyboxEditor : public SmartPropertyEditor
{
  Q_OBJECT
private:
  QPushButton* m_refreshButton = nullptr;
  QListWidget* m_listWidget = nullptr;
  std::vector<SmartSkyboxItem> m_skyboxes;

public:
  explicit SmartSkyboxEditor(MapDocument& document, QWidget* parent = nullptr);

private:
  void createGui();
  void doUpdateVisual(const std::vector<mdl::EntityNodeBase*>& nodes) override;

  void reloadSkyboxes();
  void applySkybox(QListWidgetItem* item);
  QIcon iconForSkybox(const SmartSkyboxItem& skybox);
};

} // namespace tb::ui
