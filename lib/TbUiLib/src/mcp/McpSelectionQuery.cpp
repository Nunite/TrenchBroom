/*
 Copyright (C) 2026

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

#include "McpSelectionQuery.h"

#include <QJsonArray>
#include <QStringList>

#include "mdl/Brush.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/EntityProperties.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/PatchNode.h"
#include "mdl/WorldNode.h"

#include "vm/bbox.h"

#include <algorithm>
#include <array>
#include <optional>
#include <ranges>
#include <vector>

namespace tb::ui
{
namespace
{

std::optional<vm::vec3d> vec3FromJson(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto value = params.value(key);
  if (!value.isArray())
  {
    error = QString{"%1 must be an array of three numbers"}.arg(key);
    return std::nullopt;
  }

  const auto array = value.toArray();
  if (array.size() != 3)
  {
    error = QString{"%1 must contain exactly three numbers"}.arg(key);
    return std::nullopt;
  }

  auto components = std::array<double, 3>{};
  for (auto i = 0; i < 3; ++i)
  {
    if (!array[i].isDouble())
    {
      error = QString{"%1[%2] must be a number"}.arg(key).arg(i);
      return std::nullopt;
    }
    components[static_cast<size_t>(i)] = array[i].toDouble();
  }

  return vm::vec3d{components[0], components[1], components[2]};
}

size_t optionalSize(
  const QJsonObject& params, const QString& key, const size_t defaultValue)
{
  const auto value = params.value(key);
  if (!value.isDouble())
  {
    return defaultValue;
  }
  return static_cast<size_t>(std::max(0, value.toInt()));
}

bool textMatches(const QString& text, const QString& query)
{
  return text.contains(query, Qt::CaseInsensitive);
}

bool materialMatches(const mdl::Node& node, const QString& materialName)
{
  if (materialName.isEmpty())
  {
    return true;
  }

  const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(&node);
  if (!brushNode)
  {
    return false;
  }

  for (const auto& face : brushNode->brush().faces())
  {
    if (face.attributes().materialName() == materialName.toStdString())
    {
      return true;
    }
  }
  return false;
}

bool entityPropertyMatches(
  const mdl::Node& node, const QString& classname, const QString& targetname)
{
  const auto* entityNode = dynamic_cast<const mdl::EntityNodeBase*>(&node);
  if (!entityNode)
  {
    return classname.isEmpty() && targetname.isEmpty();
  }

  if (
    !classname.isEmpty()
    && !textMatches(QString::fromStdString(entityNode->entity().classname()), classname))
  {
    return false;
  }

  if (!targetname.isEmpty())
  {
    const auto targetnameProperty = entityNode->entity().property("targetname");
    const auto targetnameValue =
      targetnameProperty ? QString::fromStdString(*targetnameProperty) : QString{};
    if (!textMatches(targetnameValue, targetname))
    {
      return false;
    }
  }

  return true;
}

bool nodeMatchesQuery(
  const mdl::Node& node, const mdl::WorldNode& worldNode, const QString& query)
{
  if (
    textMatches(mcpNodePathId(node, worldNode), query)
    || textMatches(mcpNodeTypeName(node), query)
    || textMatches(QString::fromStdString(node.name()), query))
  {
    return true;
  }

  if (const auto* entityNode = dynamic_cast<const mdl::EntityNodeBase*>(&node))
  {
    if (textMatches(QString::fromStdString(entityNode->entity().classname()), query))
    {
      return true;
    }

    for (const auto& property : entityNode->entity().properties())
    {
      if (
        textMatches(QString::fromStdString(property.key()), query)
        || textMatches(QString::fromStdString(property.value()), query))
      {
        return true;
      }
    }
  }

  return false;
}

bool boundsMatch(
  const vm::bbox3d& objectBounds, const vm::bbox3d& queryBounds, const QString& mode)
{
  if (mode.compare("contains", Qt::CaseInsensitive) == 0)
  {
    return queryBounds.contains(objectBounds);
  }
  return objectBounds.intersects(queryBounds);
}

bool typeMatches(
  const mdl::Node& node, const QString& type, const bool exactTypeOnly)
{
  if (type.isEmpty())
  {
    return true;
  }

  if (mcpNodeTypeName(node).compare(type, Qt::CaseInsensitive) == 0)
  {
    return true;
  }

  if (exactTypeOnly)
  {
    return false;
  }

  if (type.compare("entity", Qt::CaseInsensitive) == 0)
  {
    return dynamic_cast<const mdl::EntityNodeBase*>(&node) != nullptr;
  }
  return false;
}

bool nodeFilterMatches(
  const mdl::Node& node,
  const mdl::WorldNode& worldNode,
  const QJsonObject& params,
  const std::optional<vm::bbox3d>& queryBounds,
  const McpSelectionQueryOptions& options)
{
  if (options.excludeWorld && &node == &worldNode)
  {
    return false;
  }
  if (options.leafOnly && node.childCount() > 0u)
  {
    return false;
  }

  const auto type = params.value("type").toString().trimmed();
  if (!typeMatches(node, type, options.exactTypeOnly))
  {
    return false;
  }

  if (!entityPropertyMatches(
        node,
        params.value("classname").toString().trimmed(),
        params.value("targetname").toString().trimmed()))
  {
    return false;
  }

  if (!materialMatches(node, params.value("material").toString().trimmed()))
  {
    return false;
  }

  if (const auto query = params.value("query").toString().trimmed();
      !query.isEmpty() && !nodeMatchesQuery(node, worldNode, query))
  {
    return false;
  }

  if (queryBounds)
  {
    const auto mode = params.value("boundsMode").toString("intersects");
    if (!boundsMatch(node.logicalBounds(), *queryBounds, mode))
    {
      return false;
    }
  }

  return true;
}

void collectFilteredNodes(
  const mdl::Node& node,
  mdl::Map& map,
  const QJsonObject& params,
  const std::optional<vm::bbox3d>& queryBounds,
  const McpSelectionQueryOptions& options,
  const size_t limit,
  std::vector<mdl::Node*>& matches)
{
  if (matches.size() >= limit)
  {
    return;
  }

  const auto& worldNode = map.worldNode();
  if (
    nodeFilterMatches(node, worldNode, params, queryBounds, options)
    && (!options.selectableOnly || map.editorContext().selectable(node)))
  {
    matches.push_back(const_cast<mdl::Node*>(&node));
  }

  for (const auto* child : node.children())
  {
    collectFilteredNodes(*child, map, params, queryBounds, options, limit, matches);
    if (matches.size() >= limit)
    {
      return;
    }
  }
}

void removeDescendantMatches(std::vector<mdl::Node*>& nodes)
{
  nodes.erase(
    std::remove_if(
      std::begin(nodes),
      std::end(nodes),
      [&](const auto* node) {
        return std::ranges::any_of(nodes, [&](const auto* other) {
          return node != other && node->isDescendantOf(*other);
        });
      }),
    std::end(nodes));
}

} // namespace

QString mcpNodePathId(const mdl::Node& node, const mdl::WorldNode& worldNode)
{
  if (&node == &worldNode)
  {
    return "node:world";
  }

  auto parts = QStringList{};
  for (const auto index : node.pathFrom(worldNode).indices)
  {
    parts.push_back(QString::number(index));
  }
  return QString{"node:%1"}.arg(parts.join('/'));
}

QString mcpNodeTypeName(const mdl::Node& node)
{
  if (dynamic_cast<const mdl::WorldNode*>(&node) != nullptr)
  {
    return "world";
  }
  if (dynamic_cast<const mdl::LayerNode*>(&node) != nullptr)
  {
    return "layer";
  }
  if (dynamic_cast<const mdl::GroupNode*>(&node) != nullptr)
  {
    return "group";
  }
  if (dynamic_cast<const mdl::EntityNode*>(&node) != nullptr)
  {
    return "entity";
  }
  if (dynamic_cast<const mdl::BrushNode*>(&node) != nullptr)
  {
    return "brush";
  }
  if (dynamic_cast<const mdl::PatchNode*>(&node) != nullptr)
  {
    return "patch";
  }
  return "node";
}

std::optional<vm::bbox3d> mcpQueryBoundsFromParams(
  const QJsonObject& params, QString& error)
{
  const auto hasMin = !params.value("min").isUndefined();
  const auto hasMax = !params.value("max").isUndefined();
  if (hasMin != hasMax)
  {
    error = "min and max must be provided together";
    return std::nullopt;
  }
  if (!hasMin)
  {
    return vm::bbox3d{};
  }

  const auto min = vec3FromJson(params, "min", error);
  if (!min)
  {
    return std::nullopt;
  }
  const auto max = vec3FromJson(params, "max", error);
  if (!max)
  {
    return std::nullopt;
  }
  if (
    min->x() >= max->x() || min->y() >= max->y() || min->z() >= max->z())
  {
    error = "bounds min must be smaller than max on all axes";
    return std::nullopt;
  }

  return vm::bbox3d{*min, *max};
}

std::vector<mdl::Node*> mcpFilteredNodes(
  mdl::Map& map,
  const QJsonObject& params,
  const McpSelectionQueryOptions& options,
  QString& error)
{
  auto queryBounds = std::optional<vm::bbox3d>{};
  const auto hasMin = !params.value("min").isUndefined();
  if (hasMin)
  {
    auto parsedBounds = mcpQueryBoundsFromParams(params, error);
    if (!parsedBounds)
    {
      return {};
    }
    queryBounds = *parsedBounds;
  }
  else if (!params.value("max").isUndefined())
  {
    error = "min and max must be provided together";
    return {};
  }

  auto matches = std::vector<mdl::Node*>{};
  collectFilteredNodes(
    map.worldNode(),
    map,
    params,
    queryBounds,
    options,
    optionalSize(params, "limit", 100),
    matches);

  if (options.removeDescendantMatches)
  {
    removeDescendantMatches(matches);
  }
  return matches;
}

} // namespace tb::ui
