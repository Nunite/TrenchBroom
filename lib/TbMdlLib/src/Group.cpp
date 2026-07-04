/*
 Copyright (C) 2020 Kristian Duske

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

#include "mdl/Group.h"

#include "mdl/EntityProperties.h"

#include "kd/reflection_impl.h"

#include "vm/mat_io.h" // IWYU pragma: keep

namespace tb::mdl
{

kdl_reflect_impl(Group);

Group::Group(std::string name)
  : m_name{std::move(name)}
{
}

const std::string& Group::name() const
{
  return m_name;
}

void Group::setName(std::string name)
{
  m_name = std::move(name);
}

const vm::mat4x4d& Group::transformation() const
{
  return m_transformation;
}

void Group::setTransformation(const vm::mat4x4d& transformation)
{
  m_transformation = transformation;
}

void Group::transform(const vm::mat4x4d& transformation)
{
  m_transformation = transformation * m_transformation;
}

const std::vector<EntityProperty>& Group::properties() const
{
  return m_properties;
}

const std::string& Group::property(const std::string& key) const
{
  return findEntityPropertyOrDefault(m_properties, key);
}

void Group::setProperty(std::string key, std::string value)
{
  if (auto it = findEntityProperty(m_properties, key); it != std::end(m_properties))
  {
    it->setValue(std::move(value));
  }
  else
  {
    m_properties.emplace_back(std::move(key), std::move(value));
  }
}

void Group::removeProperty(const std::string& key)
{
  if (auto it = findEntityProperty(m_properties, key); it != std::end(m_properties))
  {
    m_properties.erase(it);
  }
}

} // namespace tb::mdl
