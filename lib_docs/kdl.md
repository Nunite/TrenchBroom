# kdl 调用文档

## 定位

`kdl` 是 TrenchBroom 的通用工具库，包含：
- `kdl::result`：带错误类型的返回值/管道式组合（TrenchBroom 大量使用）
- 路径/文件读写：`parse_path`、`read_file`、临时文件等
- 字符串处理：split/join/替换/数值解析
- C++20 ranges 视图扩展 + `ranges::to`
- 反射/打印宏、轻量容器（如 `vector_set`）、任务池 `task_manager`

构建信息：
- target：`kdl`（STATIC），见 [lib/kdl/CMakeLists.txt](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/kdl/CMakeLists.txt)
- include：`lib/kdl/src`（`target_include_directories(kdl PUBLIC ${KDL_SOURCE_DIR})`）

## 最常用：`kdl::result` 与管道式组合

入口头文件：
- [result.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/kdl/src/kdl/result.h)
- [result_error.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/kdl/src/kdl/result_error.h)
- [result_io.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/kdl/src/kdl/result_io.h)

### 核心类型

- `template <typename Value, typename... Errors> class kdl::result`：成功时包含 `Value`，失败时包含 `Errors...` 中之一
- `kdl::result_error{ std::string msg; }`：工程中默认错误类型（`tb::Error` 也是它）

工程内别名：
- `tb::Result<Value, ...>` 是 `kdl::result` 的别名，见 [Result.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/common/src/Result.h)

### 管道适配器（`operator|` 风格）

`result.h` 在 `namespace kdl` 提供了一组适配器函数，典型链式写法：

```cpp
return someResult()
  | kdl::transform([](auto v) { return f(v); })
  | kdl::transform_error([](const auto& e) { return fallbackValue; })
  | kdl::value();
```

已确认存在（见 [result.h:L2984-L3039](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/kdl/src/kdl/result.h#L2984-L3039)）：
- `kdl::and_then(F)`：成功分支继续返回 `result`（用于串联）
- `kdl::or_else(F)`：错误分支恢复/替换为另一个 `result`
- `kdl::transform(F)`：成功分支映射值类型
- `kdl::transform_error(F)`：错误分支映射为“成功值”（返回 `result<Value>`）
- `kdl::if_error(F)`：仅用于副作用（日志等），不改变错误
- `kdl::value()`：提取值（若是错误会抛 `bad_result_access`）
- `kdl::value_or(x)`：提取值或备用值
- `kdl::error()`：提取错误
- `kdl::ignore()`：丢弃结果
- `kdl::is_success()`：返回 `bool`

对应成员函数语义示例：
- `result::transform` / `result::transform_error` 的关键实现见 [result.h:L767-L879](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/kdl/src/kdl/result.h#L767-L879)

### 工程内真实用法（可参考的调用模式）

- `transform_error + value()` 生成“兜底值”：
  - [AssetUtils.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/common/src/mdl/AssetUtils.h)
- `transform + transform_error + value()` 做“读取 -> 校验/询问 -> 返回”：
  - [TrenchBroomApp.cpp](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/common/src/TrenchBroomApp.cpp)
- `and_then` 串联多个可能失败的步骤：
  - [LinkedGroupUtils.cpp](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/common/src/mdl/LinkedGroupUtils.cpp)

## 路径与文件

入口：
- [path_utils.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/kdl/src/kdl/path_utils.h)
- [filesystem_utils.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/kdl/src/kdl/filesystem_utils.h)

### 路径

- `kdl::parse_path(std::basic_string<...>, bool convert_separators=true)`：将 `\`/`/` 统一为 `std::filesystem::path::preferred_separator`
- `path_add_extension / path_remove_extension / path_replace_extension`：扩展名操作（实现见 [path_utils.cpp](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/kdl/src/kdl/path_utils.cpp)）

### 文件

- `kdl::read_file(path) -> result<std::string, result_error>`：读文件内容
- `kdl::tmp_file`：自动清理的临时文件路径持有者（可通过 `set_auto_remove(false)` 关闭）

## 字符串工具

入口：[string_utils.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/kdl/src/kdl/string_utils.h)

常用 API：
- `str_split(str, delims) -> vector<string>`：支持反斜杠转义
- `str_join(...)`：多种重载，支持“最后一个分隔符不同”的 join
- `str_replace_every(haystack, needle, replacement)`
- `str_to_int/str_to_long/.../str_to_double`：安全数值解析，失败返回 `std::optional{}`

工程内使用例：
- 过滤输入拆词：见 [MaterialBrowserView.cpp](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/common/src/ui/MaterialBrowserView.cpp)

## ranges 扩展

入口：
- [ranges/to.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/kdl/src/kdl/ranges/to.h)
- `lib/kdl/src/kdl/ranges/*.h`（`zip_view`/`zip_transform`/`enumerate`/`chunk` 等）

### `kdl::ranges::to`

`kdl::ranges::to<C>(range)`：将 range 收集到容器 `C`（接近 C++23 `std::ranges::to` 的语义）。

工程内常见写法：

```cpp
auto v = someRange | kdl::ranges::to<std::vector>();
```

示例：见 [MaterialBrowserView.cpp](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/common/src/ui/MaterialBrowserView.cpp)

### `kdl::views::zip_transform`

用于并行遍历多个 range，并对每一组元素调用函数：
- 定义见 [zip_transform_view.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/kdl/src/kdl/ranges/zip_transform_view.h)
- 工程内示例：见 [LinkedGroupUtils.cpp](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/common/src/mdl/LinkedGroupUtils.cpp)

## 任务池

入口：[task_manager.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/kdl/src/kdl/task_manager.h)

- `kdl::task_manager`：固定线程池
- `run_task(std::function<R()>) -> std::future<R>`
- `run_tasks(range<task>) -> vector<future<...>>`
- `run_tasks_and_wait(range<task>) -> vector<result>`

## 轻量有序集合：`kdl::vector_set`

入口：[vector_set.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/kdl/src/kdl/vector_set.h)

- `kdl::vector_set<T, Compare, Allocator>`：底层 `std::vector` + 排序去重，提供 set 语义（通常比 `std::set` 更 cache-friendly）

## 备注

- `kdl::value()` 会在错误时抛异常；工程里更常见的模式是先 `transform_error` 转成可用值，再 `value()`。
- `kdl::result` 的错误类型列表可含多个类型；如果你用 `std::visit`/`overload` 处理错误，通常要覆盖全部错误分支。
