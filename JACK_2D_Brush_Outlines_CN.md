# J.A.C.K. 2D 面板绘制逻辑与迁移说明

## 1. 文档目标

这份文档的重点是 `jack.exe` 的 2D 面板绘制模型。

核心问题只有两个：

- 2D 视图是按什么顺序把内容画出来的
- brush outline 在这个顺序里属于哪一层、依赖什么数据、应该如何迁移

当前最重要的结论是：

- J.A.C.K. 的 2D brush outlines 不是先在屏幕空间生成一套“轮廓线”再绘制
- 它本质上是把场景里的几何对象直接放进正交投影里，以线框模式重画
- 选中高亮也不是单独的一套 UI 轮廓算法，而是对象绘制器里的第二遍描边

如果你要做一个 Hammer 风格编辑器，最接近原始行为的实现方式不是“轮廓提取器”，而是“正交 2D 视图 + 场景对象线框绘制器 + 选中态二次描边”

## 2. 整体架构

从迁移角度看，J.A.C.K. 的 2D 面板可以拆成六层：

1. 视图状态层
   - 维护当前视图类型
   - 维护 zoom、pan、可见范围、背景图参数
2. 投影与坐标层
   - 建立当前 2D 正交投影
   - 把世界坐标映射到当前 panel
3. 背景层
   - 背景色
   - 可选背景图
4. 场景层
   - 网格
   - brush/entity/patch/box 等几何对象
   - 选中态线框
5. 工具 overlay 层
   - 框选框
   - 拖拽框
   - gizmo 或临时工具图形
6. HUD 与标签层
   - 对象标题
   - 状态文本
   - 视图统计信息

这六层里，brush outlines 属于第 4 层，和网格、实体盒子、patch 轮廓是同一类内容，不属于 HUD，也不属于工具 overlay。

## 3. 2D 视图的核心状态

迁移时需要保留的最小状态可以抽象成下面这些字段：

```cpp
struct View2DState
{
    AxisPair axes;          // 当前 2D 视图展示的两个世界轴
    int depthAxis;          // 被压平的那个轴
    float zoom;             // 当前缩放
    Vec2 pan;               // 当前平移
    Rect viewportPixels;    // 面板像素区域
    Rect visibleWorldRect;  // 当前世界可见区域
    bool showGrid;
    bool showBackgroundImage;
    bool showLabels;
    bool showStats;
};
```

这里最关键的不是字段名字，而是两个事实：

- 2D 视图始终有一个“当前显示的两个轴”
- 当前 panel 的所有内容都共享同一套正交投影和同一套 world-to-screen 关系

这意味着 brush、entity box、工具框选框只要采用统一的 2D 视图矩阵，就能自然落在同一个平面里。

## 4. 一帧 2D 面板的绘制顺序

J.A.C.K. 的 2D 面板逻辑可以概括成下面这个顺序：

1. 更新当前视图矩阵
2. 清背景
3. 画背景图
4. 画网格和主轴线
5. 应用当前视图方向对应的轴映射
6. 绘制场景对象
7. 绘制选中对象的额外描边
8. 绘制工具相关 overlay
9. 切到屏幕空间
10. 绘制标签、HUD、状态文本

可以把它理解成下面这段伪代码：

```cpp
void Draw2DPanel(const View2DState& view, const Scene& scene)
{
    SetupOrthoProjection(view);
    ClearPanelBackground(view);

    if (view.showBackgroundImage)
        DrawBackgroundImage(view);

    if (view.showGrid)
        DrawGrid(view);

    ApplyAxisOrientation(view);

    DrawScene2D(view, scene);
    DrawSelectionOverlays(view, scene);
    DrawToolOverlays(view, scene);

    SetupScreenSpaceProjection(view);

    if (view.showLabels)
        DrawLabels(view, scene);

    if (view.showStats)
        DrawHud(view, scene);
}
```

迁移时最应该保住的是这个顺序，而不是具体 OpenGL 调用。

## 5. brush outline 的真正逻辑

### 5.1 它不做屏幕空间轮廓提取

J.A.C.K. 的 2D brush outline 不是这种逻辑：

- 先求某个 3D 实体在当前视图下的外轮廓
- 再把外轮廓投影成一条闭合多边形
- 再在 UI 层单独画线

它更接近这种逻辑：

- 2D 面板先建立当前正交投影
- 场景绘制器遍历对象
- 对于 brush、patch、box、实体辅助框等对象，直接调用各自的 2D 绘制器
- 绘制器把对象已有的几何顶点按当前 2D 视图映射成线框

换句话说，brush outline 是“几何重画”，不是“轮廓提取”。

### 5.2 brush outline 属于对象绘制器，不属于 view 控制器

