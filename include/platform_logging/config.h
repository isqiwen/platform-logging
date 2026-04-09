#pragma once

#include "platform_logging/export.h"

#include <cstddef>
#include <string>

namespace platform_logging {

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

struct FileSinkConfig {
  bool enabled = true;
  std::string path = "logs/platform_logging.log";
  int rotation_hour = 0;
  int rotation_minute = 0;
  int retention_days = 30;
};

struct Configuration {
  std::string logger_name = "platform_logging";
  Level level = Level::kInfo;
  Level flush_level = Level::kWarn;
  bool console = true;
  bool async = false;
  std::size_t queue_size = 8192;
  OutputFormat output_format = OutputFormat::kText;
  std::string pattern = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%P:%t] [%n] [%s:%#] %v";
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
