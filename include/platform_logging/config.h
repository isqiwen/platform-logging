#pragma once

#include "platform_logging/export.h"

#include <cstddef>
#include <string>
#include <vector>

namespace platform_logging {

inline constexpr const char* kDefaultTextPattern = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%P:%t] [%n] [%s:%#] %v";

enum class Level {
  kTrace,
  kDebug,
  kInfo,
  kWarn,
  kError,
  kCritical,
  kOff,
};

enum class OutputFormat {
  kText,
  kJson,
};

struct SinkConfig {
  bool enabled = true;
  Level level = Level::kInfo;
  std::string pattern = kDefaultTextPattern;
  std::vector<std::string> channels = {};
};

struct FileSinkConfig : SinkConfig {
  std::string path = "logs/platform_logging.log";
  int rotation_hour = 0;
  int rotation_minute = 0;
  int retention_days = 30;
};

struct ConsoleSinkConfig : SinkConfig {
  bool console_color = true;
};

struct Configuration {
  std::string logger_name = "platform_logging";
  Level flush_level = Level::kWarn;
  bool async = false;
  std::size_t queue_size = 8192;
  std::size_t async_worker_count = 1;
  OutputFormat output_format = OutputFormat::kText;
  ConsoleSinkConfig console = {};
  FileSinkConfig file = {};
};

PLATFORM_LOGGING_API std::string ToString(Level level);
PLATFORM_LOGGING_API bool TryParseLevel(const std::string& text, Level* level);

PLATFORM_LOGGING_API std::string ToString(OutputFormat output_format);
PLATFORM_LOGGING_API bool TryParseOutputFormat(const std::string& text, OutputFormat* output_format);

PLATFORM_LOGGING_API std::string ResolvePathFromBase(const std::string& path, const std::string& base_dir);
PLATFORM_LOGGING_API bool LoadConfiguration(const std::string& config_path, const std::string& base_dir,
                                            Configuration* configuration, std::string* error_message);

} // namespace platform_logging
