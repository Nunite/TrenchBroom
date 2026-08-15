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

#include <QApplication>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QWidget>

#include "ui/UiSnapshot.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("UiSnapshot")
{
  SECTION("saveUiSnapshot writes a non-blank PNG and manifest")
  {
    auto tempDir = QTemporaryDir{};
    REQUIRE(tempDir.isValid());

    auto widget = QWidget{};
    widget.setFixedSize(120, 80);
    widget.setStyleSheet("background: #112233;");
    auto* child = new QWidget{&widget};
    child->setGeometry(20, 20, 80, 40);
    child->setStyleSheet("background: #ddeeff;");
    widget.show();
    QApplication::processEvents();

    const auto outputPath = tempDir.filePath("snapshot.png");
    auto error = QString{};
    REQUIRE(saveUiSnapshot(
      widget, UiSnapshotOptions{outputPath, "test-widget", "dark", "1"}, &error));
    CHECK(error.isEmpty());

    const auto image = QImage{outputPath};
    REQUIRE_FALSE(image.isNull());
    CHECK(image.width() > 0);
    CHECK(image.height() > 0);

    auto metadataFile = QFile{tempDir.filePath("snapshot.json")};
    REQUIRE(metadataFile.open(QIODevice::ReadOnly));
    const auto metadata = QJsonDocument::fromJson(metadataFile.readAll()).object();
    CHECK(metadata.value("status").toString() == "ok");
    CHECK(metadata.value("target").toString() == "test-widget");
    CHECK(metadata.value("theme").toString() == "dark");
    CHECK(metadata.value("scaleFactor").toString() == "1");
    CHECK_FALSE(metadata.value("fontFamily").toString().isEmpty());
    CHECK(metadata.value("fontSupportsBasicLatin").toBool());
    CHECK(metadata.value("sampledColorCount").toInt() >= 2);
    CHECK(metadata.value("luminanceRange").toInt() >= 8);
    CHECK(metadata.value("sha256").toString().size() == 64);
  }

  SECTION("saveUiSnapshot rejects non-PNG output paths")
  {
    auto widget = QWidget{};
    auto error = QString{};

    CHECK_FALSE(saveUiSnapshot(
      widget, UiSnapshotOptions{"snapshot.jpg", "test-widget", "dark", "1"}, &error));
    CHECK(error == "UI snapshot output path must use the .png extension");
  }
}

} // namespace tb::ui
