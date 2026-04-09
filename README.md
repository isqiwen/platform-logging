# platform_logging

`platform_logging` is a small C++20 logging library built on top of `spdlog` and `nlohmann_json`.

It provides:

- text and JSON output
- file and console sinks
- daily file rotation
- synchronous and asynchronous logging
- structured fields via `kv(...)`
- a macro-first logging API

## Requirements

- CMake 3.25+
- a C++20 compiler
- Conan 2

## Build

Detect a Conan profile once:

```bash
conan profile detect --force
```

Configure dependencies and generate the toolchain:

```bash
conan install . \
  --output-folder=out/build/linux-release \
  --build=missing \
  -s build_type=Release \
  -s compiler.cppstd=20
```

Configure, build, and test:

```bash
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
```

For a debug build:

```bash
conan install . \
  --output-folder=out/build/linux-debug \
  --build=missing \
  -s build_type=Debug \
  -s compiler.cppstd=20

cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

To build a static library instead of the default shared library:

```bash
conan install . \
  --output-folder=out/build/linux-release \
  --build=missing \
  -s build_type=Release \
  -s compiler.cppstd=20 \
  -o shared=False
```

## Install

```bash
cmake --build --preset linux-release-install
```

The package exports the CMake target:

```cmake
platform_logging::platform_logging
```

## Use In Another CMake Project

```cmake
find_package(platform_logging CONFIG REQUIRED)

add_executable(example main.cpp)
target_link_libraries(example PRIVATE platform_logging::platform_logging)
```

## Quick Start

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

## Logging Macros

Formatted logs:

```cpp
PLATFORM_LOG_INFO("service started");
PLATFORM_LOG_ERROR("open file failed: {}", file_path);
```

Formatted logs with structured fields:

```cpp
PLATFORM_LOG_INFO_KV(
    "slice {} completed",
    12,
    platform_logging::kv("scan_uid", 1001),
    platform_logging::kv("channel", 3));
```

For `PLATFORM_LOG_*_KV(...)`:

- normal format arguments come first
- trailing `kv(...)` arguments are emitted as structured fields

## Runtime Contract

- call `Configure(...)` once during process startup
- call `Shutdown()` when the host process is done with logging
- logs are dropped until `Configure(...)` succeeds
- a second successful `Configure(...)` call is not allowed
- to reload configuration, call `Shutdown()` first and then `Configure(...)` again
- shared libraries and plugins should log through the macros, but should not call `Configure(...)` or `Shutdown()` themselves

`Shutdown()` is synchronous: it waits for in-flight log calls to finish, flushes the active logger, and then resets the library state.

For processes that load multiple shared libraries or plugins, the default shared-library build is recommended so the whole process can share one logging state.

## Configuration

An example configuration file is provided at:

```text
config/logging_config.json
```

Default behavior:

- `output_format` is `text`
- text and JSON timestamps are emitted as plain local time
- file logs rotate daily
- default rotation time is local `00:00`
- default retention is `30` days
