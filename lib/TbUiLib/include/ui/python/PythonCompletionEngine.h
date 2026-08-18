#pragma once

#include "ui/python/PythonApiCatalog.h"

#include <functional>
#include <optional>
#include <string_view>

namespace tb::ui
{

struct PythonCompletionRoot
{
  bool exists = false;
  std::optional<PythonApiValueType> type;
};

using PythonCompletionRootProvider =
  std::function<PythonCompletionRoot(std::string_view)>;

std::optional<PythonApiValueType> pythonCompletionTypeForExpression(
  std::string_view expression, const PythonCompletionRootProvider& rootProvider = {});

} // namespace tb::ui
