# TrenchBroom功能设计：以现有实体为模板创建多个独立brush实体

## 项目分析

TrenchBroom是一个强大的地图编辑器，主要用于Quake和类似游戏引擎。根据代码分析，这个项目有以下几个重要组件：

1. **实体管理系统**：
   - `EntityNode`：表示实体节点
   - `BrushNode`：表示笔刷节点
   - `Entity`：包含实体属性的类
   - `EntityDefinition`：实体定义（点实体/笔刷实体）

2. **实体创建机制**：
   - `createBrushEntity`方法：将选中的brush转换为实体
   - `reparentNodes`方法：将节点重新分配给新的父节点
   - 目前的实现总是将多个选中的brush添加到同一个实体中

3. **用户交互系统**：
   - `MapViewBase`类处理上下文菜单和用户交互
   - 通过事务（Transaction）系统进行地图操作

4. **属性复制**：
   - 当从相同实体创建新实体时，系统会复制属性
   - 但目前没有直接从一个实体向每个单独的brush应用属性的功能

## 功能设计：实体模板应用器

### 功能描述

设计一个新功能，允许用户：
1. 选择一个现有的brush实体作为模板
2. 然后点击其他brush（可以是worldspawn的一部分或其他实体的一部分，但是不能是点实体）
3. 将每个点击的brush转换为独立的实体，并应用模板实体的所有属性


### 实现思路

1. **添加新的状态管理**：
   ```cpp
   class TemplateEntityApplier {
   private:
     mdl::Entity m_templateEntity;
     bool m_hasTemplate;
     std::string m_entityClassName;
     
   public:
     void setTemplate(const mdl::EntityNode* templateNode);
     void clearTemplate();
     bool hasTemplate() const;
     mdl::EntityNode* createEntityFromTemplate(mdl::BrushNode* brushNode);
   };
   ```

2. **添加新的工具类**：
   ```cpp
   class ApplyEntityTemplateTool : public Tool {
   private:
     std::weak_ptr<MapDocument> m_document;
     TemplateEntityApplier m_applier;
     
   public:
     ApplyEntityTemplateTool(std::weak_ptr<MapDocument> document);
     
     bool applies() const;
     bool activate();
     void deactivate();
     
     bool mouseClick(const InputState& inputState);
     
     void setTemplate(const mdl::EntityNode* templateNode);
     void clearTemplate();
   };
   ```

3. **修改MapDocument类**添加新方法：
   ```cpp
   mdl::EntityNode* MapDocument::createSingleBrushEntity(
     mdl::BrushNode* brushNode, const mdl::Entity& templateEntity) {
     // 复制模板实体属性
     auto entity = templateEntity;
     
     // 创建新实体节点
     auto* entityNode = new mdl::EntityNode{std::move(entity)};
     
     // 事务操作
     auto transaction = Transaction{*this, "Create Entity from Template"};
     deselectAll();
     
     // 添加实体节点
     if (addNodes({{parentForNodes(), {entityNode}}}).empty()) {
       transaction.cancel();
       return nullptr;
     }
     
     // 将brush重新分配给新实体
     if (!reparentNodes({{entityNode, {brushNode}}})) {
       transaction.cancel();
       return nullptr;
     }
     
     selectNodes({brushNode});
     
     if (!transaction.commit()) {
       return nullptr;
     }
     
     return entityNode;
   }
   ```

4. **添加用户界面交互**：
   - 在上下文菜单中添加"Set as Entity Template"选项
   - 添加"Apply Entity Template"工具按钮
   - 添加快捷键支持

5. **实现工作流程**：
   - 用户右键点击一个实体，选择"Set as Entity Template"
   - 工具状态变为活跃，并存储模板实体的属性
   - 用户点击其他brush，每次点击都创建一个新的实体
   - 完成后，用户可以按Esc键退出工具模式

### 修改文件

需要修改以下文件：
1. `MapDocument.h/cpp` - 添加新方法支持
2. `MapViewBase.h/cpp` - 添加上下文菜单和工具
3. 新建 `ApplyEntityTemplateTool.h/cpp` - 实现新工具
4. `Resources.h` - 添加新的图标和快捷键定义

### 技术挑战

1. **状态管理**：
   - 需要维护模板实体状态
   - 需要提供清晰的视觉反馈表明工具是否活跃

2. **撤销/重做支持**：
   - 确保所有操作都可以通过事务系统撤销/重做

3. **有效性检查**：
   - 确保模板实体是有效的
   - 确保点击的是有效的brush

## 用户界面设计

1. 当选择一个实体后，右键菜单添加：
   "Set as Entity Template"

2. 在主工具栏添加新按钮：
   "Apply Entity Template"

3. 当工具激活时，鼠标光标变化以指示模式状态

4. 状态栏显示提示：
   "Click on a brush to create an entity from template" 

## 使用流程

1. 用户创建并设置一个func_door实体，配置好所有参数
2. 用户右键点击该实体，选择"Set as Entity Template"
3. 工具进入模板应用模式，鼠标指针变化
4. 用户点击其他brush，每次点击都会：
   - 创建一个新的func_door实体
   - 将点击的brush移动到该实体
   - 应用模板实体的所有属性
5. 用户按Esc键或点击其他工具退出该模式
