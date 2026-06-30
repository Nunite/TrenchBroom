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
#include "mcp/McpError.h"
#include "mdl/Map.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace tb::ui
{
namespace mcp = tb::mcp;
namespace
{

constexpr auto DefaultCellSize = 64.0;
constexpr auto DefaultHeightScale = 128.0;
constexpr auto DefaultHeightSteps = 8;
constexpr auto DefaultMaxSize = 64;
constexpr auto DefaultMaxBrushes = 512;
constexpr auto DefaultAdaptiveMaxCellSpan = 4;
constexpr auto HardMaxSize = 256;
constexpr auto HardMaxBrushes = 4096;
constexpr auto SurfaceEpsilon = 0.01;

QJsonObject preMutationFailureDetails(
  QJsonObject details, const QString& recoveryAction)
{
  details.insert("mutatedDocument", false);
  details.insert("retrySafe", true);
  details.insert("recoveryAction", recoveryAction);
  return details;
}

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

struct HeightmapSurfaceCell
{
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct HeightmapSurfaceStats
{
  int surfaceCellCount = 0;
  int skippedZeroTriangles = 0;
  int triangleBrushCount = 0;
  int minCellSpan = 1;
  int maxCellSpan = DefaultAdaptiveMaxCellSpan;
  double errorTolerance = 0.0;
};

struct HeightmapPreview
{
  bool ok = false;
  bool willCommit = false;
  QString error;
  QJsonArray operations;
  QJsonObject heightmap;
  QJsonObject validation;
  QJsonArray warnings;
  QJsonObject suggestedParams;
  int brushCount = 0;
  int maxBrushes = DefaultMaxBrushes;
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

double grayHeight(
  const QImage& image,
  const int x,
  const int y,
  const int sampledWidth,
  const int sampledHeight,
  const double heightScale)
{
  const auto sourceX = sampleCoordinate(x, image.width(), sampledWidth);
  const auto sourceY = sampleCoordinate(y, image.height(), sampledHeight);
  const auto color = image.pixelColor(sourceX, sourceY);
  const auto gray = 0.299 * static_cast<double>(color.red())
                    + 0.587 * static_cast<double>(color.green())
                    + 0.114 * static_cast<double>(color.blue());
  return gray / 255.0 * heightScale;
}

std::vector<double> sampleHeights(
  const QImage& image,
  const int sampledWidth,
  const int sampledHeight,
  const double heightScale)
{
  auto result =
    std::vector<double>(static_cast<size_t>(sampledWidth * sampledHeight), 0.0);
  for (auto y = 0; y < sampledHeight; ++y)
  {
    for (auto x = 0; x < sampledWidth; ++x)
    {
      result[static_cast<size_t>(y * sampledWidth + x)] =
        grayHeight(image, x, y, sampledWidth, sampledHeight, heightScale);
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

struct JsonBounds
{
  bool valid = false;
  double minX = 0.0;
  double minY = 0.0;
  double minZ = 0.0;
  double maxX = 0.0;
  double maxY = 0.0;
  double maxZ = 0.0;
};

std::optional<std::array<double, 3>> pointFromJson(const QJsonValue& value)
{
  if (!value.isArray())
  {
    return std::nullopt;
  }
  const auto array = value.toArray();
  if (
    array.size() != 3 || !array[0].isDouble() || !array[1].isDouble()
    || !array[2].isDouble())
  {
    return std::nullopt;
  }
  return std::array<double, 3>{
    array[0].toDouble(), array[1].toDouble(), array[2].toDouble()};
}

void includePoint(JsonBounds& bounds, const std::array<double, 3>& point)
{
  if (!bounds.valid)
  {
    bounds.valid = true;
    bounds.minX = bounds.maxX = point[0];
    bounds.minY = bounds.maxY = point[1];
    bounds.minZ = bounds.maxZ = point[2];
    return;
  }

  bounds.minX = std::min(bounds.minX, point[0]);
  bounds.minY = std::min(bounds.minY, point[1]);
  bounds.minZ = std::min(bounds.minZ, point[2]);
  bounds.maxX = std::max(bounds.maxX, point[0]);
  bounds.maxY = std::max(bounds.maxY, point[1]);
  bounds.maxZ = std::max(bounds.maxZ, point[2]);
}

std::optional<JsonBounds> boundsFromOperations(const QJsonArray& operations)
{
  auto bounds = JsonBounds{};
  for (const auto& value : operations)
  {
    if (!value.isObject())
    {
      continue;
    }
    const auto operation = value.toObject();
    const auto type = operation.value("type").toString();
    if (type == "box")
    {
      const auto min = pointFromJson(operation.value("min"));
      const auto max = pointFromJson(operation.value("max"));
      if (min && max)
      {
        includePoint(bounds, *min);
        includePoint(bounds, *max);
      }
    }
    else if (type == "polyhedron")
    {
      for (const auto& pointValue : operation.value("points").toArray())
      {
        if (const auto point = pointFromJson(pointValue))
        {
          includePoint(bounds, *point);
        }
      }
    }
  }
  return bounds.valid ? std::optional<JsonBounds>{bounds} : std::nullopt;
}

QJsonObject boundsToJson(const JsonBounds& bounds)
{
  return QJsonObject{
    {"min", vec3Json(bounds.minX, bounds.minY, bounds.minZ)},
    {"max", vec3Json(bounds.maxX, bounds.maxY, bounds.maxZ)},
  };
}

QJsonArray vec3Json(
  const HeightmapOrigin& origin,
  const int x,
  const int y,
  const double z,
  const double cellSize)
{
  return vec3Json(
    origin.x + static_cast<double>(x) * cellSize,
    origin.y + static_cast<double>(y) * cellSize,
    origin.z + z);
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

double heightAt(
  const std::vector<double>& heights, const int width, const int x, const int y)
{
  return heights[static_cast<size_t>(y * width + x)];
}

double bilinearHeight(
  const double h00,
  const double h10,
  const double h11,
  const double h01,
  const double tx,
  const double ty)
{
  const auto a = h00 * (1.0 - tx) + h10 * tx;
  const auto b = h01 * (1.0 - tx) + h11 * tx;
  return a * (1.0 - ty) + b * ty;
}

bool shouldSplitSurfaceCell(
  const std::vector<double>& heights,
  const int heightWidth,
  const HeightmapSurfaceCell& cell,
  const int minCellSpan,
  const int maxCellSpan,
  const double errorTolerance)
{
  if (cell.width > maxCellSpan || cell.height > maxCellSpan)
  {
    return true;
  }
  if (cell.width <= minCellSpan && cell.height <= minCellSpan)
  {
    return false;
  }

  const auto h00 = heightAt(heights, heightWidth, cell.x, cell.y);
  const auto h10 = heightAt(heights, heightWidth, cell.x + cell.width, cell.y);
  const auto h11 =
    heightAt(heights, heightWidth, cell.x + cell.width, cell.y + cell.height);
  const auto h01 = heightAt(heights, heightWidth, cell.x, cell.y + cell.height);

  auto maxError = 0.0;
  for (auto y = cell.y; y <= cell.y + cell.height; ++y)
  {
    const auto ty = static_cast<double>(y - cell.y) / static_cast<double>(cell.height);
    for (auto x = cell.x; x <= cell.x + cell.width; ++x)
    {
      const auto tx = static_cast<double>(x - cell.x) / static_cast<double>(cell.width);
      const auto expected = bilinearHeight(h00, h10, h11, h01, tx, ty);
      maxError =
        std::max(maxError, std::abs(heightAt(heights, heightWidth, x, y) - expected));
    }
  }
  return maxError > errorTolerance;
}

void addAdaptiveSurfaceCells(
  const std::vector<double>& heights,
  const int heightWidth,
  const HeightmapSurfaceCell& cell,
  const int minCellSpan,
  const int maxCellSpan,
  const double errorTolerance,
  std::vector<HeightmapSurfaceCell>& result)
{
  if (
    shouldSplitSurfaceCell(
      heights, heightWidth, cell, minCellSpan, maxCellSpan, errorTolerance)
    && (cell.width > minCellSpan || cell.height > minCellSpan))
  {
    if (cell.width >= cell.height && cell.width > minCellSpan)
    {
      const auto leftWidth = cell.width / 2;
      addAdaptiveSurfaceCells(
        heights,
        heightWidth,
        HeightmapSurfaceCell{cell.x, cell.y, leftWidth, cell.height},
        minCellSpan,
        maxCellSpan,
        errorTolerance,
        result);
      addAdaptiveSurfaceCells(
        heights,
        heightWidth,
        HeightmapSurfaceCell{
          cell.x + leftWidth, cell.y, cell.width - leftWidth, cell.height},
        minCellSpan,
        maxCellSpan,
        errorTolerance,
        result);
      return;
    }

    if (cell.height > minCellSpan)
    {
      const auto topHeight = cell.height / 2;
      addAdaptiveSurfaceCells(
        heights,
        heightWidth,
        HeightmapSurfaceCell{cell.x, cell.y, cell.width, topHeight},
        minCellSpan,
        maxCellSpan,
        errorTolerance,
        result);
      addAdaptiveSurfaceCells(
        heights,
        heightWidth,
        HeightmapSurfaceCell{
          cell.x, cell.y + topHeight, cell.width, cell.height - topHeight},
        minCellSpan,
        maxCellSpan,
        errorTolerance,
        result);
      return;
    }
  }

  result.push_back(cell);
}

std::vector<HeightmapSurfaceCell> adaptiveSurfaceCells(
  const std::vector<double>& heights,
  const int heightWidth,
  const int cellWidth,
  const int cellHeight,
  const int minCellSpan,
  const int maxCellSpan,
  const double errorTolerance)
{
  auto result = std::vector<HeightmapSurfaceCell>{};
  addAdaptiveSurfaceCells(
    heights,
    heightWidth,
    HeightmapSurfaceCell{0, 0, cellWidth, cellHeight},
    minCellSpan,
    maxCellSpan,
    errorTolerance,
    result);
  return result;
}

void pushUniquePoint(QJsonArray& points, const QJsonArray& point)
{
  for (const auto& value : points)
  {
    if (value.toArray() == point)
    {
      return;
    }
  }
  points.push_back(point);
}

std::optional<QJsonObject> trianglePolyhedronOperation(
  const HeightmapOrigin& origin,
  const double cellSize,
  const QString& material,
  const std::array<int, 3>& xs,
  const std::array<int, 3>& ys,
  const std::array<double, 3>& heights)
{
  auto maxHeight = 0.0;
  for (const auto height : heights)
  {
    maxHeight = std::max(maxHeight, height);
  }
  if (maxHeight <= SurfaceEpsilon)
  {
    return std::nullopt;
  }

  auto points = QJsonArray{};
  for (auto i = 0u; i < xs.size(); ++i)
  {
    pushUniquePoint(points, vec3Json(origin, xs[i], ys[i], 0.0, cellSize));
  }
  for (auto i = 0u; i < xs.size(); ++i)
  {
    if (heights[i] > SurfaceEpsilon)
    {
      pushUniquePoint(points, vec3Json(origin, xs[i], ys[i], heights[i], cellSize));
    }
  }
  if (points.size() < 4)
  {
    return std::nullopt;
  }

  auto operation = QJsonObject{
    {"type", "polyhedron"},
    {"points", points},
  };
  if (!material.isEmpty())
  {
    operation.insert("material", material);
  }
  return operation;
}

QJsonArray operationsFromAdaptiveSurface(
  const std::vector<HeightmapSurfaceCell>& cells,
  const std::vector<double>& heights,
  const int heightWidth,
  const HeightmapOrigin& origin,
  const double cellSize,
  const QString& material,
  HeightmapSurfaceStats& stats)
{
  auto result = QJsonArray{};
  for (const auto& cell : cells)
  {
    const auto x0 = cell.x;
    const auto y0 = cell.y;
    const auto x1 = cell.x + cell.width;
    const auto y1 = cell.y + cell.height;

    const auto h00 = heightAt(heights, heightWidth, x0, y0);
    const auto h10 = heightAt(heights, heightWidth, x1, y0);
    const auto h11 = heightAt(heights, heightWidth, x1, y1);
    const auto h01 = heightAt(heights, heightWidth, x0, y1);

    const auto firstTriangle = trianglePolyhedronOperation(
      origin,
      cellSize,
      material,
      std::array<int, 3>{x0, x1, x1},
      std::array<int, 3>{y0, y0, y1},
      std::array<double, 3>{h00, h10, h11});
    if (firstTriangle)
    {
      result.push_back(*firstTriangle);
    }
    else
    {
      ++stats.skippedZeroTriangles;
    }

    const auto secondTriangle = trianglePolyhedronOperation(
      origin,
      cellSize,
      material,
      std::array<int, 3>{x0, x1, x0},
      std::array<int, 3>{y0, y1, y1},
      std::array<double, 3>{h00, h11, h01});
    if (secondTriangle)
    {
      result.push_back(*secondTriangle);
    }
    else
    {
      ++stats.skippedZeroTriangles;
    }
  }

  stats.surfaceCellCount = static_cast<int>(cells.size());
  stats.triangleBrushCount = result.size();
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

QJsonArray heightmapWarnings(
  const QImage& image,
  const int sampledWidth,
  const int sampledHeight,
  const double heightScale,
  const int brushCount,
  const int maxBrushes,
  const QString& mode)
{
  auto warnings = QJsonArray{};
  if (brushCount > maxBrushes)
  {
    warnings.push_back("tooManyBrushes");
  }
  if (sampledWidth <= 4 || sampledHeight <= 4)
  {
    warnings.push_back("terrainTooCoarse");
  }
  if (heightScale < 16.0)
  {
    warnings.push_back("heightRangeTooFlat");
  }
  if (heightScale > 1024.0)
  {
    warnings.push_back("heightRangeTooSteep");
  }
  if (mode == "terraced_brushes" && image.width() * image.height() > 4096)
  {
    warnings.push_back("adaptiveWouldHelp");
  }
  return warnings;
}

HeightmapPreview buildHeightmapPreview(const QJsonObject& params)
{
  auto preview = HeightmapPreview{};
  const auto imagePath = params.value("imagePath").toString().trimmed();
  if (imagePath.isEmpty())
  {
    preview.error = "heightmap_preview_grayscale requires imagePath";
    return preview;
  }

  const auto mode = params.value("mode").toString("terraced_brushes").trimmed();
  const auto normalizedMode = mode.toLower();
  const auto adaptiveSurface =
    normalizedMode == "adaptive_surface" || normalizedMode == "adaptive_sloped";
  if (normalizedMode != "terraced_brushes" && !adaptiveSurface)
  {
    preview.error = "mode must be terraced_brushes, adaptive_surface, or adaptive_sloped";
    return preview;
  }

  auto error = QString{};
  const auto origin = originFromParams(params, error);
  if (!origin)
  {
    preview.error = error;
    return preview;
  }

  const auto cellSize = optionalDouble(params, "cellSize", DefaultCellSize);
  const auto heightScale = optionalDouble(params, "heightScale", DefaultHeightScale);
  const auto heightSteps = optionalInt(params, "heightSteps", DefaultHeightSteps);
  const auto maxSize =
    std::clamp(optionalInt(params, "maxSize", DefaultMaxSize), 1, HardMaxSize);
  const auto maxBrushes =
    std::clamp(optionalInt(params, "maxBrushes", DefaultMaxBrushes), 1, HardMaxBrushes);
  preview.maxBrushes = maxBrushes;

  if (!std::isfinite(cellSize) || cellSize <= 0.0)
  {
    preview.error = "cellSize must be greater than zero";
    return preview;
  }
  if (!std::isfinite(heightScale) || heightScale <= 0.0)
  {
    preview.error = "heightScale must be greater than zero";
    return preview;
  }
  if (heightSteps <= 0 || heightSteps > 256)
  {
    preview.error = "heightSteps must be between 1 and 256";
    return preview;
  }

  const auto fileInfo = QFileInfo{imagePath};
  if (!fileInfo.exists() || !fileInfo.isFile())
  {
    preview.error = QString{"Image file does not exist: %1"}.arg(imagePath);
    return preview;
  }

  auto image = QImage{imagePath};
  if (image.isNull() || image.width() <= 0 || image.height() <= 0)
  {
    preview.error = QString{"Could not read image file: %1"}.arg(imagePath);
    return preview;
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

  const auto material = params.value("material").toString();
  auto operations = QJsonArray{};
  auto skippedCells = 0;
  auto brushCount = 0;
  auto heightmapInfoExtras = QJsonObject{};

  if (adaptiveSurface)
  {
    const auto surfaceWidth = std::max(1, sampledWidth);
    const auto surfaceHeight = std::max(1, sampledHeight);
    const auto vertexWidth = surfaceWidth + 1;
    const auto vertexHeight = surfaceHeight + 1;
    const auto heights = sampleHeights(image, vertexWidth, vertexHeight, heightScale);

    const auto requestedMinCellSize = optionalDouble(params, "minCellSize", cellSize);
    const auto requestedMaxCellSize =
      optionalDouble(params, "maxCellSize", cellSize * DefaultAdaptiveMaxCellSpan);
    if (!std::isfinite(requestedMinCellSize) || requestedMinCellSize <= 0.0)
    {
      preview.error = "minCellSize must be greater than zero";
      return preview;
    }
    if (!std::isfinite(requestedMaxCellSize) || requestedMaxCellSize <= 0.0)
    {
      preview.error = "maxCellSize must be greater than zero";
      return preview;
    }
    if (requestedMinCellSize > requestedMaxCellSize)
    {
      preview.error = "minCellSize must be less than or equal to maxCellSize";
      return preview;
    }

    const auto minCellSpan = std::clamp(
      static_cast<int>(std::round(requestedMinCellSize / cellSize)),
      1,
      std::max(surfaceWidth, surfaceHeight));
    const auto maxCellSpan = std::clamp(
      static_cast<int>(std::round(requestedMaxCellSize / cellSize)),
      minCellSpan,
      std::max(surfaceWidth, surfaceHeight));
    const auto errorTolerance =
      optionalDouble(params, "errorTolerance", std::max(1.0, heightScale / 16.0));
    if (!std::isfinite(errorTolerance) || errorTolerance < 0.0)
    {
      preview.error = "errorTolerance must be zero or greater";
      return preview;
    }

    auto stats = HeightmapSurfaceStats{};
    stats.minCellSpan = minCellSpan;
    stats.maxCellSpan = maxCellSpan;
    stats.errorTolerance = errorTolerance;
    const auto cells = adaptiveSurfaceCells(
      heights,
      vertexWidth,
      surfaceWidth,
      surfaceHeight,
      minCellSpan,
      maxCellSpan,
      errorTolerance);
    operations = operationsFromAdaptiveSurface(
      cells, heights, vertexWidth, *origin, cellSize, material, stats);
    skippedCells = stats.skippedZeroTriangles;
    brushCount = stats.triangleBrushCount;
    heightmapInfoExtras = QJsonObject{
      {"surfaceCellCount", stats.surfaceCellCount},
      {"skippedZeroTriangles", stats.skippedZeroTriangles},
      {"triangleBrushCount", stats.triangleBrushCount},
      {"minCellSize", static_cast<double>(minCellSpan) * cellSize},
      {"maxCellSize", static_cast<double>(maxCellSpan) * cellSize},
      {"errorTolerance", errorTolerance},
    };
  }
  else
  {
    const auto levels = sampleLevels(image, sampledWidth, sampledHeight, heightSteps);
    skippedCells = static_cast<int>(std::ranges::count(levels, 0));
    const auto rects = mergeSameHeightRects(levels, sampledWidth, sampledHeight);
    operations =
      operationsFromRects(rects, *origin, cellSize, heightScale, heightSteps, material);
    brushCount = static_cast<int>(rects.size());
  }

  auto heightmapInfo = heightmapInfoJson(
    image,
    sampledWidth,
    sampledHeight,
    heightSteps,
    cellSize,
    heightScale,
    skippedCells,
    brushCount,
    adaptiveSurface ? "adaptive_surface" : "terraced_brushes");
  for (auto it = heightmapInfoExtras.begin(); it != heightmapInfoExtras.end(); ++it)
  {
    heightmapInfo.insert(it.key(), it.value());
  }

  const auto operationBounds = boundsFromOperations(operations);
  const auto outputBounds =
    operationBounds ? boundsToJson(*operationBounds)
                    : QJsonObject{
                        {"min", QJsonArray{origin->x, origin->y, origin->z}},
                        {"max",
                         QJsonArray{
                           origin->x + static_cast<double>(sampledWidth) * cellSize,
                           origin->y + static_cast<double>(sampledHeight) * cellSize,
                           origin->z + heightScale}},
                      };
  const auto heightRange =
    operationBounds
      ? QJsonObject{{"min", operationBounds->minZ}, {"max", operationBounds->maxZ}}
      : QJsonObject{{"min", origin->z}, {"max", origin->z + heightScale}};
  heightmapInfo.insert(
    "sourceImageSize", QJsonObject{{"width", image.width()}, {"height", image.height()}});
  heightmapInfo.insert(
    "sampleGrid", QJsonObject{{"width", sampledWidth}, {"height", sampledHeight}});
  heightmapInfo.insert("outputBounds", outputBounds);
  heightmapInfo.insert("heightRange", heightRange);

  preview.operations = operations;
  preview.brushCount = brushCount;
  preview.heightmap = heightmapInfo;
  preview.warnings = heightmapWarnings(
    image,
    sampledWidth,
    sampledHeight,
    heightScale,
    brushCount,
    maxBrushes,
    adaptiveSurface ? "adaptive_surface" : "terraced_brushes");
  preview.suggestedParams = QJsonObject{
    {"maxSize", brushCount > maxBrushes ? std::max(1, maxSize / 2) : maxSize},
    {"maxBrushes", std::min(HardMaxBrushes, std::max(maxBrushes, brushCount))},
    {"mode",
     brushCount > maxBrushes ? "adaptive_surface"
     : adaptiveSurface       ? "adaptive_surface"
                             : "terraced_brushes"},
  };
  preview.willCommit = !operations.isEmpty() && brushCount <= maxBrushes;
  preview.validation = QJsonObject{
    {"valid", preview.willCommit},
    {"errors",
     operations.isEmpty() ? QJsonArray{"heightmap produced no non-zero terrain cells"}
     : brushCount > maxBrushes
       ? QJsonArray{QString{"heightmap would create %1 brushes; increase maxBrushes "
                            "or lower maxSize/heightSteps"}
                      .arg(brushCount)}
       : QJsonArray{}},
    {"operationCount", brushCount},
    {"brushCount", brushCount},
  };
  preview.ok = true;
  return preview;
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

McpBridgeToolResult heightmapPreviewGrayscaleResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return heightmapPreviewGrayscaleForMapResult(mapWindow->document().map(), params);
}

McpBridgeToolResult heightmapPreviewGrayscaleForMapResult(
  mdl::Map&, const QJsonObject& params)
{
  const auto preview = buildHeightmapPreview(params);
  if (!preview.ok)
  {
    return invalidParamsFailure(preview.error);
  }

  return McpBridgeToolResult::success(QJsonObject{
    {"valid", preview.willCommit},
    {"willCommit", preview.willCommit},
    {"estimatedBrushCount", preview.brushCount},
    {"maxBrushes", preview.maxBrushes},
    {"sourceImageSize", preview.heightmap.value("sourceImageSize")},
    {"sampleGrid", preview.heightmap.value("sampleGrid")},
    {"outputBounds", preview.heightmap.value("outputBounds")},
    {"heightRange", preview.heightmap.value("heightRange")},
    {"warnings", preview.warnings},
    {"suggestedParams", preview.suggestedParams},
    {"validation", preview.validation},
    {"heightmap", preview.heightmap},
  });
}

McpBridgeToolResult heightmapImportGrayscaleForMapResult(
  mdl::Map& map,
  const QString&,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  const auto preview = buildHeightmapPreview(params);
  if (!preview.ok)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InvalidParams,
      preview.error,
      preMutationFailureDetails(
        QJsonObject{{"targetSource", "heightmap_preview"}},
        "fix_heightmap_parameters_then_retry"));
  }
  if (preview.operations.isEmpty())
  {
    return McpBridgeToolResult::success(QJsonObject{
      {"valid", false},
      {"willCommit", false},
      {"validation", preview.validation},
      {"heightmap", preview.heightmap},
      {"warnings", preview.warnings},
      {"suggestedParams", preview.suggestedParams},
    });
  }
  if (preview.brushCount > preview.maxBrushes)
  {
    return McpBridgeToolResult::success(QJsonObject{
      {"valid", false},
      {"willCommit", false},
      {"validation", preview.validation},
      {"heightmap", preview.heightmap},
      {"warnings", preview.warnings},
      {"suggestedParams", preview.suggestedParams},
    });
  }

  auto batchParams = QJsonObject{
    {"name", params.value("name").toString("MCP: Import grayscale heightmap")},
    {"grid", params.value("grid").toDouble(1.0)},
    {"select", optionalBool(params, "select", true)},
    {"detail", params.value("detail").toString("summary")},
    {"operations", preview.operations},
  };
  const auto idsMode = params.value("idsMode").toString().trimmed().toLower();
  if (!idsMode.isEmpty())
  {
    batchParams.insert(
      "detail",
      idsMode == "full" || idsMode == "ids" || idsMode == "sample" ? "ids" : "summary");
  }
  const auto material = params.value("material").toString();
  if (!material.isEmpty())
  {
    batchParams.insert("material", material);
  }

  auto result = blockoutCreateBatchForMapResult(
    map, "blockout_create_batch", batchParams, history, nextOperationIndex);
  if (result.ok)
  {
    result.result.insert("heightmap", preview.heightmap);
    result.result.insert(
      "preview",
      QJsonObject{
        {"estimatedBrushCount", preview.brushCount},
        {"warnings", preview.warnings},
        {"suggestedParams", preview.suggestedParams},
      });
    result.result.insert("toolName", "heightmap_import_grayscale");
    if (
      !history.empty()
      && history.back().operationId == result.result.value("operationId").toString())
    {
      history.back().toolName = "heightmap_import_grayscale";
      auto detail = history.back().detail();
      detail.insert("heightmap", preview.heightmap);
      detail.insert("heightmapInput", params);
      history.back().setDetail(detail);
      history.back().setSummary(result.result);
    }
  }
  return result;
}

} // namespace tb::ui
