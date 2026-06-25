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

#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>

#include "McpBridgeServerTools.h"
#include "mdl/Map.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace tb::ui
{
namespace
{

constexpr auto DefaultCellSize = 64.0;
constexpr auto DefaultHeightScale = 128.0;
constexpr auto DefaultHeightSteps = 8;
constexpr auto DefaultMaxSize = 64;
constexpr auto DefaultMaxBrushes = 512;
constexpr auto HardMaxSize = 256;
constexpr auto HardMaxBrushes = 4096;

struct HeightmapOrigin
{
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct HeightmapRect
{
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int level = 0;
};

double optionalDouble(
  const QJsonObject& params, const QString& key, const double defaultValue)
{
  const auto value = params.value(key);
  return value.isDouble() ? value.toDouble(defaultValue) : defaultValue;
}

int optionalInt(const QJsonObject& params, const QString& key, const int defaultValue)
{
  const auto value = params.value(key);
  return value.isDouble() ? value.toInt(defaultValue) : defaultValue;
}

bool optionalBool(const QJsonObject& params, const QString& key, const bool defaultValue)
{
  const auto value = params.value(key);
  return value.isBool() ? value.toBool() : defaultValue;
}

std::optional<HeightmapOrigin> originFromParams(const QJsonObject& params, QString& error)
{
  const auto value = params.value("origin");
  if (value.isUndefined())
  {
    return HeightmapOrigin{};
  }
  if (!value.isArray())
  {
    error = "origin must be an array of three numbers";
    return std::nullopt;
  }

  const auto array = value.toArray();
  if (array.size() != 3)
  {
    error = "origin must contain exactly three numbers";
    return std::nullopt;
  }
  if (!array[0].isDouble() || !array[1].isDouble() || !array[2].isDouble())
  {
    error = "origin values must be numbers";
    return std::nullopt;
  }

  const auto origin =
    HeightmapOrigin{array[0].toDouble(), array[1].toDouble(), array[2].toDouble()};
  if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z))
  {
    error = "origin values must be finite";
    return std::nullopt;
  }
  return origin;
}

int sampleCoordinate(const int index, const int sourceSize, const int sampledSize)
{
  if (sampledSize <= 1)
  {
    return 0;
  }
  return std::clamp(
    static_cast<int>(std::round(
      static_cast<double>(index) * static_cast<double>(sourceSize - 1)
      / static_cast<double>(sampledSize - 1))),
    0,
    sourceSize - 1);
}

int grayLevel(
  const QImage& image,
  const int x,
  const int y,
  const int sampledWidth,
  const int sampledHeight,
  const int heightSteps)
{
  const auto sourceX = sampleCoordinate(x, image.width(), sampledWidth);
  const auto sourceY = sampleCoordinate(y, image.height(), sampledHeight);
  const auto color = image.pixelColor(sourceX, sourceY);
  const auto gray = 0.299 * static_cast<double>(color.red())
                    + 0.587 * static_cast<double>(color.green())
                    + 0.114 * static_cast<double>(color.blue());
  return std::clamp(
    static_cast<int>(std::round(gray / 255.0 * static_cast<double>(heightSteps))),
    0,
    heightSteps);
}

std::vector<int> sampleLevels(
  const QImage& image,
  const int sampledWidth,
  const int sampledHeight,
  const int heightSteps)
{
  auto result = std::vector<int>(static_cast<size_t>(sampledWidth * sampledHeight), 0);
  for (auto y = 0; y < sampledHeight; ++y)
  {
    for (auto x = 0; x < sampledWidth; ++x)
    {
      result[static_cast<size_t>(y * sampledWidth + x)] =
        grayLevel(image, x, y, sampledWidth, sampledHeight, heightSteps);
    }
  }
  return result;
}

std::vector<HeightmapRect> mergeSameHeightRects(
  const std::vector<int>& levels, const int width, const int height)
{
  auto used = std::vector<bool>(levels.size(), false);
  auto result = std::vector<HeightmapRect>{};

  const auto levelAt = [&](const int x, const int y) {
    return levels[static_cast<size_t>(y * width + x)];
  };
  const auto usedAt = [&](const int x, const int y) {
    return used[static_cast<size_t>(y * width + x)];
  };
  const auto markUsed = [&](const int x, const int y) {
    used[static_cast<size_t>(y * width + x)] = true;
  };

  for (auto y = 0; y < height; ++y)
  {
    for (auto x = 0; x < width; ++x)
    {
      if (usedAt(x, y) || levelAt(x, y) <= 0)
      {
        continue;
      }

      const auto level = levelAt(x, y);
      auto rectWidth = 1;
      while (x + rectWidth < width && !usedAt(x + rectWidth, y)
             && levelAt(x + rectWidth, y) == level)
      {
        ++rectWidth;
      }

      auto rectHeight = 1;
      auto canGrow = true;
      while (y + rectHeight < height && canGrow)
      {
        for (auto rx = 0; rx < rectWidth; ++rx)
        {
          if (usedAt(x + rx, y + rectHeight) || levelAt(x + rx, y + rectHeight) != level)
          {
            canGrow = false;
            break;
          }
        }
        if (canGrow)
        {
          ++rectHeight;
        }
      }

      for (auto ry = 0; ry < rectHeight; ++ry)
      {
        for (auto rx = 0; rx < rectWidth; ++rx)
        {
          markUsed(x + rx, y + ry);
        }
      }

      result.push_back(HeightmapRect{x, y, rectWidth, rectHeight, level});
    }
  }

  return result;
}

QJsonArray vec3Json(const double x, const double y, const double z)
{
  return QJsonArray{x, y, z};
}

QJsonArray operationsFromRects(
  const std::vector<HeightmapRect>& rects,
  const HeightmapOrigin& origin,
  const double cellSize,
  const double heightScale,
  const int heightSteps,
  const QString& material)
{
  auto result = QJsonArray{};
  for (const auto& rect : rects)
  {
    const auto minX = origin.x + static_cast<double>(rect.x) * cellSize;
    const auto minY = origin.y + static_cast<double>(rect.y) * cellSize;
    const auto maxX = origin.x + static_cast<double>(rect.x + rect.width) * cellSize;
    const auto maxY = origin.y + static_cast<double>(rect.y + rect.height) * cellSize;
    const auto maxZ =
      origin.z
      + static_cast<double>(rect.level) * heightScale / static_cast<double>(heightSteps);
    auto operation = QJsonObject{
      {"type", "box"},
      {"min", vec3Json(minX, minY, origin.z)},
      {"max", vec3Json(maxX, maxY, maxZ)},
    };
    if (!material.isEmpty())
    {
      operation.insert("material", material);
    }
    result.push_back(std::move(operation));
  }
  return result;
}

QJsonObject heightmapInfoJson(
  const QImage& image,
  const int sampledWidth,
  const int sampledHeight,
  const int heightSteps,
  const double cellSize,
  const double heightScale,
  const int skippedCells,
  const int brushCount,
  const QString& mode)
{
  return QJsonObject{
    {"sourceWidth", image.width()},
    {"sourceHeight", image.height()},
    {"sampledWidth", sampledWidth},
    {"sampledHeight", sampledHeight},
    {"heightSteps", heightSteps},
    {"cellSize", cellSize},
    {"heightScale", heightScale},
    {"skippedZeroHeightCells", skippedCells},
    {"mergedBrushCount", brushCount},
    {"mode", mode},
  };
}

} // namespace

