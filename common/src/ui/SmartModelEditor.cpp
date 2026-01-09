#include "SmartModelEditor.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QPushButton>

#include "io/PathQt.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/Game.h"
#include "mdl/Map.h"
#include "ui/QtUtils.h"

#include <filesystem>

namespace tb::ui
{

SmartModelEditor::SmartModelEditor(mdl::Map& map, QWidget* parent)
  : SmartPropertyEditor{map, parent}
{
  createGui();
}

void SmartModelEditor::createGui()
{
  auto* layout = new QHBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);

  m_browseButton = new QPushButton{this};
  m_browseButton->setText(tr("Load Model File"));

  layout->addWidget(m_browseButton);

  connect(m_browseButton, &QPushButton::clicked, this, &SmartModelEditor::browseFile);
}

void SmartModelEditor::doUpdateVisual(const std::vector<mdl::EntityNodeBase*>&)
{
}

void SmartModelEditor::browseFile()
{
  auto* game = map().game();
  if (!game)
  {
    return;
  }

  const auto caption = tr("Load Model File");
  const auto filter = tr("Model files (*.mdl);;All files (*.*)");

  const auto pathQStr = QFileDialog::getOpenFileName(
    nullptr, caption, fileDialogDefaultDirectory(FileDialogDir::GamePath), filter);
  if (pathQStr.isEmpty())
  {
    return;
  }

  updateFileDialogDefaultDirectoryWithFilename(FileDialogDir::GamePath, pathQStr);

  const auto absModelPath = io::pathFromQString(pathQStr);
  const auto gamePath = game->gamePath();

  std::error_code ec;
  const auto relativeModelPathFull = std::filesystem::relative(absModelPath, gamePath, ec);
  if (ec)
  {
    return;
  }

  auto relativePathStr = relativeModelPathFull.string();
  auto modelsPos = relativePathStr.find("models/");
  if (modelsPos == std::string::npos)
  {
    modelsPos = relativePathStr.find("models\\");
  }

  std::string finalModelPath;
  if (modelsPos != std::string::npos)
  {
    finalModelPath = relativePathStr.substr(modelsPos);
  }
  else
  {
    finalModelPath = relativeModelPathFull.generic_string();
  }

  for (auto& ch : finalModelPath)
  {
    if (ch == '\\')
    {
      ch = '/';
    }
  }

  if (finalModelPath.empty())
  {
    return;
  }

  addOrUpdateProperty(finalModelPath);
}

} // namespace tb::ui
