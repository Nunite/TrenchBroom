# TrenchBroom `lib` 包装库调用文档

本文件夹包含对 `d:\Code_Development\Source_code\CPP\TrenchBroom\lib` 下各子库的中文调用说明，目标是：
- 明确每个库在工程中的定位
- 给出对外入口（头文件、CMake target、include path）
- 总结核心 API 与在 TrenchBroom 里的典型用法

## 目录

- [kdl](./kdl.md)
- [vm](./vm.md)
- [upd](./upd.md)
- [stackwalker](./stackwalker.md)

## 子库一览（构建与入口）

这些子库由 [lib/CMakeLists.txt](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/CMakeLists.txt) 加入构建：

| 子库 | CMake Target | include 入口 | 主要用途 |
|---|---:|---|---|
| kdl | `kdl` (STATIC) | `lib/kdl/src` | 通用工具库：`result` 管道、路径/字符串、ranges views、任务池等 |
| vm | `vm` (INTERFACE) | `lib/vm/include` | 数学几何类型：`vec/mat/plane/bbox` 等 |
| upd | `upd` (STATIC) | `lib/upd/src` | Qt6 应用自更新（GitHub Releases + 下载 + 解压 + 退出时安装） |
| stackwalker | `stackwalker` (STATIC, MSVC only) | `lib/stackwalker/include` | Windows/MSVC 栈回溯（第三方 StackWalker） |

## 工程内的典型集成点

- `kdl::result` 在工程中被包装为 `tb::Result`：见 [Result.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/common/src/Result.h)
- 自更新配置与落地：见 [ui/UpdateConfig.cpp](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/common/src/ui/UpdateConfig.cpp)
- Windows 栈回溯封装：见 [TrenchBroomStackWalker.cpp](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/common/src/TrenchBroomStackWalker.cpp) 与 [TrenchBroomApp.cpp](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/common/src/TrenchBroomApp.cpp)