2D view 控制器负责的是：

- 视图矩阵
- 背景
- 网格
- 调度顺序
- HUD

真正决定 brush 怎么画的是对象绘制器。  
在迁移时，这一点非常重要：不要把 brush outline 的业务写死在 `Panel2D::Draw()` 里。

更合理的拆分是：

```cpp
class Scene2DRenderer
{
public:
    void Draw(const View2DState& view, const Scene& scene);
};

class BrushRenderer2D
{
public:
    void DrawBrush(const View2DState& view, const Brush& brush);
    void DrawSelectedBrushOverlay(const View2DState& view, const Brush& brush);
};
```

## 6. Brush 绘制专项

这一节专门回答“brush 到底是怎么在 2D 面板里被画出来的”，并把它翻译成适合迁移实现的语言。

## 6.1 brush 数据如何进入 2D renderer

在 J.A.C.K. 的逻辑里，2D panel 本身并不直接遍历场景对象然后画 brush。  
它更接近下面这条链：

```text
View2DState
  -> 建立正交投影和当前可见范围
  -> world renderer 接收当前 world 和 draw context
  -> scene collector 生成待绘制链表
  -> renderer 按对象类型分发到具体 helper
  -> brush helper 发出实际线框绘制调用
```

这条链的意义很重要：

- panel 控制器只管“当前视图是什么”
- world renderer 只管“当前帧哪些对象要画”
- brush helper 才负责“brush 具体怎么画”

迁移时最好保留这种分层。  
不要让 `Panel2D::Render()` 直接从场景树里一边筛对象一边画线，这样后面很难扩展 hover、锁定、过滤、批量隐藏和工具态。

更合适的抽象是：

```cpp
View2DState
  -> SceneCollector2D::Collect(...)
  -> Scene2DRenderQueue
  -> BrushRenderer2D / BoundsRenderer2D / OverlayRenderer2D
```

## 6.2 两条主要的 brush 绘制路径

当前逆向结果里，2D 场景中的 brush-like 绘制至少分成两大类。

### 路径 A：直接顶点序列型

这一类的特点是：

- 对象已经有一段连续顶点序列
- 绘制器直接把这些顶点作为线框来画
- 更像“多边形边界重画”

从行为上看，它对应的是这种模式：

```cpp
DrawLineLoop(vertices);
```

迁移时可以把它理解成：

- brush 面的轮廓
- 某些已经被简化成边界环的几何
- 不需要额外索引缓冲的线框对象

这条路径最接近“普通 brush outline”的直觉模型。

### 路径 B：索引缓冲型

这一类的特点是：

- 对象不是简单的顶点环
- 绘制时依赖预先组织好的索引数据
- 线框是通过索引把边连接起来，而不是直接按顶点顺序闭环

从行为上看，它更接近：

```cpp
SetPolygonModeLine();
DrawIndexedEdges(vertices, indices);
```

迁移时可以把它理解成：

- patch 或更规则化的 primitive
- 已经预编译出 edge/index 数据的 brush-like 对象
- 比简单 polygon loop 更依赖缓存结构的几何

### 这两条路径为什么要分开

因为它们背后的数据模型不一样：

- 路径 A 更像“给我一圈点，我直接画边”
- 路径 B 更像“给我点和索引，我按索引画线”

如果迁移时强行统一成一种模式，通常会发生两种问题：

- 规则 primitive 失去已有的索引组织优势
- 普通 brush 反而被迫走更重的索引构建流程

最稳妥的做法是保留两种 render item。

## 6.3 基础 pass 和选中 pass 是两回事

J.A.C.K. 的 2D brush 绘制里，最值得保留的一个设计点是：

- 基础线框是一遍
- 选中高亮是第二遍

也就是说，选中对象通常不是“基础线框换个颜色”就结束，而是在基础几何之后再补一遍强调绘制。

迁移时推荐直接保留这个结构：

```cpp
for (auto& item : queue.baseBrushes)
    brushRenderer.DrawBase(item, view);

for (auto& item : queue.selectedBrushes)
    brushRenderer.DrawSelected(item, view);
```

这么做的好处很直接：

- 线宽、颜色、透明度可以独立配置
- hover、锁定、过滤后高亮都能自然扩展
- 更接近 J.A.C.K. 原始视觉结果

如果后面你要做 `SelectionThickOutlines` 一类开关，这种第二遍 pass 的结构也最好接。

## 6.4 2D 里的“裁剪”和“过滤”不应放在 brush helper 内

从迁移视角看，brush helper 最好只关心一件事：

- 当前这个 brush render item 应该如何画

它不应该承担下面这些职责：

- 遍历整个场景
- 决定对象是否属于当前视图
- 做大范围模式过滤
- 决定当前工具态下哪些对象应该完全跳过

