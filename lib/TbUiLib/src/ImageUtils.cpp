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

#include "ui/ImageUtils.h"

#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPolygonF>
#include <QSvgRenderer>
#include <QThread>

#include "ui/QPathUtils.h"
#include "ui/QThreadUtils.h"
#include "ui/SystemPaths.h"

#include "kd/contracts.h"

#include <map>

namespace tb::ui
{
namespace
{

thread_local std::string currentSvgRenderPathValue;

QString imagePathToString(const std::filesystem::path& imagePath)
{
  const auto fullPath = imagePath.is_absolute()
                          ? imagePath
                          : SystemPaths::findResourceFile("images" / imagePath);
  return pathAsQPath(fullPath);
}

QImage createDisabledState(const QImage& image)
{
  // Convert to greyscale, divide the opacity by 3
  auto disabledImage = image.convertToFormat(QImage::Format_ARGB32);
  const auto w = disabledImage.width();
  const auto h = disabledImage.height();
  for (int y = 0; y < h; ++y)
  {
    auto* row = reinterpret_cast<QRgb*>(disabledImage.scanLine(y));
    for (int x = 0; x < w; ++x)
    {
      const auto oldPixel = row[x];
      const auto grey = (qRed(oldPixel) + qGreen(oldPixel) + qBlue(oldPixel)) / 3;
      const auto alpha = qAlpha(oldPixel) / 3;
      row[x] = qRgba(grey, grey, grey, alpha);
    }
  }

  return disabledImage;
}

bool isCircleToolSvg(const QString& imagePathString)
{
  return imagePathString.endsWith("CircleEdgeAligned.svg")
         || imagePathString.endsWith("CircleVertexAligned.svg")
         || imagePathString.endsWith("CircleScalable.svg");
}

QImage renderCircleToolImage(const QString& imagePathString, const qreal devicePixelRatio)
{
  auto image = QImage{
    int(24 * devicePixelRatio),
    int(24 * devicePixelRatio),
    QImage::Format_ARGB32_Premultiplied};
  image.setDevicePixelRatio(devicePixelRatio);
  image.fill(Qt::transparent);

  auto painter = QPainter{&image};
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.scale(devicePixelRatio, devicePixelRatio);

  painter.setPen(QPen{QColor{0xA0, 0x00, 0x00}, 1.0});
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(QRectF{3.5, 3.5, 17.0, 17.0});

  const auto orange = QColor{0xFF, 0x7F, 0x01};
  painter.setPen(QPen{orange, 1.0});

  if (imagePathString.endsWith("CircleScalable.svg"))
  {
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(QPolygonF{
      {14.5, 3.5},
      {18.5, 5.5},
      {20.5, 9.5},
      {20.5, 14.5},
      {18.5, 18.5},
      {14.5, 20.5},
      {9.5, 20.5},
      {5.5, 18.5},
      {3.5, 14.5},
      {3.5, 9.5},
      {5.5, 5.5},
      {9.5, 3.5}});

    painter.setPen(Qt::NoPen);
    painter.setBrush(orange);
    painter.drawRect(QRectF{7.0, 7.0, 3.0, 3.0});
    painter.drawRect(QRectF{14.0, 7.0, 3.0, 3.0});
    painter.drawRect(QRectF{14.0, 14.0, 3.0, 3.0});
    painter.drawRect(QRectF{7.0, 14.0, 3.0, 3.0});
  }
  else
  {
    painter.setBrush(QColor{0x7F, 0x7F, 0x7F, 63});
    painter.drawPolygon(
      imagePathString.endsWith("CircleVertexAligned.svg")
        ? QPolygonF{{18.0, 6.0}, {20.5, 12.0}, {18.0, 18.0}, {12.0, 20.5}, {6.0, 18.0}, {3.5, 12.0}, {6.0, 6.0}, {12.0, 3.5}}
        : QPolygonF{
            {15.5, 3.5},
            {20.5, 8.5},
            {20.5, 15.5},
            {15.5, 20.5},
            {8.5, 20.5},
            {3.5, 15.5},
            {3.5, 8.5},
            {8.5, 3.5}});
  }

  return image;
}

QImage renderSvgToImage(
  QSvgRenderer& svgSource,
  const QString& imagePathString,
  const bool invert,
  const qreal devicePixelRatio)
{
  currentSvgRenderPathValue = imagePathString.toStdString();
  if (isCircleToolSvg(imagePathString))
  {
    currentSvgRenderPathValue.clear();
    return renderCircleToolImage(imagePathString, devicePixelRatio);
  }

  if (!svgSource.isValid())
  {
    currentSvgRenderPathValue.clear();
    return QImage{};
  }

  const auto defaultSize = svgSource.defaultSize();
  if (
    !defaultSize.isValid() || defaultSize.isEmpty() || defaultSize.width() > 1024
    || defaultSize.height() > 1024 || devicePixelRatio <= 0.0)
  {
    qWarning() << "Refusing to render SVG with invalid size" << defaultSize;
    currentSvgRenderPathValue.clear();
    return QImage{};
  }

  auto image = QImage{
    int(svgSource.defaultSize().width() * devicePixelRatio),
    int(svgSource.defaultSize().height() * devicePixelRatio),
    QImage::Format_ARGB32_Premultiplied};
  if (image.isNull())
  {
    qWarning() << "Failed to allocate SVG image" << defaultSize << devicePixelRatio;
    currentSvgRenderPathValue.clear();
    return QImage{};
  }

  image.fill(Qt::transparent);

  auto paint = QPainter{&image};
  svgSource.render(&paint);
  currentSvgRenderPathValue.clear();
  image.setDevicePixelRatio(devicePixelRatio);

  if (invert && image.isGrayscale())
  {
    image.invertPixels();
  }

  return image;
}

void renderSvgToIcon(
  QSvgRenderer& svgSource,
  const QString& imagePathString,
  QIcon& icon,
  const QIcon::State state,
  const bool invert,
  const qreal devicePixelRatio)
{
  if (!svgSource.isValid() && !isCircleToolSvg(imagePathString))
  {
    return;
  }

  auto image = renderSvgToImage(svgSource, imagePathString, invert, devicePixelRatio);
  if (image.isNull())
  {
    return;
  }

  icon.addPixmap(QPixmap::fromImage(image), QIcon::Normal, state);
  icon.addPixmap(QPixmap::fromImage(createDisabledState(image)), QIcon::Disabled, state);
}

} // namespace

std::string currentSvgRenderPath()
{
  return currentSvgRenderPathValue;
}

QPixmap loadPixmap(const std::filesystem::path& imagePath)
{
  return QPixmap{imagePathToString(imagePath)};
}

QPixmap loadSVGPixmap(const std::filesystem::path& imagePath)
{
  contract_pre(isMainThread());

  static auto cache = std::map<std::filesystem::path, QPixmap>{};
  if (const auto it = cache.find(imagePath); it != cache.end())
  {
    return it->second;
  }

  const auto palette = QPalette{};
  const auto windowColor = palette.color(QPalette::Active, QPalette::Window);
  const auto darkTheme = windowColor.lightness() <= 127;

  // Cache miss, load the image
  if (!imagePath.empty())
  {
    const auto imagePathString = imagePathToString(imagePath);
    auto renderer = QSvgRenderer{imagePathString};
    if (!renderer.isValid())
    {
      qWarning() << "Failed to load SVG " << imagePathString;
    }

    auto pixmap =
      QPixmap::fromImage(renderSvgToImage(renderer, imagePathString, darkTheme, 1.0));
    cache[imagePath] = pixmap;
    return pixmap;
  }

  cache[imagePath] = QPixmap{};
  return QPixmap{};
}

QIcon loadSVGIcon(const std::filesystem::path& imagePath)
{
  // Simple caching layer.
  // Without it, the .svg files would be read from disk and decoded each time this is
  // called, which is slow. We never evict from the cache which is assumed to be OK
  // because this is just used for icons and there's a relatively small set of them.

  contract_pre(isMainThread());

  static auto cache = std::map<std::filesystem::path, QIcon>{};
  if (const auto it = cache.find(imagePath); it != cache.end())
  {
    return it->second;
  }

  const auto palette = QPalette{};
  const auto windowColor = palette.color(QPalette::Active, QPalette::Window);
  const auto darkTheme = windowColor.lightness() <= 127;

  // Cache miss, load the icon
  auto result = QIcon{};
  if (!imagePath.empty())
  {
    const auto onPath =
      imagePathToString(imagePath.parent_path() / imagePath.stem() += "_on.svg");
    const auto offPath =
      imagePathToString(imagePath.parent_path() / imagePath.stem() += "_off.svg");
    const auto imagePathString = imagePathToString(imagePath);

    if (!onPath.isEmpty() && !offPath.isEmpty())
    {
      auto onRenderer = QSvgRenderer{onPath};
      if (!onRenderer.isValid())
      {
        qWarning() << "Failed to load SVG " << onPath;
      }

      auto offRenderer = QSvgRenderer{offPath};
      if (!offRenderer.isValid())
      {
        qWarning() << "Failed to load SVG " << offPath;
      }

      renderSvgToIcon(onRenderer, onPath, result, QIcon::On, darkTheme, 1.0);
      renderSvgToIcon(onRenderer, onPath, result, QIcon::On, darkTheme, 2.0);
      renderSvgToIcon(offRenderer, offPath, result, QIcon::Off, darkTheme, 1.0);
      renderSvgToIcon(offRenderer, offPath, result, QIcon::Off, darkTheme, 2.0);
    }
    else if (!imagePathString.isEmpty())
    {
      auto renderer = QSvgRenderer{imagePathString};
      if (!renderer.isValid())
      {
        qWarning() << "Failed to load SVG " << imagePathString;
      }

      renderSvgToIcon(renderer, imagePathString, result, QIcon::Off, darkTheme, 1.0);
      renderSvgToIcon(renderer, imagePathString, result, QIcon::Off, darkTheme, 2.0);
    }
    else
    {
      qWarning() << "Couldn't find image for path: " << pathAsQString(imagePath);
    }
  }

  cache[imagePath] = result;

  return result;
}

} // namespace tb::ui
