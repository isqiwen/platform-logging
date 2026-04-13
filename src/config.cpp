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

std::string MakeFieldPath(std::string_view parent, std::string_view field) {
  if (parent.empty()) {
    return std::string(field);
  }

  std::string path(parent);
  path.push_back('.');
  path.append(field);
  return path;
}

bool ReadOptionalString(const nlohmann::json& root, const char* key, const std::string& field_path, std::string* out,
                        std::string* error_message) {
  if (!root.contains(key)) {
    return true;
  }
  if (!root.at(key).is_string()) {
    if (error_message != nullptr) {
      *error_message = "Field '" + field_path + "' must be a string.";
    }
    return false;
  }
  *out = root.at(key).get<std::string>();
  return true;
}

bool ReadOptionalBoolean(const nlohmann::json& root, const char* key, const std::string& field_path, bool* out,
                         std::string* error_message) {
  if (!root.contains(key)) {
    return true;
  }
  if (!root.at(key).is_boolean()) {
    if (error_message != nullptr) {
      *error_message = "Field '" + field_path + "' must be a boolean.";
    }
    return false;
  }
  *out = root.at(key).get<bool>();
  return true;
}

template <typename T>
bool ReadOptionalNonNegativeInteger(const nlohmann::json& root, const char* key, const std::string& field_path, T* out,
                                    std::uint64_t max_value, std::string* error_message) {
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
        *error_message = "Field '" + field_path + "' must be a non-negative integer.";
      }
      return false;
    }
    parsed = static_cast<std::uint64_t>(signed_value);
  } else {
    if (error_message != nullptr) {
      *error_message = "Field '" + field_path + "' must be an integer.";
    }
    return false;
  }

  if (parsed > max_value) {
    if (error_message != nullptr) {
      *error_message = "Field '" + field_path + "' is out of range.";
    }
    return false;
  }

  *out = static_cast<T>(parsed);
  return true;
}

bool ReadOptionalStringArray(const nlohmann::json& root, const char* key, const std::string& field_path,
                             std::vector<std::string>* out, std::string* error_message) {
  if (!root.contains(key)) {
    return true;
  }

  const nlohmann::json& value = root.at(key);
  if (!value.is_array()) {
    if (error_message != nullptr) {
      *error_message = "Field '" + field_path + "' must be an array of strings.";
    }
    return false;
  }

  std::vector<std::string> parsed;
  parsed.reserve(value.size());
  for (std::size_t index = 0; index < value.size(); ++index) {
    const nlohmann::json& entry = value.at(index);
    if (!entry.is_string()) {
      if (error_message != nullptr) {
        *error_message = "Field '" + field_path + "' must be an array of strings.";
      }
      return false;
    }

    const std::string channel = entry.get<std::string>();
    if (channel.empty()) {
      if (error_message != nullptr) {
        *error_message = "Field '" + field_path + "' must not contain empty channel names.";
      }
      return false;
    }
    parsed.push_back(channel);
  }

  *out = std::move(parsed);
  return true;
}

bool ParseLevelField(const nlohmann::json& root, const char* key, const std::string& field_path, Level* out,
                     std::string* error_message) {
  if (!root.contains(key)) {
    return true;
  }
  if (!root.at(key).is_string()) {
    if (error_message != nullptr) {
      *error_message = "Field '" + field_path + "' must be a string.";
    }
    return false;
  }
  if (!TryParseLevel(root.at(key).get<std::string>(), out)) {
    if (error_message != nullptr) {
      *error_message = "Unsupported log level in field '" + field_path + "'.";
    }
    return false;
  }
  return true;
}

bool RejectUnsupportedField(const nlohmann::json& root, const char* key, std::string_view guidance,
                            std::string* error_message) {
  if (!root.contains(key)) {
    return true;
  }
  if (error_message != nullptr) {
    *error_message = "Field '" + std::string(key) + "' is not supported in the current logging schema. " +
                     std::string(guidance);
  }
  return false;
}

