# upd（Update Library）调用文档

## 定位

`upd` 是一个用于更新 GitHub Releases 上发布的 Qt6 应用的库，提供：
- 查询更新：GitHub API 获取 release 列表/最新 release
- 下载更新：通过 `HttpClient` 下载 asset
- 准备更新：可配置（例如解压 zip）
- 安装更新：应用退出时在析构阶段触发安装脚本（跨平台示例脚本已提供）

构建信息：
- target：`upd`（STATIC），见 [lib/upd/CMakeLists.txt](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/upd/CMakeLists.txt)
- 依赖：`Qt6::Widgets`、`Qt6::Network`
- include：`lib/upd/src`（`target_include_directories(upd PUBLIC ${LIB_UPDATE_SOURCE_DIR})`）

上游说明：见 [lib/upd/README.md](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/upd/README.md)

## 对外入口（你真正需要 include 的）

- 更新入口：
  - [Updater.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/upd/src/upd/Updater.h)
  - [UpdateConfig.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/upd/src/upd/UpdateConfig.h)
  - [UpdateController.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/upd/src/upd/UpdateController.h)
- 网络抽象：
  - [HttpClient.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/upd/src/upd/HttpClient.h)
  - （Qt 实现）[QtHttpClient.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/upd/src/upd/QtHttpClient.h)
- 辅助：
  - [GithubApi.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/upd/src/upd/GithubApi.h)
  - [Unzip.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/upd/src/upd/Unzip.h)
  - [InstallUpdate.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/upd/src/upd/InstallUpdate.h)

## 核心概念

### `upd::UpdateConfig`

见 [UpdateConfig.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/upd/src/upd/UpdateConfig.h)

`UpdateConfig` 是“把更新过程接到你的应用上”的配置，核心字段：
- `checkForUpdates(UpdateController&)`：你来决定怎么调用 `UpdateController::checkForUpdates<Version>(...)`
- `prepareUpdate(downloadedPath, config) -> optional<QString>`：把下载的包变成“可安装内容”（例如解压）
- `installUpdate(preparedPath, config, restartApp)`：启动脚本/进程在应用退出后完成替换

以及一些固定参数：GitHub org/repo、脚本路径、app 目录、workdir、日志文件等。

### `upd::HttpClient` / `upd::HttpOperation`

见 [HttpClient.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/upd/src/upd/HttpClient.h)

- `HttpClient::get(url, onBody, onError) -> HttpOperation*`
- `HttpClient::download(url, onFile, onError) -> HttpOperation*`
- `HttpOperation::cancel()` + `progress()`

`upd` 自身不绑定具体网络实现；工程用 Qt 的实现（`QtHttpClient`）。

### `upd::UpdateController`

见 [UpdateController.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/upd/src/upd/UpdateController.h)

- `checkForUpdates()`：对外触发（实际调用 `UpdateConfig::checkForUpdates(*this)`）
- `checkForUpdates<Version>(...)`：模板实现（内部调用 GitHub API），签名见 [UpdateController.h:L181-L227](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/upd/src/upd/UpdateController.h#L181-L227)
- `downloadAndPrepareUpdate()`：下载 asset 并调用 `UpdateConfig::prepareUpdate`
- `cancelPendingOperation()` / `reset()` / `setRestartApp(bool)`
- `state()` + `stateChanged` 信号：UI 层通过它响应状态机

重要行为：
- 若析构时处于“更新待安装”状态，会触发安装（见 [UpdateController.cpp:L230-L238](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/upd/src/upd/UpdateController.cpp#L230-L238)）。因此 `UpdateController/Updater` 生命周期必须覆盖整个应用生命周期。

### `upd::Updater`

见 [Updater.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/upd/src/upd/Updater.h)

这是最推荐的入口封装：
- 构造：`Updater(HttpClient&, optional<UpdateConfig>, parent)`
- `showUpdateDialog()`：弹更新对话框
- `checkForUpdates()` / `reset()`
- `createUpdateIndicator(parent)`：返回一个状态指示控件（label）

## 工程内的落地示例（TrenchBroom 是怎么接的）

TrenchBroom 在 [common/src/ui/UpdateConfig.cpp](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/common/src/ui/UpdateConfig.cpp) 里实现了：
- 是否启用自动更新（不同平台不同判断）
- `UpdateVersion` 解析/比较/描述
- `prepareUpdate`：下载 zip 后调用 `upd::unzip(...)` 解压到 workdir
- `installUpdate`：调用 `upd::installUpdate(...)`，传入脚本、目录、日志等
- 最终组装 `upd::UpdateConfig` 并返回 `std::optional<UpdateConfig>`

你要集成到自己的 Qt 应用，最接近的参考就是这个文件。

## 辅助函数

- `upd::unzip(zipPath, destPath, logFilePath) -> bool`：见 [Unzip.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/upd/src/upd/Unzip.h)
- `upd::installUpdate(scriptFolderPath, targetPath, sourcePath, relativeAppPath, tempFolderPath, logFilePath, requiresAdminPrivileges, restartApp) -> bool`：见 [InstallUpdate.h](file:///d:/Code_Development/Source_code/CPP/TrenchBroom/lib/upd/src/upd/InstallUpdate.h)

