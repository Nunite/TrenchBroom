# vm 调用文档

## 定位

`vm` 是 TrenchBroom 使用的几何/线代基础库，提供：
- 向量 `vm::vec<T, N>`
- 矩阵 `vm::mat<T, R, C>`（列主序）
- 平面 `vm::plane<T, N>`、包围盒 `vm::bbox<T, N>`、射线/线段/多边形/四元数等

构建信息：
- target：`vm`（INTERFACE），见 [lib/vm/CMakeLists.txt](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/vm/CMakeLists.txt)
- include：`lib/vm/include`（使用时 `#include "vm/xxx.h"`）

## 对外入口头文件

`vm` 的公共头文件集中在 `lib/vm/include/vm`：
- `vec.h`：向量
- `mat.h`：矩阵
- `plane.h`：平面
- `bbox.h`：AABB
- 其它：`ray.h`、`segment.h`、`polygon.h`、`quat.h`、`intersection.h`、`distance.h` 等（见 [lib/vm/CMakeLists.txt](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/vm/CMakeLists.txt) 的 `target_sources` 列表）

## 核心类型与常用 API

### `vm::vec<T, S>`

入口：[vec.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/vm/include/vm/vec.h)

- 存储：`T v[S]`
- 构造：默认全 0、initializer_list、显式逐分量构造、不同维度/类型转换构造
- 常用访问：`operator[]`，以及 `x()/y()/z()/w()`（按维度启用）
- 工厂：`vec::fill(value)`、`vec::axis(index)`

典型写法：

```cpp
vm::vec<float, 3> a{1.0f, 2.0f, 3.0f};
a[0] = 10.0f;
a.x();
```

### `vm::mat<T, R, C>`

入口：[mat.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/vm/include/vm/mat.h)

- 存储：`vec<T, R> v[C]`（列主序）
- 默认构造：单位矩阵
- 工厂：`identity()`、`zero()`、`fill(value)`
- 提供大量 4x4 变换矩阵辅助（旋转、缩放等，按模板约束启用）

注意点：
- initializer_list 构造的输入按“行主序”给出，但内部仍按“列主序”存储（见 [mat.h:L77-L88](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/vm/include/vm/mat.h#L77-L88)）

### `vm::plane<T, S>`

入口：[plane.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/vm/include/vm/plane.h)

- 表示：`distance` + `normal`
- 构造：
  - `(distance, normal)`
  - `(anchorPoint, normal)`：`distance = dot(anchor, normal)`
- 常用：
  - `anchor()`：返回距原点最近的平面上一点（假设 normal 已归一化）
  - `point_distance(p)`：点到平面有符号距离
  - `point_status(p, epsilon)`：above/below/inside
  - `flip()`
  - `transform(mat)`：平面变换

### `vm::bbox<T, S>`

入口：[bbox.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/vm/include/vm/bbox.h)

- 表示：`min`/`max` 两点，保持 `min[i] <= max[i]`
- 构造：默认原点零尺寸、`(min,max)`、`(minScalar,maxScalar)` 等
- `bbox::builder`：从点集/多个 bbox 累积合并构建包围盒（见 [bbox.h:L49-L123](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/vm/include/vm/bbox.h#L49-L123)）

## 工程内使用线索

在 `common/src` 下大量出现 `vm::vec3d / vm::bbox3d / vm::plane3d` 等别名/类型（别名通常在其它 vm 头文件中提供），建议直接从调用点反查需要的头文件。

如果你想从“某个算法/模块”出发定位 vm 的用法，优先搜：
- `vm::bbox`（空间裁剪、可见性、选择）
- `vm::plane`（几何分类、切割、面计算）
- `vm::mat`（视图/相机变换）
