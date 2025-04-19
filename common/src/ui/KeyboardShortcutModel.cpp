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

#include "KeyboardShortcutModel.h"

#include <QBrush>

#include "io/PathQt.h"
#include "ui/ActionContext.h"
#include "ui/Actions.h"
#include "ui/MapDocument.h"
#include "PreferenceManager.h"
#include "Preferences.h"

#include "kdl/range_to_vector.h"
#include "kdl/vector_utils.h"

#include <ranges>
#include <unordered_map>

namespace tb::ui
{

// 创建路径部分翻译映射表
const std::unordered_map<std::string, QString> pathTranslations = {
    {"Menu", QObject::tr("菜单")},
    {"File", QObject::tr("文件")},
    {"Edit", QObject::tr("编辑")},
    {"View", QObject::tr("视图")},
    {"Run", QObject::tr("运行")},
    {"Debug", QObject::tr("调试")},
    {"Help", QObject::tr("帮助")},
    {"Controls", QObject::tr("控制")},
    {"Map View", QObject::tr("地图视图")},
    {"Map view", QObject::tr("地图视图")},
    {"Export", QObject::tr("导出")},
    {"Import", QObject::tr("导入")},
    {"Tags", QObject::tr("标签")},
    {"Entity Definitions", QObject::tr("实体定义")},
    {"Tools", QObject::tr("工具")},
    {"Texture", QObject::tr("纹理")},
    {"Entities", QObject::tr("实体")},
    {"Open", QObject::tr("打开")},
    {"Save", QObject::tr("保存")},
    {"Preferences", QObject::tr("首选项")},
    {"New", QObject::tr("新建")},
    {"Close", QObject::tr("关闭")},
    {"Undo", QObject::tr("撤销")},
    {"Redo", QObject::tr("重做")},
    {"Cut", QObject::tr("剪切")},
    {"Copy", QObject::tr("复制")},
    {"Paste", QObject::tr("粘贴")},
    {"Delete", QObject::tr("删除")},
    {"Duplicate", QObject::tr("复制")},
    {"Select All", QObject::tr("全选")},
    {"Select None", QObject::tr("取消选择")},
    {"Compile", QObject::tr("编译")},
    {"Launch", QObject::tr("启动")},
    {"About", QObject::tr("关于")},
    {"Manual", QObject::tr("手册")}
};

// 将路径转换为多语言显示
static QString translatePath(const std::filesystem::path& path) {
    std::string pathStr = path.generic_string();
    QString result;
    
    // 如果路径为空，返回空字符串
    if (pathStr.empty()) {
        return QString();
    }
    
    // 检查当前语言设置
    auto& prefs = PreferenceManager::instance();
    bool isEnglish = (prefs.get(Preferences::Language) == Preferences::languageEnglish());
    
    // 如果是英文，直接使用路径原始文本
    if (isEnglish) {
        return QString::fromUtf8(pathStr.c_str());
    }
    
    // 中文界面才进行翻译
    // 将路径按'/'分割
    std::vector<std::string> parts;
    size_t start = 0;
    size_t end = pathStr.find('/');
    
    while (end != std::string::npos) {
        parts.push_back(pathStr.substr(start, end - start));
        start = end + 1;
        end = pathStr.find('/', start);
    }
    
    // 添加最后一部分
    if (start < pathStr.size()) {
        parts.push_back(pathStr.substr(start));
    }
    
    // 翻译每个部分
    for (size_t i = 0; i < parts.size(); ++i) {
        auto it = pathTranslations.find(parts[i]);
        if (it != pathTranslations.end()) {
            result += it->second;
        } else {
            // 如果没有找到翻译，使用原始文本
            result += QString::fromUtf8(parts[i].c_str());
        }
        
        // 不是最后一个部分时，添加分隔符
        if (i < parts.size() - 1) {
            result += " / ";
        }
    }
    
    return result;
}

KeyboardShortcutModel::KeyboardShortcutModel(MapDocument* document, QObject* parent)
  : QAbstractTableModel{parent}
  , m_document{document}
{
  initializeActions();
  updateConflicts();
}

void KeyboardShortcutModel::reset()
{
  m_actions.clear();
  initializeActions();
  updateConflicts();
  if (totalActionCount() > 0)
  {
    emit dataChanged(createIndex(0, 0), createIndex(totalActionCount() - 1, 2));
  }
}

int KeyboardShortcutModel::rowCount(const QModelIndex& /* parent */) const
{
  return totalActionCount();
}

int KeyboardShortcutModel::columnCount(const QModelIndex& /* parent */) const
{
  // Shortcut, Context, Description
  return 3;
}

QVariant KeyboardShortcutModel::headerData(
  const int section, const Qt::Orientation orientation, const int role) const
{
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
  {
    // 检查当前语言设置
    auto& prefs = PreferenceManager::instance();
    bool isEnglish = (prefs.get(Preferences::Language) == Preferences::languageEnglish());
    
    if (isEnglish) {
      return section == 0   ? QString{"Shortcut"}
             : section == 1 ? QString{"Context"}
                            : QString{"Description"};
    } else {
      return section == 0   ? QObject::tr("快捷键")
             : section == 1 ? QObject::tr("上下文")
                            : QObject::tr("描述");
    }
  }
  return QVariant{};
}

QVariant KeyboardShortcutModel::data(const QModelIndex& index, const int role) const
{
  if (!checkIndex(index))
  {
    return QVariant{};
  }
  if (role == Qt::DisplayRole || role == Qt::EditRole)
  {
    const auto& actionInfo = this->actionInfo(index.row());
    if (index.column() == 0)
    {
      return actionInfo.action.keySequence();
    }
    if (index.column() == 1)
    {
      const std::string contextName = actionContextName(actionInfo.action.actionContext());
      // 检查当前语言设置
      auto& prefs = PreferenceManager::instance();
      bool isEnglish = (prefs.get(Preferences::Language) == Preferences::languageEnglish());
      
      if (contextName == "any") {
        return isEnglish ? QString("Any") : QObject::tr("任意");
      } else {
        return QString::fromUtf8(contextName.c_str());
      }
    }
    
    // 结合路径主要部分和Action的标签
    std::string pathStr = actionInfo.displayPath.generic_string();
    QString prefix;
    
    // 提取路径中的主要类别（如"File", "Edit"等）
    // 检查当前语言设置
    auto& prefs = PreferenceManager::instance();
    bool isEnglish = (prefs.get(Preferences::Language) == Preferences::languageEnglish());
    
    if (!pathStr.empty() && !isEnglish) {
      size_t firstSlash = pathStr.find('/');
      if (firstSlash != std::string::npos && firstSlash > 0) {
        std::string firstPart = pathStr.substr(0, firstSlash);
        auto it = pathTranslations.find(firstPart);
        if (it != pathTranslations.end()) {
          prefix = it->second + "/";
        }
      }
    } else if (!pathStr.empty() && isEnglish) {
      size_t firstSlash = pathStr.find('/');
      if (firstSlash != std::string::npos && firstSlash > 0) {
        std::string firstPart = pathStr.substr(0, firstSlash);
        prefix = QString::fromUtf8(firstPart.c_str()) + "/";
      }
    }
    
    // 返回"类别/标签"的组合
    return prefix + actionInfo.action.label();
  }
  if (role == Qt::ForegroundRole && hasConflicts(index))
  {
    return QBrush{Qt::red};
  }
  return QVariant{};
}

bool KeyboardShortcutModel::setData(
  const QModelIndex& index, const QVariant& value, const int role)
{
  if (!checkIndex(index) || role != Qt::EditRole)
  {
    return false;
  }

  // We take a copy here on purpose in order to set the key further below.
  auto& actionInfo = this->actionInfo(index.row());
  actionInfo.action.setKeySequence(value.value<QKeySequence>());

  updateConflicts();

  emit dataChanged(index, index, {Qt::DisplayRole, role});
  return true;
}

Qt::ItemFlags KeyboardShortcutModel::flags(const QModelIndex& index) const
{
  if (!checkIndex(index))
  {
    return Qt::ItemIsEnabled;
  }

  return index.column() == 0
           ? Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable
           : Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

bool KeyboardShortcutModel::hasConflicts() const
{
  return !m_conflicts.empty();
}

bool KeyboardShortcutModel::hasConflicts(const QModelIndex& index) const
{
  if (!checkIndex(index))
  {
    return false;
  }

  return kdl::wrap_set(m_conflicts).count(index.row()) > 0u;
}

void KeyboardShortcutModel::refreshAfterLanguageChange()
{
  // 通知视图所有数据都需要重新获取
  if (totalActionCount() > 0)
  {
    emit dataChanged(createIndex(0, 0), createIndex(totalActionCount() - 1, 2));
    // 通知表头变化
    emit headerDataChanged(Qt::Horizontal, 0, 2);
  }
}

void KeyboardShortcutModel::initializeActions()
{
  initializeMenuActions();
  initializeViewActions();
  if (m_document)
  {
    initializeTagActions();
    initializeEntityDefinitionActions();
  }
}

void KeyboardShortcutModel::initializeMenuActions()
{
  const auto& actionManager = ActionManager::instance();

  auto currentPath = std::filesystem::path{};
  actionManager.visitMainMenu(kdl::overload(
    [](const MenuSeparator&) {},
    [&](const MenuAction& actionItem) {
      m_actions.push_back(ActionInfo{
        currentPath / io::pathFromQString(actionItem.action.label()), actionItem.action});
    },
    [&](const auto& thisLambda, const Menu& menu) {
      currentPath = currentPath / menu.name;
      menu.visitEntries(thisLambda);
      currentPath = currentPath.parent_path();
    }));
}

void KeyboardShortcutModel::initializeViewActions()
{
  const auto& actionManager = ActionManager::instance();
  actionManager.visitMapViewActions([&](const Action& action) {
    m_actions.push_back(
      ActionInfo{"Map View" / io::pathFromQString(action.label()), action});
  });
}

void KeyboardShortcutModel::initializeTagActions()
{
  assert(m_document);
  m_document->visitTagActions([&](const Action& action) {
    m_actions.push_back(ActionInfo{"Tags" / io::pathFromQString(action.label()), action});
  });
}

void KeyboardShortcutModel::initializeEntityDefinitionActions()
{
  assert(m_document);
  m_document->visitEntityDefinitionActions([&](const Action& action) {
    m_actions.push_back(
      ActionInfo{"Entity Definitions" / io::pathFromQString(action.label()), action});
  });
}

void KeyboardShortcutModel::updateConflicts()
{
  const auto allActions =
    m_actions
    | std::views::transform([](const auto& actionInfo) { return &actionInfo.action; })
    | kdl::to_vector;

  m_conflicts = kdl::vec_static_cast<int>(findConflicts(allActions));
  for (const auto& row : m_conflicts)
  {
    const auto index = createIndex(row, 0);
    emit dataChanged(index, index, {Qt::DisplayRole});
  }
}

const KeyboardShortcutModel::ActionInfo& KeyboardShortcutModel::actionInfo(
  const int index) const
{
  assert(index < totalActionCount());
  return m_actions[static_cast<size_t>(index)];
}

int KeyboardShortcutModel::totalActionCount() const
{
  return static_cast<int>(m_actions.size());
}

bool KeyboardShortcutModel::checkIndex(const QModelIndex& index) const
{
  return index.isValid() && index.column() < 3 && index.row() < totalActionCount();
}

} // namespace tb::ui
