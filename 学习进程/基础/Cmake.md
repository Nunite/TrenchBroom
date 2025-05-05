# 初始命令

## 项目设置:
- `cmake_minimum_required(VERSION 3.10)` 指定cmake最低要求的版本
- `project(MyProject VERSION 1.0 LANGUAGES CXX)` 指定项目的名称、版本、语言

## 编译设置:
- `set(CMAKE_CXX_STANDARD 11)` 指定C++标准为11
- `set(CMAKE_CXX_STANDARD_REQUIRED ON)` 设置变量
如果出现编译错误很有可能是这里没有添加源文件，头文件
- `set(COMMON_SOURCE main.cpp utils.cpp)` 指定源文件列表
- `set(COMMON_HEADER ---)` 指定头文件列表
- `add_compile_options()` 添加编译选项

## 库与可执行文件:
- `add_executable(EexName $(COMMON_HEADER))` 根据`COMMON_SOURCE`列表创建可执行文件
- `add_library(common OBJECT ${COMMON_SOURCE} ${COMMON_HEADER})`  将`COMMON_SOURCE`的源文件编译为OBJ文件方便其他文件使用，避免重复编译
