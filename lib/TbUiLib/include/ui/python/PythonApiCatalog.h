#pragma once

#include <span>
#include <string_view>

namespace tb::ui
{

enum class PythonApiType
{
  Module,
  Vec3,
  Plane,
  Document,
  Selection,
  Entity,
  Brush,
  Face,
  Material,
  MaterialCollection,
  Transaction,
  PluginPanel,
};

enum class PythonApiSymbolKind
{
  Class,
  Function,
  Property,
  Method,
};

struct PythonApiSymbol
{
  std::string_view name;
  PythonApiSymbolKind kind;
  std::string_view detail;
};

struct PythonApiTypeInfo
{
  PythonApiType type;
  std::string_view name;
};

std::span<const PythonApiTypeInfo> pythonApiTypes();
std::span<const PythonApiSymbol> pythonApiSymbols(PythonApiType type);
std::span<const std::string_view> pythonConsoleHelperNames();

} // namespace tb::ui
