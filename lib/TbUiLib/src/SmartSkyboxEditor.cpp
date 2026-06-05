#include "ui/SmartSkyboxEditor.h"

#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "fs/PathInfo.h"
#include "fs/TraversalMode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/GameFileSystem.h"
#include "mdl/Map.h"
#include "ui/MapDocument.h"
#include "ui/QPathUtils.h"

#include "kd/string_compare.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <optional>

namespace tb::ui
{
namespace
{
constexpr auto SkySuffixes = std::array{"rt", "bk", "lf", "ft", "up", "dn"};
constexpr auto SkyExtensions =
  std::array{".tga", ".bmp", ".png", ".jpg", ".jpeg", ".dds"};

bool hasSkyboxExtension(const std::filesystem::path& path)
{
  const auto extension = path.extension().generic_string();
  return std::ranges::any_of(SkyExtensions, [&](const auto* candidate) {
    return kdl::ci::str_is_equal(extension, candidate);
  });
}

bool endsWithIgnoreCase(const std::string& str, const char* suffix)
{
  const auto suffixLength = std::strlen(suffix);
  return str.size() >= suffixLength
         && kdl::ci::str_is_equal(str.substr(str.size() - suffixLength), suffix);
}

} // namespace

std::optional<std::pair<std::string, std::string>> skyboxBaseAndSuffix(
  const std::filesystem::path& path)
{
  if (!hasSkyboxExtension(path))
  {
    return std::nullopt;
  }

  const auto stem = path.stem().generic_string();
  for (const auto* suffix : SkySuffixes)
  {
    if (stem.size() > std::strlen(suffix) && endsWithIgnoreCase(stem, suffix))
    {
      return std::pair{stem.substr(0, stem.size() - std::strlen(suffix)), suffix};
    }
  }

  return std::nullopt;
}

std::vector<SmartSkyboxItem> findSmartSkyboxes(mdl::GameFileSystem& gameFileSystem)
{
  auto result = std::vector<SmartSkyboxItem>{};
  const auto envPath = std::filesystem::path{"gfx"} / "env";
  if (gameFileSystem.pathInfo(envPath) != fs::PathInfo::Directory)
  {
    return result;
  }

  const auto pathsResult = gameFileSystem.find(envPath, fs::TraversalMode::Flat);
  if (pathsResult.is_error())
  {
    return result;
  }

  struct SkyboxCandidate
  {
    std::map<std::string, std::filesystem::path, kdl::ci::string_less> faces;
  };

  auto candidates = std::map<std::string, SkyboxCandidate, kdl::ci::string_less>{};
  for (const auto& path : pathsResult.value())
  {
    if (gameFileSystem.pathInfo(path) != fs::PathInfo::File)
    {
      continue;
    }

    const auto baseAndSuffix = skyboxBaseAndSuffix(path);
    if (!baseAndSuffix)
    {
      continue;
    }

    const auto& [baseName, suffix] = *baseAndSuffix;
    candidates[baseName].faces.emplace(suffix, path);
  }

  for (const auto& [baseName, candidate] : candidates)
  {
    const auto complete = std::ranges::all_of(
      SkySuffixes, [&](const auto* suffix) { return candidate.faces.contains(suffix); });
    if (!complete)
    {
      continue;
    }

    result.push_back(SmartSkyboxItem{baseName, candidate.faces.at("rt")});
  }

  std::ranges::sort(result, {}, &SmartSkyboxItem::name);
  return result;
}

SmartSkyboxEditor::SmartSkyboxEditor(MapDocument& document, QWidget* parent)
  : SmartPropertyEditor{document, parent}
{
  createGui();
}

void SmartSkyboxEditor::createGui()
{
  setMinimumHeight(220);

  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);

  m_refreshButton = new QPushButton{tr("Refresh skyboxes"), this};
  m_listWidget = new QListWidget{this};
  m_listWidget->setIconSize(QSize{96, 48});
  m_listWidget->setMinimumHeight(180);

  layout->addWidget(m_refreshButton);
  layout->addWidget(m_listWidget);

  connect(
    m_refreshButton, &QPushButton::clicked, this, &SmartSkyboxEditor::reloadSkyboxes);
  connect(m_listWidget, &QListWidget::itemClicked, this, &SmartSkyboxEditor::applySkybox);
}

void SmartSkyboxEditor::doUpdateVisual(const std::vector<mdl::EntityNodeBase*>&)
{
  if (m_skyboxes.empty())
  {
    reloadSkyboxes();
  }
}

void SmartSkyboxEditor::reloadSkyboxes()
{
  const auto currentRow = m_listWidget->currentRow();
  m_skyboxes = findSmartSkyboxes(document().map().gameFileSystem());

  m_listWidget->clear();
  for (const auto& skybox : m_skyboxes)
  {
    auto* item =
      new QListWidgetItem{iconForSkybox(skybox), QString::fromStdString(skybox.name)};
    item->setToolTip(pathAsQPath(skybox.previewPath));
    m_listWidget->addItem(item);
  }

  if (currentRow >= 0 && currentRow < m_listWidget->count())
  {
    m_listWidget->setCurrentRow(currentRow);
  }
}

void SmartSkyboxEditor::applySkybox(QListWidgetItem* item)
{
  const auto row = m_listWidget->row(item);
  if (row < 0 || row >= static_cast<int>(m_skyboxes.size()))
  {
    return;
  }

  addOrUpdateProperty(m_skyboxes[size_t(row)].name);
  emit skyboxApplied();
}

QIcon SmartSkyboxEditor::iconForSkybox(const SmartSkyboxItem& skybox)
{
  const auto absPath = document().map().gameFileSystem().makeAbsolute(skybox.previewPath);
  if (absPath.is_error())
  {
    return {};
  }

  return QIcon{pathAsQPath(absPath.value())};
}

} // namespace tb::ui
