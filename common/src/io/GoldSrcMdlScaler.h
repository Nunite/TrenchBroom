#pragma once

#include "Result.h"

#include <filesystem>

namespace tb::io
{

Result<void> scaleGoldSrcMdlFile(const std::filesystem::path& absPath, float scale);

} // namespace tb::io

