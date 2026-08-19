/*
 Copyright (C) 2026 XiangXtreme

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

#include <QJsonObject>
#include <QString>

#include "mdl/Node.h"

#include "vm/bbox.h"

#include <optional>
#include <vector>

namespace tb::mdl
{
class Map;
class WorldNode;
}

namespace tb::ui
{

struct McpSelectionQueryOptions
{
  bool excludeWorld = true;
  bool selectableOnly = false;
  bool leafOnly = false;
  bool exactTypeOnly = true;
  bool removeDescendantMatches = false;
};

std::optional<vm::bbox3d> mcpQueryBoundsFromParams(
  const QJsonObject& params, QString& error);

std::vector<mdl::Node*> mcpFilteredNodes(
  mdl::Map& map,
  const QJsonObject& params,
  const McpSelectionQueryOptions& options,
  QString& error);

QString mcpNodePathId(const mdl::Node& node, const mdl::WorldNode& worldNode);

QString mcpNodeTypeName(const mdl::Node& node);

} // namespace tb::ui