这些工作更适合放在 scene collector 或 render queue builder。

推荐的职责拆分是：

- `View2DController`
  - 维护 `visibleWorldRect`
  - 提供当前轴映射和投影信息
- `SceneCollector2D`
  - 做 AABB 与视图矩形的初筛
  - 处理图层、锁定、隐藏、工具模式过滤
  - 决定哪些对象进 base queue，哪些进 selected queue
- `BrushRenderer2D`
  - 只负责绘制收到的 item

如果迁移第一版想先做简单实现，裁剪策略也不需要过度复杂：

1. 收集阶段先用对象 AABB 与 `visibleWorldRect` 做粗筛
2. 绘制阶段直接交给 GPU 的线段裁剪
3. 只有当某些大型对象在 2D 里产生明显多余绘制时，再补细粒度 edge clipping

这比一开始就在 brush helper 里手写复杂裁剪逻辑更稳。

## 6.5 迁移时推荐的数据结构

如果目标是接近 J.A.C.K. 的 2D brush 绘制行为，建议至少准备两层结构：

### 场景收集后的 render item

```cpp
enum class PrimitiveKind2D
{
    BrushLoop,
    IndexedBrush,
    EntityBounds,
    PatchBounds,
    ToolOverlay,
};

struct BrushRenderItem2D
{
    PrimitiveKind2D kind;
    BrushId sourceBrush;
    Span<Vec2> vertices;
    Span<uint16_t> indices;
    Color baseColor;
    Color selectedColor;
    float baseLineWidth;
    float selectedLineWidth;
    uint32_t flags;
};
```

这里最重要的是 `kind`、`vertices`、`indices` 和两套颜色/线宽。  
它们刚好对应前面说的两条主要路径和第二遍选中 pass。

### 一帧 2D 面板的绘制队列

```cpp
struct Scene2DRenderQueue
{
    std::vector<BrushRenderItem2D> baseBrushes;
    std::vector<BrushRenderItem2D> selectedBrushes;
    std::vector<BrushRenderItem2D> bounds;
    std::vector<OverlayItem2D> overlays;
    std::vector<LabelItem2D> labels;
};
```

这个队列模型的价值在于：

- 基础 brush 和选中 brush 明确分离
- bounds 不和 brush 混在一起
- overlay 不和 scene geometry 混在一起
- label/HUD 可以在 screen pass 单独处理

## 6.6 如果你要做 1:1 风格复刻，重点保哪几个特性

如果目标不是“做个大致类似的 2D panel”，而是更接近 J.A.C.K. / Hammer 的操作手感，那么 brush 绘制部分最值得保留的是这几个点：

1. 正交视图里的几何重画，而不是 silhouette 提取
2. 基础线框和选中高亮分两遍
3. 普通 brush 路径和 indexed primitive 路径分开
4. bounds / overlay / label 从 brush 渲染器里拆出去
5. 可见性和过滤前移到 scene collector

只要这五点保住，底层图形 API 换掉也不会影响整体结构。

## 7. 迁移时应保留的三种场景绘制层

从效果上看，J.A.C.K. 的 2D 场景层至少包含三种不同职责：

### 7.1 基础对象线框

这是最基础的一层，用来表达：

- brush 的边
- patch 的边
- 实体 box 或辅助框
- 其它可编辑对象的几何边界

这层通常只做一件事：把对象以当前视图对应的 2D 线框形式画出来。

### 7.2 选中态额外描边

选中对象不是换一个“对象类型”来画，而是在原本对象线框基础上再补一层高亮。

迁移时最好显式保留第二遍 pass：

```cpp
for (auto& obj : visibleObjects)
    DrawBaseOutline(obj);

for (auto& obj : selectedObjects)
    DrawSelectedOutline(obj);
```

这样做有几个好处：

- 更接近 J.A.C.K. 原始行为
- 颜色、粗细、透明度策略更容易统一
- 后面做“粗轮廓”“hover outline”“锁定对象 outline”也更好扩展

### 7.3 非 brush 的辅助几何

2D 面板里不只有 brush 轮廓。还有很多“看起来像 outline”的内容，其实语义不同：

- entity 的边界盒
- sprite/model 的包围框
- 当前工具的拖拽框
- 某些模式下的临时矩形或提示框

迁移时最好不要把这些全塞到一个 `DrawBrushOutlines()` 里。  
应该分成：

- `DrawBrushGeometry2D`
- `DrawEntityBounds2D`
- `DrawToolOverlays2D`

## 8. 2D 面板真正要迁移的不是 OpenGL，而是数据流

从工程实现角度，最值得迁移的是下面这条数据流：

