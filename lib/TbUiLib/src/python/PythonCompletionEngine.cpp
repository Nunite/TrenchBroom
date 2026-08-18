#include "ui/python/PythonCompletionEngine.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tb::ui
{
namespace
{
enum class PostfixKind
{
  Call,
  Index,
};

struct ExpressionSegment
{
  std::string name;
  std::vector<PostfixKind> postfixes;
};

bool isIdentifierStart(const char ch)
{
  return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool isIdentifierContinue(const char ch)
{
  return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

void skipWhitespace(const std::string_view expression, size_t& position)
{
  while (position < expression.size()
         && std::isspace(static_cast<unsigned char>(expression[position])) != 0)
  {
    ++position;
  }
}

bool skipDelimited(const std::string_view expression, size_t& position)
{
  const auto closingDelimiter = [](const char opening) -> std::optional<char> {
    switch (opening)
    {
    case '(':
      return ')';
    case '[':
      return ']';
    case '{':
      return '}';
    default:
      return std::nullopt;
    }
  };

  const auto firstClosing =
    position < expression.size() ? closingDelimiter(expression[position]) : std::nullopt;
  if (!firstClosing)
  {
    return false;
  }

  auto delimiters = std::vector<char>{*firstClosing};
  auto quote = char{0};
  auto escaped = false;
  ++position;

  while (position < expression.size() && !delimiters.empty())
  {
    const auto ch = expression[position++];
    if (quote != 0)
    {
      if (escaped)
      {
        escaped = false;
      }
      else if (ch == '\\')
      {
        escaped = true;
      }
      else if (ch == quote)
      {
        quote = 0;
      }
      continue;
    }

    if (ch == '\'' || ch == '"')
    {
      quote = ch;
    }
    else if (const auto closing = closingDelimiter(ch))
    {
      delimiters.push_back(*closing);
    }
    else if (ch == delimiters.back())
    {
      delimiters.pop_back();
    }
  }

  return delimiters.empty() && quote == 0;
}

std::optional<std::vector<ExpressionSegment>> parseExpression(
  const std::string_view expression)
{
  auto result = std::vector<ExpressionSegment>{};
  auto position = size_t{0u};
  skipWhitespace(expression, position);

  while (position < expression.size())
  {
    if (!isIdentifierStart(expression[position]))
    {
      return std::nullopt;
    }

    const auto nameStart = position++;
    while (position < expression.size() && isIdentifierContinue(expression[position]))
    {
      ++position;
    }

    auto segment = ExpressionSegment{
      std::string{expression.substr(nameStart, position - nameStart)}, {}};
    skipWhitespace(expression, position);
    while (position < expression.size()
           && (expression[position] == '(' || expression[position] == '['))
    {
      const auto postfix =
        expression[position] == '(' ? PostfixKind::Call : PostfixKind::Index;
      if (!skipDelimited(expression, position))
      {
        return std::nullopt;
      }
      segment.postfixes.push_back(postfix);
      skipWhitespace(expression, position);
    }
    result.push_back(std::move(segment));

    if (position == expression.size())
    {
      break;
    }
    if (expression[position] != '.')
    {
      return std::nullopt;
    }
    ++position;
    skipWhitespace(expression, position);
    if (position == expression.size())
    {
      return std::nullopt;
    }
  }

  return result.empty()
           ? std::nullopt
           : std::make_optional<std::vector<ExpressionSegment>>(std::move(result));
}

bool isCallable(const PythonApiSymbolKind kind)
{
  return kind == PythonApiSymbolKind::Class || kind == PythonApiSymbolKind::Function
         || kind == PythonApiSymbolKind::Method;
}

std::optional<PythonApiValueType> applyPostfixes(
  std::optional<PythonApiValueType> type,
  const std::vector<PostfixKind>& postfixes,
  const bool callRequired)
{
  auto postfixIndex = size_t{0u};
  if (callRequired)
  {
    if (postfixes.empty() || postfixes.front() != PostfixKind::Call)
    {
      return std::nullopt;
    }
    ++postfixIndex;
  }

  if (!type)
  {
    return std::nullopt;
  }

  for (; postfixIndex < postfixes.size(); ++postfixIndex)
  {
    if (postfixes[postfixIndex] != PostfixKind::Index || type->sequenceDepth == 0u)
    {
      return std::nullopt;
    }
    --type->sequenceDepth;
  }
  return type;
}

const PythonApiSymbol* findSymbol(const PythonApiType owner, const std::string_view name)
{
  const auto symbols = pythonApiSymbols(owner);
  const auto symbol = std::ranges::find(symbols, name, &PythonApiSymbol::name);
  return symbol != symbols.end() ? &*symbol : nullptr;
}

bool isConsoleHelper(const std::string_view name)
{
  const auto helperNames = pythonConsoleHelperNames();
  return std::ranges::find(helperNames, name) != helperNames.end();
}

std::optional<PythonApiValueType> resolveSymbol(
  const PythonApiSymbol& symbol, const std::vector<PostfixKind>& postfixes)
{
  if (symbol.kind == PythonApiSymbolKind::Class)
  {
    auto remainingPostfixes = postfixes;
    if (!remainingPostfixes.empty() && remainingPostfixes.front() == PostfixKind::Call)
    {
      remainingPostfixes.erase(remainingPostfixes.begin());
    }
    return applyPostfixes(symbol.resultType, remainingPostfixes, false);
  }
  return applyPostfixes(symbol.resultType, postfixes, isCallable(symbol.kind));
}

std::optional<PythonApiValueType> resolveStaticRoot(const ExpressionSegment& segment)
{
  if (segment.name == "tb2")
  {
    return applyPostfixes(
      PythonApiValueType{PythonApiType::Module}, segment.postfixes, false);
  }
  if (segment.name == "doc")
  {
    return applyPostfixes(
      PythonApiValueType{PythonApiType::Document}, segment.postfixes, false);
  }
  if (segment.name == "sel")
  {
    return applyPostfixes(
      PythonApiValueType{PythonApiType::Selection}, segment.postfixes, false);
  }

  const auto* symbol = findSymbol(PythonApiType::Module, segment.name);
  return symbol != nullptr && isConsoleHelper(segment.name)
           ? resolveSymbol(*symbol, segment.postfixes)
           : std::nullopt;
}
} // namespace

std::optional<PythonApiValueType> pythonCompletionTypeForExpression(
  const std::string_view expression, const PythonCompletionRootProvider& rootProvider)
{
  const auto segments = parseExpression(expression);
  if (!segments)
  {
    return std::nullopt;
  }

  const auto& rootSegment = segments->front();
  auto type = std::optional<PythonApiValueType>{};
  if (rootProvider)
  {
    const auto root = rootProvider(rootSegment.name);
    if (root.exists)
    {
      type = applyPostfixes(root.type, rootSegment.postfixes, false);
    }
    else
    {
      type = resolveStaticRoot(rootSegment);
    }
  }
  else
  {
    type = resolveStaticRoot(rootSegment);
  }

  for (auto i = size_t{1u}; type && i < segments->size(); ++i)
  {
    if (type->sequenceDepth != 0u)
    {
      return std::nullopt;
    }

    const auto& segment = (*segments)[i];
    const auto* symbol = findSymbol(type->type, segment.name);
    if (symbol == nullptr)
    {
      return std::nullopt;
    }
    type = resolveSymbol(*symbol, segment.postfixes);
  }

  return type;
}

} // namespace tb::ui
