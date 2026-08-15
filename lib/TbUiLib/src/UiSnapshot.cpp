/*
 Copyright (C) 2026 Kristian Duske

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

#include "ui/UiSnapshot.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontInfo>
#include <QFontMetrics>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QSaveFile>
#include <QSet>
#include <QWidget>
#include <QtGlobal>

#include <algorithm>
#include <utility>

namespace tb::ui
{
namespace
{

struct ImageStats
{
  int sampleStride = 1;
  int opaqueSamples = 0;
  int uniqueColors = 0;
  int luminanceRange = 0;
};

bool fail(QString* error, QString message)
{
  if (error != nullptr)
  {
    *error = std::move(message);
  }
  return false;
}

ImageStats analyzeImage(const QImage& source)
{
  const auto image = source.convertToFormat(QImage::Format_ARGB32);
  const auto stride = std::max(1, std::min(image.width(), image.height()) / 200);
  auto colors = QSet<QRgb>{};
  auto opaqueSamples = 0;
  auto minimumLuminance = 255;
  auto maximumLuminance = 0;

  for (auto y = 0; y < image.height(); y += stride)
  {
    for (auto x = 0; x < image.width(); x += stride)
    {
      const auto pixel = image.pixel(x, y);
      if (qAlpha(pixel) == 0)
      {
        continue;
      }

      ++opaqueSamples;
      colors.insert(pixel);
      const auto luminance = qGray(pixel);
      minimumLuminance = std::min(minimumLuminance, luminance);
      maximumLuminance = std::max(maximumLuminance, luminance);
    }
  }

  return ImageStats{
    stride,
    opaqueSamples,
    int(colors.size()),
    opaqueSamples > 0 ? maximumLuminance - minimumLuminance : 0};
}

QString manifestPath(const QString& outputPath)
{
  const auto fileInfo = QFileInfo{outputPath};
  return fileInfo.dir().filePath(fileInfo.completeBaseName() + ".json");
}

bool fontSupportsBasicLatin(const QWidget& widget)
{
  const auto metrics = QFontMetrics{widget.font()};
  for (const auto character : QStringLiteral("TrenchBroom Level Editor 0123456789"))
  {
    if (!character.isSpace() && !metrics.inFont(character))
    {
      return false;
    }
  }
  return true;
}

} // namespace

bool saveUiSnapshot(QWidget& widget, const UiSnapshotOptions& options, QString* error)
{
  const auto outputInfo = QFileInfo{options.outputPath};
  if (outputInfo.suffix().compare("png", Qt::CaseInsensitive) != 0)
  {
    return fail(error, "UI snapshot output path must use the .png extension");
  }

  if (!QDir{}.mkpath(outputInfo.absolutePath()))
  {
    return fail(
      error,
      QStringLiteral("Could not create UI snapshot directory: %1")
        .arg(outputInfo.absolutePath()));
  }

  if (!fontSupportsBasicLatin(widget))
  {
    return fail(error, "UI snapshot font cannot render required acceptance text");
  }

  const auto pixmap = widget.grab();
  if (pixmap.isNull())
  {
    return fail(error, "UI snapshot returned an empty pixmap");
  }

  const auto image = pixmap.toImage();
  const auto stats = analyzeImage(image);
  if (stats.opaqueSamples == 0 || stats.uniqueColors < 2 || stats.luminanceRange < 8)
  {
    return fail(error, "UI snapshot is transparent or visually uniform");
  }

  auto imageFile = QSaveFile{outputInfo.absoluteFilePath()};
  if (!imageFile.open(QIODevice::WriteOnly))
  {
    return fail(error, imageFile.errorString());
  }
  if (!image.save(&imageFile, "PNG"))
  {
    return fail(error, "Could not encode UI snapshot as PNG");
  }
  if (!imageFile.commit())
  {
    return fail(error, imageFile.errorString());
  }

  auto savedImage = QFile{outputInfo.absoluteFilePath()};
  if (!savedImage.open(QIODevice::ReadOnly))
  {
    return fail(error, savedImage.errorString());
  }
  const auto sha256 =
    QCryptographicHash::hash(savedImage.readAll(), QCryptographicHash::Sha256).toHex();

  const auto metadata = QJsonObject{
    {"status", "ok"},
    {"target", options.target},
    {"theme", options.theme},
    {"scaleFactor", options.scaleFactor},
    {"qtVersion", QString::fromLatin1(qVersion())},
    {"fontFamily", QFontInfo{widget.font()}.family()},
    {"fontSupportsBasicLatin", true},
    {"imagePath", outputInfo.absoluteFilePath()},
    {"logicalSize", QJsonObject{{"width", widget.width()}, {"height", widget.height()}}},
    {"pixelSize", QJsonObject{{"width", image.width()}, {"height", image.height()}}},
    {"devicePixelRatio", image.devicePixelRatio()},
    {"sampleStride", stats.sampleStride},
    {"sampledOpaquePixels", stats.opaqueSamples},
    {"sampledColorCount", stats.uniqueColors},
    {"luminanceRange", stats.luminanceRange},
    {"sha256", QString::fromLatin1(sha256)},
  };

  auto metadataFile = QSaveFile{manifestPath(outputInfo.absoluteFilePath())};
  if (!metadataFile.open(QIODevice::WriteOnly))
  {
    return fail(error, metadataFile.errorString());
  }
  if (metadataFile.write(QJsonDocument{metadata}.toJson(QJsonDocument::Indented)) < 0)
  {
    return fail(error, metadataFile.errorString());
  }
  if (!metadataFile.commit())
  {
    return fail(error, metadataFile.errorString());
  }

  if (error != nullptr)
  {
    error->clear();
  }
  return true;
}

} // namespace tb::ui