```text
View2DState
  -> 生成当前正交投影
  -> 决定当前显示的两个轴
  -> 计算当前可见世界范围
  -> 场景遍历筛出可见对象
  -> 每种对象交给自己的 2D 绘制器
  -> 选中对象追加第二遍高亮
  -> 最后再画 UI/HUD
```

只要这条数据流对了，底层是 OpenGL、D3D、Qt、ImGui、Skia 还是你自己的 renderer，都不是关键问题。

## 9. 适合 Hammer 风格编辑器的模块拆分

如果你要在一个类似 Hammer 的编辑器里落地，我建议至少拆成下面几个模块：

### `View2DController`

负责：

- 当前视图类型
- zoom / pan
- 鼠标世界坐标换算
- 视图矩阵
- 世界可见区域

### `GridRenderer2D`

负责：

- 细网格
- 粗网格
- 主轴线
- 网格吸附可视反馈

### `SceneCollector2D`

负责：

- 根据 `visibleWorldRect` 筛出当前 2D 面板需要画的对象
- 按对象类型分桶
- 分离普通对象、选中对象、工具对象

### `BrushRenderer2D`

负责：

- 普通 brush 线框
- 选中 brush 第二遍描边
- 可选 hover 状态

### `OverlayRenderer2D`

负责：

- 框选框
- 拖拽矩形
- gizmo
- 其它临时工具图形

### `HudRenderer2D`

负责：

- 标题文字
- 数量统计
- 当前 zoom / 坐标 / 状态信息

这套模块划分比“一个大函数按顺序把所有东西都画了”更适合迁移后的长期维护。

## 10. brush outline 的推荐实现方式

如果目标是“像 J.A.C.K. / Hammer 一样工作”，建议用下面的策略实现 brush outline：

### 基础策略

- brush 保留自己的几何顶点或边列表
- 2D 视图只负责选择当前要显示的两个轴
- 绘制时把 3D 点投到当前 2D 平面
- 按边列表直接画线

### 选中策略

- 普通 brush：细线、普通颜色
- 选中 brush：第二遍重画、强调色、可选更粗线宽

### 不建议的策略

- 先把整个 3D brush 求 silhouette 再画
- 把 brush outline 和工具 overlay 混在一个 renderer
- 把所有对象都转成一个统一的“2D 多边形缓存”后再统一处理

后两种做法短期看起来简单，但后面做 entity、patch、sprite、bounds、hover、锁定对象时会很快变乱。

## 11. 一个可直接落地的渲染骨架

```cpp
void OrthoPanel::Render()
{
    UpdateViewState();

    renderer.BeginWorldPass(viewState);
    renderer.ClearBackground(colors.background);

    backgroundRenderer.Draw(viewState);
    gridRenderer.Draw(viewState);

    auto visible = collector.Collect(scene, viewState);

    sceneRenderer.DrawWorldGeometry(viewState, visible.normalObjects);
    sceneRenderer.DrawSelection(viewState, visible.selectedObjects);
    overlayRenderer.Draw(viewState, toolState);

    renderer.BeginScreenPass(viewState.viewportPixels);
    labelRenderer.Draw(viewState, visible.labeledObjects);
    hudRenderer.Draw(viewState, scene, toolState);
}
```

对应到实际职责：

- `BeginWorldPass` 对应原本的正交矩阵和坐标方向准备
- `DrawWorldGeometry` 对应原本的 world draw 和对象 helper
- `DrawSelection` 对应原本对象 helper 里的第二遍 outline
- `BeginScreenPass` 对应原本从世界空间切回屏幕空间再画文本

## 12. 迁移时最容易出错的点

### 视图方向

不同 2D 视图不是简单共用同一套 XY 投影。  
它们的显示轴和旋转关系不同，必须抽象成“当前视图使用哪两个轴”。

### world pass 和 screen pass 混用

网格、brush、entity bounds 应该在世界空间 pass。  
标签、HUD、状态字应该在屏幕空间 pass。

### 选中态只改颜色、不走第二遍绘制

只改颜色通常不够，后面你会发现 hover、锁定、过滤、高亮都不好扩展。  
保留第二遍 selection pass 更稳。

### 把工具矩形当成 brush 轮廓的一部分

框选框、裁剪框、拖拽框属于 overlay，不属于场景几何本体。

## 13. 对迁移最有价值的最终结论

如果把这次逆向结果压缩成一句实现建议，就是：

“用一个正交 2D 视图去重画场景对象的几何边界，再对选中对象追加第二遍描边，最后再单独画 overlay 和 HUD。”

这比“提取 brush 的 2D 轮廓再画 UI 线条”更接近 J.A.C.K. 的真实工作方式，也更适合迁移到 Hammer 风格编辑器。

## 
