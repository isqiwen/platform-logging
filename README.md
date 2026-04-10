# platform_logging

`platform_logging` 是一个独立的现代 C++ 日志库仓库，基于 `spdlog + nlohmann_json`，通过同级 `thirdparty_toolchain` 发现依赖，并使用同级 `cmake_project_kit` 组织构建、安装和 smoke test。

## 工作区约定

默认工作区目录如下：

```text
<workspace>/
  cmake_project_kit/
  thirdparty_toolchain/
  platform_logging/
```

并且 `thirdparty_toolchain/linux_artifacts/...` 已准备好。

## 编译

Linux:

```bash
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
```

默认构建动态库。
如果需要静态库，可在配置时增加：

```bash
-DPLATFORM_LOGGING_BUILD_SHARED=OFF
```

安装：

```bash
cmake --build --preset linux-release-install
```

## 接入到其它工程

安装后，在下游工程中这样使用：

```cmake
find_package(platform_logging CONFIG REQUIRED)

add_executable(example main.cpp)
target_link_libraries(example PRIVATE platform_logging::platform_logging)
```

如果在同一工作区内联调，也可以先执行：

```bash
cmake --build --preset linux-release-install
```

然后把下面这个目录加入下游工程的 `CMAKE_PREFIX_PATH`：

```text
<workspace>/platform_logging/out/install/linux-release
```

## 快速开始

```cpp
#include <platform_logging/logging.h>

int main() {
  std::string error_message;
  if (!platform_logging::Configure("config/logging_config.json", ".", &error_message)) {
    return 1;
  }

  PLATFORM_LOG_INFO("service started");
  PLATFORM_LOG_WARN_KV(
      "regrid completed for slice {}",
      12,
      platform_logging::kv("scan_uid", 1001),
      platform_logging::kv("channel", 3));

  platform_logging::Shutdown();
  return 0;
}
```

## 日志宏

普通日志：

```cpp
PLATFORM_LOG_INFO("service started");
PLATFORM_LOG_ERROR("open file failed: {}", file_path);
```

带 channel 的日志：

```cpp
PLATFORM_LOG_INFO_CH("status", "service started");
PLATFORM_LOG_INFO_KV_CH(
    "perf",
    "slice {} completed",
    12,
    platform_logging::kv("scan_uid", 1001));
```

带结构化字段的日志：

```cpp
PLATFORM_LOG_INFO_KV(
    "slice {} completed",
    12,
    platform_logging::kv("scan_uid", 1001),
    platform_logging::kv("channel", 3));
```

`PLATFORM_LOG_*_KV(...)` 的参数约定是：

- 前面是格式化参数
- 末尾连续的 `kv(...)` 会作为结构化字段写入日志

## 使用约定

- 在进程启动阶段调用一次 `Configure(...)`
- 在进程退出前按需调用 `Shutdown()`
- 未调用 `Configure(...)` 前，日志宏不会输出内容
- 已经配置成功后再次调用 `Configure(...)` 会返回失败
- 如果需要重新加载配置，先调用 `Shutdown()`，再重新 `Configure(...)`
- 动态库、静态库和插件只负责写日志，不应各自调用 `Configure(...)` 或 `Shutdown()`

`Shutdown()` 是同步的：它会等待正在执行的日志调用完成，并刷新当前 logger 后再重置内部状态。

对于会加载多个 `.so` / 插件的进程，建议保持默认的动态库构建方式，这样更容易让整个进程共享同一份日志状态。

## 配置文件

默认示例配置位于：

```text
config/logging_config.json
```

当前默认行为：

- `output_format` 默认是 `text`
- 文本和 JSON 时间戳默认使用直观的本地时间
- `console.console_color` 默认是 `true`，可在配置里关闭彩色 console 输出
- `async_worker_count` 默认是 `1`
- `console.level` 和 `file.level` 分别控制各自 sink 的最低日志级别
- `console.pattern` 和 `file.pattern` 分别控制各自 sink 的文本格式
- `console.channels` 和 `file.channels` 控制各自 sink 接收哪些 channel；空数组表示接收全部 channel
- 未显式指定 channel 的日志会落到默认 channel `default`
- 文件日志按天滚动
- 默认每天本地时间 `00:00` 滚动
- 默认保留 `30` 天

默认配置结构：

```json
{
  "logger_name": "platform_logging",
  "flush_level": "warn",
  "async": false,
  "queue_size": 8192,
  "async_worker_count": 1,
  "output_format": "text",
  "console": {
    "enabled": true,
    "level": "info",
    "console_color": false,
    "pattern": "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%P:%t] [%n] [%s:%#] %v",
    "channels": []
  },
  "file": {
    "enabled": true,
    "level": "info",
    "path": "logs/platform_logging.log",
    "retention_days": 30,
    "pattern": "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%P:%t] [%n] [%s:%#] %v",
    "channels": []
  }
}
```

`console`、`file` 两个 sink 的关系：

- 每个 sink 都有自己独立的 `enabled / level / pattern / channels`
- `Configure(...)` 内部会自动推导一个最详细的有效级别，作为前置快速过滤
- `output_format = "json"` 时，文本 `pattern` 会被忽略，sink 统一输出 `%v`
- 如果 console 配了 `channels` 过滤，`warn/error/critical` 仍然会输出到 console，避免关键日志被 channel 规则误伤