McpBridgeToolResult heightmapImportGrayscaleResult(
  AppController& appController,
  const QString&,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return heightmapImportGrayscaleForMapResult(
    mapWindow->document().map(),
    "heightmap_import_grayscale",
    params,
    history,
    nextOperationIndex);
}

McpBridgeToolResult heightmapImportGrayscaleForMapResult(
  mdl::Map& map,
  const QString&,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  const auto imagePath = params.value("imagePath").toString().trimmed();
  if (imagePath.isEmpty())
  {
    return invalidParamsFailure("heightmap_import_grayscale requires imagePath");
  }

  const auto mode = params.value("mode").toString("terraced_brushes").trimmed();
  if (mode != "terraced_brushes")
  {
    return invalidParamsFailure(
      "heightmap_import_grayscale currently supports mode=terraced_brushes only");
  }

  auto error = QString{};
  const auto origin = originFromParams(params, error);
  if (!origin)
  {
    return invalidParamsFailure(error);
  }

  const auto cellSize = optionalDouble(params, "cellSize", DefaultCellSize);
  const auto heightScale = optionalDouble(params, "heightScale", DefaultHeightScale);
  const auto heightSteps = optionalInt(params, "heightSteps", DefaultHeightSteps);
  const auto maxSize =
    std::clamp(optionalInt(params, "maxSize", DefaultMaxSize), 1, HardMaxSize);
  const auto maxBrushes =
    std::clamp(optionalInt(params, "maxBrushes", DefaultMaxBrushes), 1, HardMaxBrushes);

  if (!std::isfinite(cellSize) || cellSize <= 0.0)
  {
    return invalidParamsFailure("cellSize must be greater than zero");
  }
  if (!std::isfinite(heightScale) || heightScale <= 0.0)
  {
    return invalidParamsFailure("heightScale must be greater than zero");
  }
  if (heightSteps <= 0 || heightSteps > 256)
  {
    return invalidParamsFailure("heightSteps must be between 1 and 256");
  }

  const auto fileInfo = QFileInfo{imagePath};
  if (!fileInfo.exists() || !fileInfo.isFile())
  {
    return invalidParamsFailure(QString{"Image file does not exist: %1"}.arg(imagePath));
  }

  auto image = QImage{imagePath};
  if (image.isNull() || image.width() <= 0 || image.height() <= 0)
  {
    return invalidParamsFailure(QString{"Could not read image file: %1"}.arg(imagePath));
  }

  const auto scale = std::max(
    1.0,
    std::max(
      static_cast<double>(image.width()) / static_cast<double>(maxSize),
      static_cast<double>(image.height()) / static_cast<double>(maxSize)));
  const auto sampledWidth =
    std::max(1, static_cast<int>(std::ceil(static_cast<double>(image.width()) / scale)));
  const auto sampledHeight =
    std::max(1, static_cast<int>(std::ceil(static_cast<double>(image.height()) / scale)));

  const auto levels = sampleLevels(image, sampledWidth, sampledHeight, heightSteps);
  const auto skippedCells = static_cast<int>(std::ranges::count(levels, 0));
  const auto rects = mergeSameHeightRects(levels, sampledWidth, sampledHeight);
  if (rects.empty())
  {
    return McpBridgeToolResult::success(QJsonObject{
      {"valid", false},
      {"validation",
       QJsonObject{
         {"valid", false},
         {"errors", QJsonArray{"heightmap produced no non-zero terrain cells"}},
         {"operationCount", 0},
         {"brushCount", 0},
       }},
      {"heightmap",
       heightmapInfoJson(
         image,
         sampledWidth,
         sampledHeight,
         heightSteps,
         cellSize,
         heightScale,
         skippedCells,
         0,
         mode)},
    });
  }
  if (static_cast<int>(rects.size()) > maxBrushes)
  {
    return McpBridgeToolResult::success(QJsonObject{
      {"valid", false},
      {"validation",
       QJsonObject{
         {"valid", false},
         {"errors",
          QJsonArray{QString{"heightmap would create %1 brushes; increase maxBrushes "
                             "or lower maxSize/heightSteps"}
                       .arg(rects.size())}},
         {"operationCount", static_cast<int>(rects.size())},
         {"brushCount", static_cast<int>(rects.size())},
       }},
      {"heightmap",
       heightmapInfoJson(
         image,
         sampledWidth,
         sampledHeight,
         heightSteps,
         cellSize,
         heightScale,
         skippedCells,
         static_cast<int>(rects.size()),
         mode)},
    });
  }

  const auto material = params.value("material").toString();
  auto batchParams = QJsonObject{
    {"name", params.value("name").toString("MCP: Import grayscale heightmap")},
    {"grid", params.value("grid").toDouble(1.0)},
    {"select", optionalBool(params, "select", true)},
    {"detail", params.value("detail").toString("summary")},
    {"operations",
     operationsFromRects(rects, *origin, cellSize, heightScale, heightSteps, material)},
  };
  if (!material.isEmpty())
  {
    batchParams.insert("material", material);
  }

  auto result = blockoutCreateBatchForMapResult(
    map, "blockout_create_batch", batchParams, history, nextOperationIndex);
  if (result.ok)
  {
    const auto heightmapInfo = heightmapInfoJson(
      image,
      sampledWidth,
      sampledHeight,
      heightSteps,
      cellSize,
      heightScale,
      skippedCells,
      static_cast<int>(rects.size()),
      mode);
    result.result.insert("heightmap", heightmapInfo);
    result.result.insert("toolName", "heightmap_import_grayscale");
    if (
      !history.empty()
      && history.back().operationId == result.result.value("operationId").toString())
    {
      history.back().toolName = "heightmap_import_grayscale";
      auto detail = history.back().detail();
      detail.insert("heightmap", heightmapInfo);
      detail.insert("heightmapInput", params);
      history.back().setDetail(detail);
      history.back().setSummary(result.result);
    }
  }
  return result;
}

} // namespace tb::ui
