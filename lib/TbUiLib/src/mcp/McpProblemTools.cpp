/*
 Copyright (C) 2026

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

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include "McpBridgeServerTools.h"
#include "mcp/McpError.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityNode.h"
#include "mdl/GroupNode.h"
#include "mdl/Issue.h"
#include "mdl/IssueQuickFix.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Node.h"
#include "mdl/PatchNode.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"

#include "kd/overload.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <set>
#include <vector>

namespace tb::ui
{
namespace mcp = tb::mcp;

namespace
{

QString nodePathId(const mdl::Node& node, const mdl::WorldNode& worldNode)
{
  if (&node == &worldNode)
  {
    return "node:world";
  }

  auto parts = QStringList{};
  for (const auto index : node.pathFrom(worldNode).indices)
  {
    parts.push_back(QString::number(index));
  }
  return QString{"node:%1"}.arg(parts.join('/'));
}

QString nodeTypeName(const mdl::Node& node)
{
  if (dynamic_cast<const mdl::WorldNode*>(&node) != nullptr)
  {
    return "world";
  }
  if (dynamic_cast<const mdl::LayerNode*>(&node) != nullptr)
  {
    return "layer";
  }
  if (dynamic_cast<const mdl::GroupNode*>(&node) != nullptr)
  {
    return "group";
  }
  if (dynamic_cast<const mdl::EntityNode*>(&node) != nullptr)
  {
    return "entity";
  }
  if (dynamic_cast<const mdl::BrushNode*>(&node) != nullptr)
  {
    return "brush";
  }
  if (dynamic_cast<const mdl::PatchNode*>(&node) != nullptr)
  {
    return "patch";
  }
  return "node";
}

QString makeOperationId(int& nextOperationIndex)
{
  return QString{"mcp-op-%1"}.arg(nextOperationIndex++);
}

QJsonObject mutationResultJson(const McpOperationRecord& operation)
{
  auto result = QJsonObject{};
  result.insert("operationId", operation.operationId);
  result.insert("transactionName", operation.transactionName);
  result.insert("changedObjectIds", operation.changedObjectIdsJson());
  result.insert("changedObjectCount", operation.changedObjectIds.size());
  return result;
}

void mcpRecordOperation(
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex,
  const QString& toolName,
  const QString& transactionName,
  const QJsonArray& changedObjectIds,
  QJsonObject& result)
{
  auto operation = McpOperationRecord{};
  operation.operationId = makeOperationId(nextOperationIndex);
  operation.toolName = toolName;
  operation.transactionName = transactionName;
  operation.setChangedObjectIds(changedObjectIds);
  result = mutationResultJson(operation);
  history.push_back(std::move(operation));
}

bool mcpOptionalBool(
  const QJsonObject& params, const QString& key, const bool defaultValue)
{
  const auto value = params.value(key);
  return value.isBool() ? value.toBool() : defaultValue;
}

size_t optionalSize(
  const QJsonObject& params, const QString& key, const size_t defaultValue)
{
  const auto value = params.value(key);
  if (!value.isDouble())
  {
    return defaultValue;
  }
  return static_cast<size_t>(std::max(0, value.toInt()));
}

std::optional<std::vector<QString>> stringListFromJson(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto value = params.value(key);
  if (!value.isArray())
  {
    error = QString{"%1 must be an array"}.arg(key);
    return std::nullopt;
  }

  auto result = std::vector<QString>{};
  for (const auto& item : value.toArray())
  {
    if (!item.isString())
    {
      error = QString{"%1 must contain only strings"}.arg(key);
      return std::nullopt;
    }
    result.push_back(item.toString());
  }
  return result;
}

std::optional<std::vector<QString>> requiredStringListFromJson(
  const QJsonObject& params, const QString& key, QString& error)
{
  const auto values = stringListFromJson(params, key, error);
  if (!values)
  {
    return std::nullopt;
  }
  if (values->empty())
  {
    error = QString{"%1 must not be empty"}.arg(key);
    return std::nullopt;
  }
  return values;
}

bool executeTransaction(
  mdl::Map& map, const QString& transactionName, const std::function<bool()>& operation)
{
  auto transaction = mdl::Transaction{map, transactionName.toStdString()};
  if (!operation())
  {
    transaction.cancel();
    return false;
  }
  return transaction.commit();
}

QString problemId(const mdl::Issue& issue, const mdl::WorldNode& worldNode)
{
  auto id = QString{"issue:%1:%2:%3"}
              .arg(QString::number(issue.type()))
              .arg(nodePathId(issue.node(), worldNode))
              .arg(QString::number(issue.lineNumber()));
  if (const auto* faceIssue = dynamic_cast<const mdl::BrushFaceIssue*>(&issue))
  {
    id += QString{":face:%1"}.arg(QString::number(faceIssue->faceIndex()));
  }
  if (const auto* propertyIssue = dynamic_cast<const mdl::EntityPropertyIssue*>(&issue))
  {
    id +=
      QString{":property:%1"}.arg(QString::fromStdString(propertyIssue->propertyKey()));
  }
  return id;
}

bool isSafeQuickFixDescription(const QString& description)
{
  static const auto SafeDescriptions = std::set<QString>{
    "Delete Property",
    "Remove Mod",
    "Replace \" with '",
    "Reset UV Scale",
    "Snap Vertices",
    "Truncate Property Values",
  };
  return SafeDescriptions.contains(description);
}

QJsonArray safeQuickFixDescriptions(
  const mdl::WorldNode& worldNode, const mdl::Issue& issue)
{
  auto result = QJsonArray{};
  for (const auto* quickFix : worldNode.quickFixes(issue.type()))
  {
    const auto description = QString::fromStdString(quickFix->description());
    if (isSafeQuickFixDescription(description))
    {
      result.push_back(description);
    }
  }
  return result;
}

QJsonObject issueJson(const mdl::Issue& issue, const mdl::WorldNode& worldNode)
{
  auto result = QJsonObject{
    {"id", problemId(issue, worldNode)},
    {"type", issue.type()},
    {"severity", "warning"},
    {"message", QString::fromStdString(issue.description())},
    {"objectId", nodePathId(issue.node(), worldNode)},
    {"objectType", nodeTypeName(issue.node())},
    {"lineNumber", static_cast<int>(issue.lineNumber())},
    {"hidden", issue.hidden()},
    {"safeQuickFixes", safeQuickFixDescriptions(worldNode, issue)},
  };

  if (const auto* faceIssue = dynamic_cast<const mdl::BrushFaceIssue*>(&issue))
  {
    result.insert("faceIndex", static_cast<int>(faceIssue->faceIndex()));
  }
  if (const auto* propertyIssue = dynamic_cast<const mdl::EntityPropertyIssue*>(&issue))
  {
    result.insert("propertyKey", QString::fromStdString(propertyIssue->propertyKey()));
  }
  return result;
}

QJsonObject issueBoundsJson(const mdl::Issue& issue)
{
  const auto& bounds = issue.node().logicalBounds();
  return QJsonObject{
    {"min", QJsonArray{bounds.min.x(), bounds.min.y(), bounds.min.z()}},
    {"max", QJsonArray{bounds.max.x(), bounds.max.y(), bounds.max.z()}},
  };
}

std::vector<const mdl::Issue*> collectMapIssues(
  mdl::Map& map, const bool includeHidden, const size_t limit = 0)
{
  const auto validators = map.worldNode().registeredValidators();
  auto issues = std::vector<const mdl::Issue*>{};
  const auto collectIssues = [&](auto& node) {
    for (const auto* issue : node.issues(validators))
    {
      if ((includeHidden || !issue->hidden()) && (limit == 0 || issues.size() < limit))
      {
        issues.push_back(issue);
      }
    }
  };

  map.worldNode().accept(kdl::overload(
    [&](auto&& thisLambda, mdl::WorldNode& worldNode) {
      collectIssues(worldNode);
      worldNode.visitChildren(thisLambda);
    },
    [&](auto&& thisLambda, mdl::LayerNode& layerNode) {
      collectIssues(layerNode);
      layerNode.visitChildren(thisLambda);
    },
    [&](auto&& thisLambda, mdl::GroupNode& groupNode) {
      collectIssues(groupNode);
      groupNode.visitChildren(thisLambda);
    },
    [&](auto&& thisLambda, mdl::EntityNode& entityNode) {
      collectIssues(entityNode);
      entityNode.visitChildren(thisLambda);
    },
    [&](mdl::BrushNode& brushNode) { collectIssues(brushNode); },
    [&](mdl::PatchNode& patchNode) { collectIssues(patchNode); }));

  return issues;
}

QJsonObject problemsJson(mdl::Map& map, const QJsonObject& params)
{
  const auto includeHidden = mcpOptionalBool(params, "includeHidden", false);
  const auto limit = optionalSize(params, "limit", 500);
  const auto issues = collectMapIssues(map, includeHidden, limit);

  auto results = QJsonArray{};
  auto safeFixableCount = 0;
  for (const auto* issue : issues)
  {
    const auto json = issueJson(*issue, map.worldNode());
    if (!json.value("safeQuickFixes").toArray().empty())
    {
      ++safeFixableCount;
    }
    results.push_back(json);
  }

  return QJsonObject{
    {"valid", issues.empty()},
    {"count", results.size()},
    {"safeFixableCount", safeFixableCount},
    {"problems", results},
  };
}

QJsonArray groupedIssuesJson(mdl::Map& map, const bool includeHidden)
{
  struct Group
  {
    int count = 0;
    QString message;
    QJsonArray sampleObjectIds;
    QJsonArray sampleBounds;
  };

  auto groups = std::map<QString, Group>{};
  for (const auto* issue : collectMapIssues(map, includeHidden))
  {
    const auto message = QString::fromStdString(issue->description());
    const auto key = QString{"%1|%2"}.arg(QString::number(issue->type()), message);
    auto& group = groups[key];
    ++group.count;
    group.message = message;
    if (group.sampleObjectIds.size() < 5)
    {
      group.sampleObjectIds.push_back(nodePathId(issue->node(), map.worldNode()));
      group.sampleBounds.push_back(issueBoundsJson(*issue));
    }
  }

  auto result = QJsonArray{};
  for (const auto& [key, group] : groups)
  {
    Q_UNUSED(key);
    result.push_back(QJsonObject{
      {"message", group.message},
      {"count", group.count},
      {"sampleObjectIds", group.sampleObjectIds},
      {"sampleBounds", group.sampleBounds},
    });
  }
  return result;
}

std::vector<const mdl::Issue*> findIssuesByIds(
  mdl::Map& map,
  const std::vector<QString>& problemIds,
  const bool includeHidden,
  QString& error)
{
  const auto wantedIds = std::set<QString>{std::begin(problemIds), std::end(problemIds)};
  auto foundIds = std::set<QString>{};
  auto result = std::vector<const mdl::Issue*>{};
  for (const auto* issue : collectMapIssues(map, includeHidden))
  {
    const auto id = problemId(*issue, map.worldNode());
    if (wantedIds.contains(id))
    {
      foundIds.insert(id);
      result.push_back(issue);
    }
  }

  if (foundIds.size() != wantedIds.size())
  {
    for (const auto& id : wantedIds)
    {
      if (!foundIds.contains(id))
      {
        error = QString{"Unknown or stale problem id: %1"}.arg(id);
        break;
      }
    }
    return {};
  }
  return result;
}

const mdl::IssueQuickFix* findSafeQuickFix(
  const mdl::WorldNode& worldNode,
  const std::vector<const mdl::Issue*>& issues,
  const QString& description)
{
  if (!isSafeQuickFixDescription(description))
  {
    return nullptr;
  }

  auto issueTypes = ~static_cast<mdl::IssueType>(0);
  for (const auto* issue : issues)
  {
    issueTypes &= issue->type();
  }

  for (const auto* quickFix : worldNode.quickFixes(issueTypes))
  {
    if (QString::fromStdString(quickFix->description()) == description)
    {
      return quickFix;
    }
  }
  return nullptr;
}

} // namespace

McpBridgeToolResult problemsCheckResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  return McpBridgeToolResult::success(problemsJson(mapWindow->document().map(), params));
}

McpBridgeToolResult mapValidateResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto result = problemsJson(mapWindow->document().map(), params);
  if (mcpOptionalBool(params, "groupByType", false))
  {
    result.insert(
      "groups",
      groupedIssuesJson(
        mapWindow->document().map(), mcpOptionalBool(params, "includeHidden", false)));
    result.insert("detail", "groupedSummary");
  }
  if (!mcpOptionalBool(params, "includeProblems", false))
  {
    result.remove("problems");
  }
  else
  {
    result.insert("detail", "summaryWithProblems");
    result.insert("limit", static_cast<int>(optionalSize(params, "limit", 500)));
  }
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult problemsFixResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto& map = mapWindow->document().map();
  auto error = QString{};
  const auto problemIds = requiredStringListFromJson(params, "problemIds", error);
  if (!problemIds)
  {
    return invalidParamsFailure(error);
  }

  const auto quickFixDescription = params.value("quickFix").toString().trimmed();
  if (quickFixDescription.isEmpty())
  {
    return invalidParamsFailure("problems_fix requires quickFix");
  }

  const auto issues = findIssuesByIds(
    map, *problemIds, mcpOptionalBool(params, "includeHidden", false), error);
  if (!error.isEmpty())
  {
    return invalidParamsFailure(error);
  }

  const auto* quickFix = findSafeQuickFix(map.worldNode(), issues, quickFixDescription);
  if (!quickFix)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::Forbidden,
      QString{"Quick fix is not safe or not applicable: %1"}.arg(quickFixDescription));
  }

  auto changedObjectIds = QJsonArray{};
  for (const auto* issue : issues)
  {
    changedObjectIds.push_back(nodePathId(issue->node(), map.worldNode()));
  }

  const auto transactionName = QString{"MCP: Fix problems (%1)"}.arg(quickFixDescription);
  const auto ok = executeTransaction(map, transactionName, [&]() {
    quickFix->apply(map, issues);
    return true;
  });
  if (!ok)
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not apply problem quick fix");
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history, nextOperationIndex, toolName, transactionName, changedObjectIds, result);
  result.insert("fixedCount", static_cast<int>(issues.size()));
  result.insert("quickFix", quickFixDescription);
  return McpBridgeToolResult::success(std::move(result));
}

McpBridgeToolResult mapFixAllSafeResult(
  AppController& appController,
  const QString& toolName,
  const QJsonObject& params,
  std::vector<McpOperationRecord>& history,
  int& nextOperationIndex)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return noActiveDocumentFailure();
  }

  auto& map = mapWindow->document().map();
  const auto includeHidden = mcpOptionalBool(params, "includeHidden", false);
  auto changedObjectIds = QJsonArray{};
  auto fixedCount = 0;
  auto appliedFixes = QJsonArray{};
  const auto transactionName = QString{"MCP: Fix all safe problems"};
  const auto ok = executeTransaction(map, transactionName, [&]() {
    auto appliedAny = false;
    for (auto pass = 0; pass < 8; ++pass)
    {
      auto issues = collectMapIssues(map, includeHidden);
      auto didApply = false;
      for (const auto* issue : issues)
      {
        const auto safeFixes = safeQuickFixDescriptions(map.worldNode(), *issue);
        if (safeFixes.empty())
        {
          continue;
        }

        const auto quickFixDescription = safeFixes.first().toString();
        const auto* quickFix =
          findSafeQuickFix(map.worldNode(), {issue}, quickFixDescription);
        if (!quickFix)
        {
          continue;
        }

        changedObjectIds.push_back(nodePathId(issue->node(), map.worldNode()));
        quickFix->apply(map, {issue});
        appliedFixes.push_back(quickFixDescription);
        ++fixedCount;
        didApply = true;
        appliedAny = true;
        break;
      }
      if (!didApply)
      {
        break;
      }
    }
    return appliedAny;
  });
  if (!ok)
  {
    return McpBridgeToolResult::success(QJsonObject{
      {"fixedCount", 0},
      {"appliedFixes", QJsonArray{}},
      {"message", "No safe problem fixes were available"},
    });
  }

  auto result = QJsonObject{};
  mcpRecordOperation(
    history, nextOperationIndex, toolName, transactionName, changedObjectIds, result);
  result.insert("fixedCount", fixedCount);
  result.insert("appliedFixes", appliedFixes);
  return McpBridgeToolResult::success(std::move(result));
}

} // namespace tb::ui
