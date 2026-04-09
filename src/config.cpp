#include "platform_logging/config.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <string_view>

namespace platform_logging {

namespace fs = std::filesystem;

namespace {

bool ReadOptionalString(const nlohmann::json& root, const char* key, std::string* out, std::string* error_message) {
  if (!root.contains(key)) {
    return true;
  }
  if (!root.at(key).is_string()) {
    if (error_message != nullptr) {
      *error_message = std::string("Field '") + key + "' must be a string.";
    }
    return false;
  }
  *out = root.at(key).get<std::string>();
  return true;
}

bool ReadOptionalBoolean(const nlohmann::json& root, const char* key, bool* out, std::string* error_message) {
  if (!root.contains(key)) {
    return true;
  }
  if (!root.at(key).is_boolean()) {
    if (error_message != nullptr) {
      *error_message = std::string("Field '") + key + "' must be a boolean.";
    }
    return false;
  }
  *out = root.at(key).get<bool>();
  return true;
}

template <typename T>
bool ReadOptionalNonNegativeInteger(const nlohmann::json& root, const char* key, T* out, std::uint64_t max_value,
                                    std::string* error_message) {
  if (!root.contains(key)) {
    return true;
  }

  const nlohmann::json& value = root.at(key);
  std::uint64_t parsed = 0;
  if (value.is_number_unsigned()) {
    parsed = value.get<std::uint64_t>();
  } else if (value.is_number_integer()) {
    const std::int64_t signed_value = value.get<std::int64_t>();
    if (signed_value < 0) {
      if (error_message != nullptr) {
        *error_message = std::string("Field '") + key + "' must be a non-negative integer.";
      }
      return false;
    }
    parsed = static_cast<std::uint64_t>(signed_value);
  } else {
    if (error_message != nullptr) {
      *error_message = std::string("Field '") + key + "' must be an integer.";
    }
    return false;
  }

  if (parsed > max_value) {
    if (error_message != nullptr) {
      *error_message = std::string("Field '") + key + "' is out of range.";
    }
    return false;
  }

  *out = static_cast<T>(parsed);
  return true;
}

bool ParseLevelField(const nlohmann::json& root, const char* key, Level* out, std::string* error_message) {
  if (!root.contains(key)) {
    return true;
  }
  if (!root.at(key).is_string()) {
    if (error_message != nullptr) {
      *error_message = std::string("Field '") + key + "' must be a string.";
    }
    return false;
  }
  if (!TryParseLevel(root.at(key).get<std::string>(), out)) {
    if (error_message != nullptr) {
      *error_message = std::string("Unsupported log level in field '") + key + "'.";
    }
    return false;
  }
  return true;
}

} // namespace

std::string ToString(Level level) {
  switch (level) {
    case Level::kTrace:
      return "trace";
    case Level::kDebug:
      return "debug";
    case Level::kInfo:
      return "info";
    case Level::kWarn:
      return "warn";
    case Level::kError:
      return "error";
    case Level::kCritical:
      return "critical";
    case Level::kOff:
      return "off";
  }
  return "info";
}

bool TryParseLevel(const std::string& text, Level* level) {
  if (level == nullptr) {
    return false;
  }
  if (text == "trace") {
    *level = Level::kTrace;
    return true;
  }
  if (text == "debug") {
    *level = Level::kDebug;
    return true;
  }
  if (text == "info") {
    *level = Level::kInfo;
    return true;
  }
  if (text == "warn" || text == "warning") {
    *level = Level::kWarn;
    return true;
  }
  if (text == "error" || text == "err") {
    *level = Level::kError;
    return true;
  }
  if (text == "critical") {
    *level = Level::kCritical;
    return true;
  }
  if (text == "off") {
    *level = Level::kOff;
    return true;
  }
  return false;
}

std::string ToString(OutputFormat output_format) {
  switch (output_format) {
    case OutputFormat::kText:
      return "text";
    case OutputFormat::kJson:
      return "json";
  }
  return "text";
}

bool TryParseOutputFormat(const std::string& text, OutputFormat* output_format) {
  if (output_format == nullptr) {
    return false;
  }
  if (text == "text") {
    *output_format = OutputFormat::kText;
    return true;
  }
  if (text == "json") {
    *output_format = OutputFormat::kJson;
    return true;
  }
  return false;
}

std::string ResolvePathFromBase(const std::string& path, const std::string& base_dir) {
  if (path.empty()) {
    return path;
  }

  const fs::path raw_path(path);
  if (raw_path.is_absolute()) {
    return raw_path.lexically_normal().string();
  }

  const fs::path base(base_dir.empty() ? "." : base_dir);
  return (base / raw_path).lexically_normal().string();
}

bool LoadConfiguration(const std::string& config_path, const std::string& base_dir, Configuration* configuration,
                       std::string* error_message) {
  if (configuration == nullptr) {
    if (error_message != nullptr) {
      *error_message = "Configuration output pointer is null.";
    }
    return false;
  }

  Configuration parsed;

  std::ifstream input(config_path);
  if (!input.is_open()) {
    if (error_message != nullptr) {
      *error_message = std::string("Failed to open logging config '") + config_path + "'.";
    }
    return false;
  }

  nlohmann::json root;
  try {
    input >> root;
  } catch (const std::exception& error) {
    if (error_message != nullptr) {
      *error_message = std::string("Failed to parse logging config '") + config_path + "': " + error.what();
    }
    return false;
  }

  try {
    if (!ReadOptionalString(root, "logger_name", &parsed.logger_name, error_message)) {
      return false;
    }
    if (!ReadOptionalBoolean(root, "console", &parsed.console, error_message)) {
      return false;
    }
    if (!ReadOptionalBoolean(root, "async", &parsed.async, error_message)) {
      return false;
    }
    if (!ReadOptionalNonNegativeInteger(root, "queue_size", &parsed.queue_size, std::numeric_limits<std::size_t>::max(),
                                        error_message)) {
      return false;
    }
    if (!ReadOptionalString(root, "pattern", &parsed.pattern, error_message)) {
      return false;
    }

    if (!ParseLevelField(root, "level", &parsed.level, error_message)) {
      return false;
    }
    if (!ParseLevelField(root, "flush_level", &parsed.flush_level, error_message)) {
      return false;
    }

    if (root.contains("output_format")) {
      if (!root.at("output_format").is_string() ||
          !TryParseOutputFormat(root.at("output_format").get<std::string>(), &parsed.output_format)) {
        if (error_message != nullptr) {
          *error_message = "Field 'output_format' must be 'text' or 'json'.";
        }
        return false;
      }
    }

    if (root.contains("file")) {
      const nlohmann::json& file = root.at("file");
      if (!file.is_object()) {
        if (error_message != nullptr) {
          *error_message = "Field 'file' must be an object.";
        }
        return false;
      }
      if (!ReadOptionalBoolean(file, "enabled", &parsed.file.enabled, error_message)) {
        return false;
      }
      if (!ReadOptionalString(file, "path", &parsed.file.path, error_message)) {
        return false;
      }
      if (!ReadOptionalNonNegativeInteger(file, "rotation_hour", &parsed.file.rotation_hour, 23, error_message)) {
        return false;
      }
      if (!ReadOptionalNonNegativeInteger(file, "rotation_minute", &parsed.file.rotation_minute, 59, error_message)) {
        return false;
      }
      if (!ReadOptionalNonNegativeInteger(file, "retention_days", &parsed.file.retention_days,
                                          std::numeric_limits<std::uint16_t>::max(), error_message)) {
        return false;
      }
      if (!ReadOptionalNonNegativeInteger(file, "max_files", &parsed.file.retention_days,
                                          std::numeric_limits<std::uint16_t>::max(), error_message)) {
        return false;
      }
    } else {
      if (!ReadOptionalString(root, "file_path", &parsed.file.path, error_message)) {
        return false;
      }
      if (!ReadOptionalNonNegativeInteger(root, "rotation_hour", &parsed.file.rotation_hour, 23, error_message)) {
        return false;
      }
      if (!ReadOptionalNonNegativeInteger(root, "rotation_minute", &parsed.file.rotation_minute, 59, error_message)) {
        return false;
      }
      if (!ReadOptionalNonNegativeInteger(root, "retention_days", &parsed.file.retention_days,
                                          std::numeric_limits<std::uint16_t>::max(), error_message)) {
        return false;
      }
      if (!ReadOptionalNonNegativeInteger(root, "max_files", &parsed.file.retention_days,
                                          std::numeric_limits<std::uint16_t>::max(), error_message)) {
        return false;
      }
    }
  } catch (const std::exception& error) {
    if (error_message != nullptr) {
      *error_message = std::string("Failed to read logging config '") + config_path + "': " + error.what();
    }
    return false;
  }

  parsed.file.path = ResolvePathFromBase(parsed.file.path, base_dir);

  *configuration = std::move(parsed);
  if (error_message != nullptr) {
    error_message->clear();
  }
  return true;
}

} // namespace platform_logging
