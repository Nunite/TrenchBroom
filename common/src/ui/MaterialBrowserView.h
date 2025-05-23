/*
 Copyright (C) 2010 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "NotifierConnection.h"
#include "render/FontDescriptor.h"
#include "ui/CellView.h"

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

class QScrollBar;

namespace tb::mdl
{
class Material;
class MaterialCollection;
class ResourceId;
} // namespace tb::mdl

namespace tb::ui
{

class GLContextManager;
class MapDocument;
using MaterialGroupData = std::string;

enum class MaterialSortOrder
{
  Name,
  Usage
};

class MaterialBrowserView : public CellView
{
  Q_OBJECT
private:
  std::weak_ptr<MapDocument> m_document;
  bool m_group = false;
  bool m_hideUnused = false;
  MaterialSortOrder m_sortOrder = MaterialSortOrder::Name;
  std::string m_filterText;

  const mdl::Material* m_selectedMaterial = nullptr;
  std::unordered_map<std::string, bool> m_collapsedGroups;

  NotifierConnection m_notifierConnection;

public:
  MaterialBrowserView(
    QScrollBar* scrollBar,
    GLContextManager& contextManager,
    std::weak_ptr<MapDocument> document);
  ~MaterialBrowserView() override;

  void setSortOrder(MaterialSortOrder sortOrder);
  void setGroup(bool group);
  void setHideUnused(bool hideUnused);
  void setFilterText(const std::string& filterText);

  const mdl::Material* selectedMaterial() const;
  void setSelectedMaterial(const mdl::Material* selectedMaterial);

  void revealMaterial(const mdl::Material* material);
  
  bool isGroupCollapsed(const std::string& groupName) const;
  void toggleGroupCollapsed(const std::string& groupName);

private:
  void resourcesWereProcessed(const std::vector<mdl::ResourceId>& resources);

  void reloadMaterials();

  void doInitLayout(Layout& layout) override;
  void doReloadLayout(Layout& layout) override;

  void addMaterialsToLayout(
    Layout& layout,
    const std::vector<const mdl::Material*>& materials,
    const render::FontDescriptor& font);
  void addMaterialToLayout(
    Layout& layout, const mdl::Material& material, const render::FontDescriptor& font);

  std::vector<const mdl::MaterialCollection*> getCollections() const;
  std::vector<const mdl::Material*> getMaterials(
    const mdl::MaterialCollection& collection) const;
  std::vector<const mdl::Material*> getMaterials() const;

  std::vector<const mdl::Material*> filterMaterials(
    std::vector<const mdl::Material*> materials) const;
  std::vector<const mdl::Material*> sortMaterials(
    std::vector<const mdl::Material*> materials) const;

  void doClear() override;
  void doRender(Layout& layout, float y, float height) override;
  bool shouldRenderFocusIndicator() const override;
  const Color& getBackgroundColor() override;

  void renderBounds(Layout& layout, float y, float height);
  const Color& materialColor(const mdl::Material& material) const;
  void renderMaterials(Layout& layout, float y, float height);

  void doLeftClick(Layout& layout, float x, float y) override;
  QString tooltip(const Cell& cell) override;
  void doContextMenu(Layout& layout, float x, float y, QContextMenuEvent* event) override;

  void renderGroup(Layout& layout, float y, float height, size_t groupIndex);
  void renderGroupHeader(Layout& layout, float y, float height, size_t groupIndex);

  const mdl::Material& cellData(const Cell& cell) const;
signals:
  void materialSelected(const mdl::Material* material);
};

} // namespace tb::ui
