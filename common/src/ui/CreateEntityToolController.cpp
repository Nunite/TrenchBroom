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

#include "CreateEntityToolController.h"

#include "Ensure.h"
#include "ui/CreateEntityTool.h"
#include "ui/DropTracker.h"
#include "ui/InputState.h"

#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/Game.h"
#include "mdl/Map.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace tb::ui
{
namespace
{

std::optional<std::string> droppedModelPathToEntityModelPath(
  const mdl::Map& map, const std::string_view droppedModelPathStr)
{
  const auto* game = map.game();
  if (!game)
  {
    return std::nullopt;
  }

  if (droppedModelPathStr.empty())
  {
    return std::nullopt;
  }

  const auto gamePath = game->gamePath();

  const auto modelPath = std::filesystem::path{std::string{droppedModelPathStr}};
  const auto absModelPath = modelPath.is_absolute() ? modelPath : (gamePath / modelPath);

  auto ec = std::error_code{};
  const auto relativeModelPathFull = std::filesystem::relative(absModelPath, gamePath, ec);
  if (ec)
  {
    return std::nullopt;
  }

  auto relativePathStr = relativeModelPathFull.string();
  auto modelsPos = relativePathStr.find("models/");
  if (modelsPos == std::string::npos)
  {
    modelsPos = relativePathStr.find("models\\");
  }

  std::string finalModelPath;
  if (modelsPos != std::string::npos)
  {
    finalModelPath = relativePathStr.substr(modelsPos);
  }
  else
  {
    finalModelPath = relativeModelPathFull.generic_string();
  }

  for (auto& ch : finalModelPath)
  {
    if (ch == '\\')
    {
      ch = '/';
    }
  }

  if (finalModelPath.empty())
  {
    return std::nullopt;
  }

  return finalModelPath;
}

class CreateEntityDropTracker : public DropTracker
{
private:
  using UpdateEntityPosition = std::function<void(const InputState&, CreateEntityTool&)>;

  CreateEntityTool& m_tool;
  UpdateEntityPosition m_updateEntityPosition;

public:
  explicit CreateEntityDropTracker(
    const InputState& inputState,
    CreateEntityTool& tool,
    UpdateEntityPosition updateEntityPosition)
    : m_tool{tool}
    , m_updateEntityPosition{std::move(updateEntityPosition)}
  {
    m_updateEntityPosition(inputState, m_tool);
  }

  bool move(const InputState& inputState) override
  {
    m_updateEntityPosition(inputState, m_tool);
    return true;
  }

  bool drop(const InputState&) override
  {
    m_tool.commitEntity();
    return true;
  }

  void leave(const InputState&) override { m_tool.removeEntity(); }
};

} // namespace

CreateEntityToolController::CreateEntityToolController(CreateEntityTool& tool)
  : m_tool{tool}
{
}

CreateEntityToolController::~CreateEntityToolController() = default;

Tool& CreateEntityToolController::tool()
{
  return m_tool;
}

const Tool& CreateEntityToolController::tool() const
{
  return m_tool;
}

bool CreateEntityToolController::shouldAcceptDrop(
  const InputState&, const std::string& payload) const
{
  static constexpr auto entityPrefix = std::string_view{"entity:"};
  static constexpr auto modelPrefix = std::string_view{"model:"};
  static const auto cyclerSpriteClassname = std::string{"cycler_sprite"};

  const auto payloadView = std::string_view{payload};

  if (payloadView.starts_with(entityPrefix))
  {
    const auto classname = payloadView.substr(entityPrefix.size());
    return !classname.empty();
  }

  if (payloadView.starts_with(modelPrefix))
  {
    const auto droppedPath = payloadView.substr(modelPrefix.size());
    if (droppedPath.empty())
    {
      return false;
    }

    const auto* cyclerSpriteDef =
      m_tool.map().entityDefinitionManager().definition(cyclerSpriteClassname);
    if (!cyclerSpriteDef || !cyclerSpriteDef->pointEntityDefinition)
    {
      return false;
    }

    return droppedModelPathToEntityModelPath(m_tool.map(), droppedPath).has_value();
  }

  return false;
}

std::unique_ptr<DropTracker> CreateEntityToolController::acceptDrop(
  const InputState& inputState, const std::string& payload)
{
  static constexpr auto entityPrefix = std::string_view{"entity:"};
  static constexpr auto modelPrefix = std::string_view{"model:"};
  static const auto cyclerSpriteClassname = std::string{"cycler_sprite"};
  static const auto modelKey = std::string{"model"};

  const auto payloadView = std::string_view{payload};

  if (payloadView.starts_with(entityPrefix))
  {
    const auto classnameView = payloadView.substr(entityPrefix.size());
    ensure(!classnameView.empty(), "dropped item is an entity");
    const auto classname = std::string{classnameView};
    return m_tool.createEntity(classname) ? createDropTracker(inputState) : nullptr;
  }

  if (payloadView.starts_with(modelPrefix))
  {
    const auto droppedPath = payloadView.substr(modelPrefix.size());
    const auto finalModelPath = droppedModelPathToEntityModelPath(m_tool.map(), droppedPath);
    if (!finalModelPath)
    {
      return nullptr;
    }

    return m_tool.createEntity(cyclerSpriteClassname, modelKey, *finalModelPath)
             ? createDropTracker(inputState)
             : nullptr;
  }

  return nullptr;
}

bool CreateEntityToolController::cancel()
{
  return false;
}

CreateEntityToolController2D::CreateEntityToolController2D(CreateEntityTool& tool)
  : CreateEntityToolController{tool}
{
}

std::unique_ptr<DropTracker> CreateEntityToolController2D::createDropTracker(
  const InputState& inputState) const
{
  return std::make_unique<CreateEntityDropTracker>(
    inputState, m_tool, [](const auto& is, auto& t) {
      t.updateEntityPosition2D(is.pickRay());
    });
}

CreateEntityToolController3D::CreateEntityToolController3D(CreateEntityTool& tool)
  : CreateEntityToolController{tool}
{
}

std::unique_ptr<DropTracker> CreateEntityToolController3D::createDropTracker(
  const InputState& inputState) const
{
  return std::make_unique<CreateEntityDropTracker>(
    inputState, m_tool, [](const auto& is, auto& t) {
      t.updateEntityPosition3D(is.pickRay(), is.pickResult());
    });
}

} // namespace tb::ui
