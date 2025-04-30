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

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace tb
{
class Color;
}

namespace tb::ColorRange
{
using Type = int;
}

namespace tb::mdl
{

class EntityNodeBase;

ColorRange::Type detectColorRange(
  const std::string& entityNode, const std::vector<EntityNodeBase*>& nodes);

std::string convertEntityColor(std::string_view str, ColorRange::Type colorRange);
Color parseEntityColor(std::string_view str);
std::string entityColorAsString(const Color& color, ColorRange::Type colorRange);

} // namespace tb::mdl
