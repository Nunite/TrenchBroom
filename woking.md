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
2. 然后选择其他brush（可以是worldspawn的一部分或其他实体的一部分，但是不能是点实体）
3. 通过上下文菜单将选中的brush转换为独立的实体，并应用模板实体的所有属性

### 实现思路

1. **在MapViewBase类中添加模板实体存储**：
   ```cpp
   // 在MapViewBase.h中添加
   private:
     std::optional<mdl::Entity> m_templateEntity;
     std::string m_templateEntityClassName;
     
   public:
     bool hasTemplateEntity() const;
     void setTemplateEntity(const mdl::EntityNode* entityNode);
     void clearTemplateEntity();
     const mdl::Entity* templateEntity() const;
   ```

2. **在MapDocument类中添加创建单个brush实体的方法**：
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

3. **在MapViewBase中实现上下文菜单处理**：
   ```cpp
   void MapViewBase::showPopupMenuLater() {
     beforePopupMenu();

     auto document = kdl::mem_lock(m_document);
     // 现有代码...
     
     // 添加实体模板相关菜单项
     const auto& selectedNodes = document->selectedNodes().nodes();
     
     // 检查是否只选择了一个实体
     bool canSetTemplate = false;
     mdl::EntityNode* selectedEntityNode = nullptr;
     
     if (selectedNodes.size() == 1) {
       if (auto* entityNode = dynamic_cast<mdl::EntityNode*>(selectedNodes.front())) {
         canSetTemplate = true;
         selectedEntityNode = entityNode;
       }
     }
     
     // 检查是否可以应用模板
     bool canApplyTemplate = hasTemplateEntity() && document->selectedNodes().hasOnlyBrushes();
     
     // 添加菜单项
     if (canSetTemplate) {
       menu.addSeparator();
       menu.addAction(tr("Set as Entity Template"), this, &MapViewBase::setSelectedEntityAsTemplate);
     }
     
     if (canApplyTemplate) {
       menu.addSeparator();
       menu.addAction(
         tr("Apply Entity Template (%1)").arg(QString::fromStdString(m_templateEntityClassName)),
         this, 
         &MapViewBase::applyEntityTemplate);
     }
     
     // 现有代码...
     
     menu.exec(QCursor::pos());
     
     // 现有代码...
   }
   ```

4. **实现相关方法**：
   ```cpp
   void MapViewBase::setSelectedEntityAsTemplate() {
     auto document = kdl::mem_lock(m_document);
     const auto& selectedNodes = document->selectedNodes().nodes();
     
     if (selectedNodes.size() == 1) {
       if (auto* entityNode = dynamic_cast<mdl::EntityNode*>(selectedNodes.front())) {
         setTemplateEntity(entityNode);
       }
     }
   }
   
   void MapViewBase::applyEntityTemplate() {
     if (!hasTemplateEntity()) {
       return;
     }
     
     auto document = kdl::mem_lock(m_document);
     const auto& selectedNodes = document->selectedNodes().nodes();
     
     // 过滤出所有的brush节点
     std::vector<mdl::BrushNode*> brushNodes;
     for (auto* node : selectedNodes) {
       if (auto* brushNode = dynamic_cast<mdl::BrushNode*>(node)) {
         brushNodes.push_back(brushNode);
       }
     }
     
     if (brushNodes.empty()) {
       return;
     }
     
     // 为每个brush创建一个新实体
     auto transaction = Transaction{*document, "Apply Entity Template"};
     
     for (auto* brushNode : brushNodes) {
       document->createSingleBrushEntity(brushNode, *templateEntity());
     }
     
     transaction.commit();
   }
   ```

### 修改文件

需要修改以下文件：
1. `MapViewBase.h/cpp` - 添加模板实体存储和上下文菜单处理
2. `MapDocument.h/cpp` - 添加创建单个brush实体的方法

### 技术挑战

1. **状态管理**：
   - 需要维护模板实体状态
   - 需要在上下文菜单中显示当前模板实体的类名

2. **撤销/重做支持**：
   - 确保所有操作都可以通过事务系统撤销/重做

3. **有效性检查**：
   - 确保模板实体是有效的
   - 确保选中的是有效的brush

## 用户界面设计

1. 当选择一个实体后，右键菜单添加：
   "Set as Entity Template"

2. 当有模板实体并且选择了brush时，右键菜单添加：
   "Apply Entity Template (entity_class_name)"

## 使用流程

1. 用户创建并设置一个func_door实体，配置好所有参数
2. 用户选择该实体，右键点击并选择"Set as Entity Template"
3. 用户选择一个或多个brush
4. 用户右键点击并选择"Apply Entity Template (func_door)"
5. 系统为每个选中的brush创建一个独立的func_door实体，并应用模板实体的所有属性
