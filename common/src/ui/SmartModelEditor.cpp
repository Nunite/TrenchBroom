/*
 Copyright (C) 2023 Lws

 This file is part of TrenchBroom.

 This file is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This file is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "SmartModelEditor.h"

#include <QWidget>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QFileDialog>
#include <QAbstractButton>
#include <QToolButton>

#include "io/PathQt.h"
#include "ui/QtUtils.h"
#include "ui/MapDocument.h"
#include "mdl/EntityNodeBase.h"

#include <filesystem>

namespace tb::ui
{

SmartModelEditor::SmartModelEditor(
  std::weak_ptr<MapDocument> document, QWidget* parent)
  : SmartPropertyEditor{std::move(document), parent}
{
  m_pathLineEdit = new QLineEdit{};
  m_pathLineEdit->setReadOnly(true);

  m_browseButton = createBitmapButton("Folder.svg", "Browse for a model file");

  auto* layout = new QHBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(m_pathLineEdit, 1);
  layout->addWidget(m_browseButton, 0);

  setLayout(layout);

  connect(m_browseButton, &QAbstractButton::clicked, this, &SmartModelEditor::browseFile);
}

void SmartModelEditor::doUpdateVisual(const std::vector<mdl::EntityNodeBase*>& nodes)
{
  if (nodes.size() == 1) {
    if (const auto* modelPathStr = nodes.front()->entity().property(propertyKey())) {
      m_pathLineEdit->setText(io::pathAsQString(std::filesystem::path{*modelPathStr}));
    } else {
      m_pathLineEdit->clear();
    }
  } else {
    m_pathLineEdit->clear();
  }
}

void SmartModelEditor::browseFile()
{
  const auto pathQStr = QFileDialog::getOpenFileName(
    nullptr,
    tr("Load Model File"),
    fileDialogDefaultDirectory(FileDialogDir::GamePath),
    tr("Model files (*.mdl);;All files (*.*)"));

  if (!pathQStr.isEmpty())
  {
    updateFileDialogDefaultDirectoryWithFilename(
      FileDialogDir::GamePath, pathQStr);

    const auto absModelPath = io::pathFromQString(pathQStr);
    // Assuming the model path should be relative to the game document or game path
    // You might need to adjust this logic based on how model paths are handled in TrenchBroom
    // Calculate path relative to the game path
    auto relativeModelPathFull = std::filesystem::relative(absModelPath, document()->game()->gamePath());

    // Find the "models/" part in the relative path
    std::string relativePathStr = relativeModelPathFull.string();
    size_t modelsPos = relativePathStr.find("models/");
    if (modelsPos == std::string::npos) {
      modelsPos = relativePathStr.find("models\\");
    }

    std::string finalModelPath;
    if (modelsPos != std::string::npos) {
      // Extract the part starting from "models/" or "models\"
      finalModelPath = relativePathStr.substr(modelsPos);
    } else {
      // If "models" is not found, use the full relative path (or handle error)
      finalModelPath = relativePathStr;
    }

    addOrUpdateProperty(finalModelPath);
  }
}

} // namespace tb::ui 