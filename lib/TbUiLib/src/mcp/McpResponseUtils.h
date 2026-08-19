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

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <algorithm>

namespace tb::ui
{

constexpr auto McpDefaultIdSampleLimit = 12;

inline QString mcpIdsModeFromParams(const QJsonObject& params)
{
  const auto idsMode = params.value("idsMode").toString().trimmed().toLower();
  if (!idsMode.isEmpty())
  {
    return idsMode;
  }

  const auto detail = params.value("detail").toString("summary").trimmed().toLower();
  if (detail == "ids" || detail == "full")
  {
    return "full";
  }
  if (detail == "sample" || detail == "none" || detail == "count")
  {
    return detail;
  }
  return "count";
}

inline QJsonArray mcpSampleArray(const QJsonArray& values, const int limit)
{
  auto result = QJsonArray{};
  const auto count = std::min(values.size(), static_cast<qsizetype>(limit));
  for (auto i = qsizetype{0}; i < count; ++i)
  {
    result.push_back(values[i]);
  }
  return result;
}

inline void mcpApplyIdsMode(
  QJsonObject& result,
  const QString& pluralKey,
  const QString& sampleKey,
  const QString& countKey,
  const QJsonArray& ids,
  const QString& idsMode)
{
  const auto mode = idsMode.trimmed().toLower();
  result.remove(pluralKey);
  result.remove(sampleKey);
  result.insert(countKey, ids.size());

  if (mode == "full")
  {
    result.insert(pluralKey, ids);
  }
  else if (mode == "sample")
  {
    result.insert(sampleKey, mcpSampleArray(ids, McpDefaultIdSampleLimit));
  }
  else if (mode == "none" || mode == "count" || mode.isEmpty())
  {
    return;
  }
  else
  {
    auto warnings = result.value("warnings").toArray();
    warnings.push_back(
      QString{"unknownIdsMode: %1; returned %2 only"}.arg(idsMode, countKey));
    result.insert("warnings", warnings);
  }
}

inline void mcpApplyChangedObjectIdsMode(
  QJsonObject& result, const QJsonArray& changedObjectIds, const QString& idsMode)
{
  mcpApplyIdsMode(
    result,
    "changedObjectIds",
    "changedObjectIdSample",
    "changedObjectCount",
    changedObjectIds,
    idsMode);
}

inline void mcpApplyDeletedObjectIdsMode(
  QJsonObject& result, const QJsonArray& deletedObjectIds, const QString& idsMode)
{
  mcpApplyIdsMode(
    result,
    "deletedObjectIds",
    "deletedObjectIdSample",
    "deletedObjectCount",
    deletedObjectIds,
    idsMode);
}

} // namespace tb::ui
