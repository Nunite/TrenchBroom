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

#include <QJsonArray>
#include <QJsonObject>

#include "McpToolCatalogInternal.h"
#include "mcp/McpMode.h"

#include <map>

namespace tb::mcp
{

QJsonObject toMcpToolJson(const McpToolDefinition& tool)
{
  return QJsonObject{
    {"name", tool.name},
    {"description", tool.description},
    {"inputSchema", tool.inputSchema},
  };
}

QJsonObject toMcpToolDiagnosticJson(
  const McpToolDefinition& tool, const McpMode currentMode)
{
  return QJsonObject{
    {"name", tool.name},
    {"requiredMode", modeName(tool.requiredMode)},
    {"availableInCurrentMode", allowsMode(currentMode, tool.requiredMode)},
    {"mutatesDocument", tool.mutatesDocument},
    {"implemented", tool.implemented},
    {"category", tool.category},
    {"expert", tool.expert},
    {"lifecycle", tool.lifecycle},
    {"costClass", toolCostClassName(tool.costClass)},
    {"timeoutMs", toolResponseTimeoutMs(tool.costClass)},
  };
}

QJsonObject toMcpToolSummaryJson(
  const McpToolDefinition& tool, const McpToolProfile profile)
{
  return QJsonObject{
    {"name", tool.name},
    {"description", tool.description},
    {"category", tool.category},
    {"expert", tool.expert},
    {"lifecycle", tool.lifecycle},
    {"requiredMode", modeName(tool.requiredMode)},
    {"visibleInCurrentProfile", visibleInProfile(tool, profile)},
    {"costClass", toolCostClassName(tool.costClass)},
    {"timeoutMs", toolResponseTimeoutMs(tool.costClass)},
  };
}

QJsonArray toolsListJson(
  const McpMode mode, const bool implementedOnly, const McpToolProfile profile)
{
  auto result = QJsonArray{};
  for (const auto& tool : defaultToolCatalog())
  {
    if (implementedOnly && !tool.implemented)
    {
      continue;
    }
    if (!allowsMode(mode, tool.requiredMode))
    {
      continue;
    }
    if (!visibleInProfile(tool, profile))
    {
      continue;
    }
    result.push_back(toMcpToolJson(tool));
  }
  return result;
}

QJsonArray toolsListJson(const McpMode mode, const bool implementedOnly)
{
  return toolsListJson(mode, implementedOnly, McpToolProfile::Modeling);
}

QJsonArray toolsListJson(const McpMode mode)
{
  return toolsListJson(mode, true, McpToolProfile::Modeling);
}

QJsonArray toolsSummaryJson(
  const McpMode mode, const bool implementedOnly, const McpToolProfile profile)
{
  auto result = QJsonArray{};
  for (const auto& tool : defaultToolCatalog())
  {
    if (implementedOnly && !tool.implemented)
    {
      continue;
    }
    if (!allowsMode(mode, tool.requiredMode))
    {
      continue;
    }
    if (!visibleInProfile(tool, profile))
    {
      continue;
    }
    result.push_back(toMcpToolSummaryJson(tool, profile));
  }
  return result;
}

QJsonObject toolProfileStatsJson(
  const McpMode mode, const bool implementedOnly, const McpToolProfile profile)
{
  auto categoryCounts = std::map<QString, int>{};
  auto lifecycleCounts = std::map<QString, int>{};
  auto implementedToolCount = 0;
  auto expertToolCount = 0;

  for (const auto& tool : defaultToolCatalog())
  {
    if (implementedOnly && !tool.implemented)
    {
      continue;
    }
    if (!allowsMode(mode, tool.requiredMode))
    {
      continue;
    }
    if (!visibleInProfile(tool, profile))
    {
      continue;
    }

    ++implementedToolCount;
    ++categoryCounts[tool.category];
    ++lifecycleCounts[tool.lifecycle];
    if (tool.expert)
    {
      ++expertToolCount;
    }
  }

  auto categoryCountsJson = QJsonObject{};
  for (const auto& [category, count] : categoryCounts)
  {
    categoryCountsJson.insert(category, count);
  }

  auto lifecycleCountsJson = QJsonObject{};
  for (const auto& [lifecycle, count] : lifecycleCounts)
  {
    lifecycleCountsJson.insert(lifecycle, count);
  }

  return QJsonObject{
    {"implementedToolCount", implementedToolCount},
    {"expertToolCount", expertToolCount},
    {"toolCategoryCounts", categoryCountsJson},
    {"toolLifecycleCounts", lifecycleCountsJson},
  };
}

QJsonArray toolDiagnosticsJson(const McpMode currentMode)
{
  auto result = QJsonArray{};
  for (const auto& tool : defaultToolCatalog())
  {
    result.push_back(toMcpToolDiagnosticJson(tool, currentMode));
  }
  return result;
}

} // namespace tb::mcp
