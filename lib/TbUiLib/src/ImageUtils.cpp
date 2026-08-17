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
#include <QIconEngine>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QSvgRenderer>
#include <QThread>

#include "ui/QPathUtils.h"
#include "ui/QThreadUtils.h"
#include "ui/SystemPaths.h"

#include "kd/contracts.h"

#include <map>
#include <tuple>

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

QImage renderSvgToImage(
  QSvgRenderer& svgSource,
  const QString& imagePathString,
  const bool invert,
  const qreal devicePixelRatio,
  const QSize& requestedSize = {})
{
  currentSvgRenderPathValue = imagePathString.toStdString();
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

  const auto logicalSize = requestedSize.isValid() && !requestedSize.isEmpty()
                             ? defaultSize.scaled(requestedSize, Qt::KeepAspectRatio)
                             : defaultSize;
  auto image = QImage{
    int(logicalSize.width() * devicePixelRatio),
    int(logicalSize.height() * devicePixelRatio),
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

bool usesDarkIconColors()
{
  return QApplication::palette().color(QPalette::Active, QPalette::Window).lightness()
         <= 127;
}

QString svgIconPath(const std::filesystem::path& imagePath, const QIcon::State state)
{
  const auto onPath =
    imagePathToString(imagePath.parent_path() / imagePath.stem() += "_on.svg");
  const auto offPath =
    imagePathToString(imagePath.parent_path() / imagePath.stem() += "_off.svg");
  if (!onPath.isEmpty() && !offPath.isEmpty())
  {
    return state == QIcon::On ? onPath : offPath;
  }

  return imagePathToString(imagePath);
}

class PaletteAwareSvgIconEngine : public QIconEngine
{
private:
  using CacheKey = std::tuple<int, int, int, int, bool, int>;

  std::filesystem::path m_imagePath;
  mutable std::map<CacheKey, QPixmap> m_cache;

public:
  explicit PaletteAwareSvgIconEngine(std::filesystem::path imagePath)
    : m_imagePath{std::move(imagePath)}
  {
  }

  QIconEngine* clone() const override
  {
    return new PaletteAwareSvgIconEngine{m_imagePath};
  }

  QString key() const override
  {
    return QStringLiteral("PaletteAwareSvgIcon");
  }

  bool isNull() override
  {
    return m_imagePath.empty() || svgIconPath(m_imagePath, QIcon::Off).isEmpty();
  }

  QSize actualSize(
    const QSize& size, const QIcon::Mode, const QIcon::State state) override
  {
    auto renderer = QSvgRenderer{svgIconPath(m_imagePath, state)};
    return renderer.isValid() ? renderer.defaultSize().scaled(size, Qt::KeepAspectRatio)
                              : QSize{};
  }

  QList<QSize> availableSizes(
    const QIcon::Mode = QIcon::Normal, const QIcon::State state = QIcon::Off) override
  {
    auto renderer = QSvgRenderer{svgIconPath(m_imagePath, state)};
    return renderer.isValid() ? QList<QSize>{renderer.defaultSize()} : QList<QSize>{};
  }

  void paint(
    QPainter* painter,
    const QRect& rect,
    const QIcon::Mode mode,
    const QIcon::State state) override
  {
    const auto scale = painter->device()->devicePixelRatioF();
    painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, scale));
  }

  QPixmap pixmap(
    const QSize& size, const QIcon::Mode mode, const QIcon::State state) override
  {
    return scaledPixmap(size, mode, state, 1.0);
  }

  QPixmap scaledPixmap(
    const QSize& size,
    const QIcon::Mode mode,
    const QIcon::State state,
    const qreal scale) override
  {
    const auto dark = usesDarkIconColors();
    const auto cacheKey = CacheKey{
      size.width(),
      size.height(),
      int(mode),
      int(state),
      dark,
      qRound(scale * 1000.0)};
    if (const auto it = m_cache.find(cacheKey); it != m_cache.end())
    {
      return it->second;
    }

    const auto sourcePath = svgIconPath(m_imagePath, state);
    auto renderer = QSvgRenderer{sourcePath};
    if (!renderer.isValid())
    {
      qWarning() << "Failed to load SVG" << sourcePath;
      return {};
    }

    auto image = renderSvgToImage(renderer, sourcePath, dark, scale, size);
    if (mode == QIcon::Disabled)
    {
      image = createDisabledState(image);
    }

    auto pixmap = QPixmap::fromImage(image);
    m_cache.emplace(cacheKey, pixmap);
    return pixmap;
  }
};

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

  using CacheKey = std::pair<std::filesystem::path, bool>;
  static auto cache = std::map<CacheKey, QPixmap>{};
  const auto darkTheme = usesDarkIconColors();
  const auto cacheKey = CacheKey{imagePath, darkTheme};
  if (const auto it = cache.find(cacheKey); it != cache.end())
  {
    return it->second;
  }

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
    cache[cacheKey] = pixmap;
    return pixmap;
  }

  cache[cacheKey] = QPixmap{};
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

  auto result = imagePath.empty()
                  ? QIcon{}
                  : QIcon{new PaletteAwareSvgIconEngine{imagePath}};

  cache[imagePath] = result;

  return result;
}

} // namespace tb::ui