bool ParseSinkCommon(const nlohmann::json& root, std::string_view sink_name, SinkConfig* sink,
                     std::string* error_message) {
  const std::string enabled_path = MakeFieldPath(sink_name, "enabled");
  if (!ReadOptionalBoolean(root, "enabled", enabled_path, &sink->enabled, error_message)) {
    return false;
  }

  const std::string level_path = MakeFieldPath(sink_name, "level");
  if (!ParseLevelField(root, "level", level_path, &sink->level, error_message)) {
    return false;
  }

  const std::string pattern_path = MakeFieldPath(sink_name, "pattern");
  if (!ReadOptionalString(root, "pattern", pattern_path, &sink->pattern, error_message)) {
    return false;
  }

  const std::string channels_path = MakeFieldPath(sink_name, "channels");
  if (!ReadOptionalStringArray(root, "channels", channels_path, &sink->channels, error_message)) {
    return false;
  }

  return true;
}

bool ParseConsoleSinkConfig(const nlohmann::json& root, ConsoleSinkConfig* sink, std::string* error_message) {
  if (!ParseSinkCommon(root, "console", sink, error_message)) {
    return false;
  }

  const std::string color_path = MakeFieldPath("console", "console_color");
  return ReadOptionalBoolean(root, "console_color", color_path, &sink->console_color, error_message);
}

bool ParseFileSinkConfig(const nlohmann::json& root, FileSinkConfig* sink, std::string* error_message) {
  if (!ParseSinkCommon(root, "file", sink, error_message)) {
    return false;
  }

  if (!ReadOptionalString(root, "path", MakeFieldPath("file", "path"), &sink->path, error_message)) {
    return false;
  }
  if (!ReadOptionalNonNegativeInteger(root, "rotation_hour", MakeFieldPath("file", "rotation_hour"),
                                      &sink->rotation_hour, 23, error_message)) {
    return false;
  }
  if (!ReadOptionalNonNegativeInteger(root, "rotation_minute", MakeFieldPath("file", "rotation_minute"),
                                      &sink->rotation_minute, 59, error_message)) {
    return false;
  }
  if (!ReadOptionalNonNegativeInteger(root, "max_files", MakeFieldPath("file", "max_files"),
                                      &sink->max_files, std::numeric_limits<std::uint16_t>::max(),
                                      error_message)) {
    return false;
  }

  if (!RejectUnsupportedField(root, "retention_days", "Use 'file.max_files' instead.", error_message)) {
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

  if (!root.is_object()) {
    if (error_message != nullptr) {
      *error_message = std::string("Logging config '") + config_path + "' must be a JSON object.";
    }
    return false;
  }

  try {
    if (!RejectUnsupportedField(root, "level", "Use 'console.level' and/or 'file.level' instead.", error_message) ||
        !RejectUnsupportedField(root, "pattern", "Use 'console.pattern' and/or 'file.pattern' instead.",
                                error_message) ||
        !RejectUnsupportedField(root, "console_color", "Use 'console.console_color' instead.", error_message) ||
        !RejectUnsupportedField(root, "console_pattern", "Use 'console.pattern' instead.", error_message) ||
        !RejectUnsupportedField(root, "file_pattern", "Use 'file.pattern' instead.", error_message)) {
      return false;
    }

    if (!ReadOptionalString(root, "logger_name", "logger_name", &parsed.logger_name, error_message)) {
      return false;
    }
    if (!ReadOptionalBoolean(root, "async", "async", &parsed.async, error_message)) {
      return false;
    }
    if (!ReadOptionalNonNegativeInteger(root, "queue_size", "queue_size", &parsed.queue_size,
                                        std::numeric_limits<std::size_t>::max(), error_message)) {
      return false;
    }
    if (!ReadOptionalNonNegativeInteger(root, "async_worker_count", "async_worker_count",
                                        &parsed.async_worker_count, std::numeric_limits<std::size_t>::max(),
                                        error_message)) {
      return false;
    }

    if (!ParseLevelField(root, "flush_level", "flush_level", &parsed.flush_level, error_message)) {
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

    if (root.contains("console")) {
      const nlohmann::json& console = root.at("console");
      if (!console.is_object()) {
        if (error_message != nullptr) {
          *error_message = "Field 'console' must be an object.";
        }
        return false;
      }
      if (!ParseConsoleSinkConfig(console, &parsed.console, error_message)) {
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
      if (!ParseFileSinkConfig(file, &parsed.file, error_message)) {
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
