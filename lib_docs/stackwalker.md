# stackwalker 调用文档

## 定位

`lib/stackwalker` 是一个第三方 Windows 栈回溯库（StackWalker），在 TrenchBroom 中主要用于：
- Windows + MSVC 平台捕获崩溃时的调用栈
- 生成 crash report

构建信息：
- 仅在 MSVC 下构建：见 [lib/stackwalker/CMakeLists.txt](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/stackwalker/CMakeLists.txt)
- include：`lib/stackwalker/include`，入口头文件 [StackWalker.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/stackwalker/include/StackWalker.h)

## 在 TrenchBroom 中的推荐用法（不要直接用 StackWalker）

TrenchBroom 对 StackWalker 做了一层封装：
- [TrenchBroomStackWalker.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/common/src/TrenchBroomStackWalker.h)
- [TrenchBroomStackWalker.cpp](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/common/src/TrenchBroomStackWalker.cpp)

提供的 API：
- `tb::TrenchBroomStackWalker::getStackTrace() -> std::string`
- （Windows/MSVC）`tb::TrenchBroomStackWalker::getStackTraceFromContext(void* context) -> std::string`

实现要点：
- StackWalker 非线程安全，封装层用 `QMutex` 保护共享实例（见 [TrenchBroomStackWalker.cpp](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/common/src/TrenchBroomStackWalker.cpp)）

## 崩溃捕获入口

- Windows/MSVC：设置 `SetUnhandledExceptionFilter`，并从异常上下文取栈回溯：
  - 见 [TrenchBroomApp.cpp:L882-L900](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/common/src/TrenchBroomApp.cpp#L882-L900)
- 非 Windows：使用 `backtrace/backtrace_symbols` 走另一套实现（同一个封装 API）

## 直接使用 StackWalker 的最小方式（仅供理解）

如果你必须直接用第三方类（通常不建议），关键点是重载 `OnOutput` 收集字符串，然后调用 `ShowCallstack(...)`：
- TrenchBroom 自己的派生类示例：见 [TrenchBroomStackWalker.cpp](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/common/src/TrenchBroomStackWalker.cpp)

