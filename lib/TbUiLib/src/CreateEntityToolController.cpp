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

#include "ui/CreateEntityToolController.h"

#include "ui/CreateEntityTool.h"
#include "ui/DropTracker.h"
#include "ui/InputState.h"

#include "kd/contracts.h"

#include <functional>
#include <optional>
#include <string>

namespace tb::ui
{
namespace
{

struct DropPayload
{
  std::string type;
  std::string value;
};

std::optional<DropPayload> parseDropPayload(const std::string& payload)
{
  const auto separatorPos = payload.find(':');
  if (separatorPos == std::string::npos)
  {
    return std::nullopt;
  }
  return DropPayload{payload.substr(0, separatorPos), payload.substr(separatorPos + 1)};
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
  const auto parsedPayload = parseDropPayload(payload);
  if (!parsedPayload)
  {
    return false;
  }

  if (parsedPayload->type == "entity")
  {
    return true;
  }

  if (parsedPayload->type == "model")
  {
    return m_tool.canCreateModelEntity(parsedPayload->value);
  }

  return false;
}

std::unique_ptr<DropTracker> CreateEntityToolController::acceptDrop(
  const InputState& inputState, const std::string& payload)
{
  const auto parsedPayload = parseDropPayload(payload);
  contract_assert(parsedPayload.has_value());

  if (parsedPayload->type == "entity")
  {
    return m_tool.createEntity(parsedPayload->value) ? createDropTracker(inputState)
                                                     : nullptr;
  }

  if (parsedPayload->type == "model")
  {
    return m_tool.createModelEntity(parsedPayload->value) ? createDropTracker(inputState)
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
