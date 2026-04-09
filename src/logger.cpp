#include "platform_logging/logger.h"

#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <mutex>
#include <nlohmann/json.hpp>
#include <spdlog/async.h>
#include <spdlog/async_logger.h>
#include <spdlog/details/thread_pool.h>
#include <spdlog/logger.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <shared_mutex>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <process.h>
#else
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace platform_logging {

namespace fs = std::filesystem;

namespace {

struct State {
  mutable std::shared_mutex mutex;
  Configuration configuration;
  std::vector<spdlog::sink_ptr> sinks;
  std::shared_ptr<spdlog::logger> logger;
  std::shared_ptr<spdlog::details::thread_pool> thread_pool;
  bool configured = false;
};

State& GlobalState() {
  static State state;
  return state;
}

spdlog::level::level_enum ToSpdlogLevel(Level level) {
  switch (level) {
    case Level::kTrace:
      return spdlog::level::trace;
    case Level::kDebug:
      return spdlog::level::debug;
    case Level::kInfo:
      return spdlog::level::info;
    case Level::kWarn:
      return spdlog::level::warn;
    case Level::kError:
      return spdlog::level::err;
    case Level::kCritical:
      return spdlog::level::critical;
    case Level::kOff:
      return spdlog::level::off;
  }
  return spdlog::level::info;
}

const char* Basename(const char* path) {
  if (path == nullptr) {
    return "";
  }
  const char* last_forward = std::strrchr(path, '/');
  const char* last_backward = std::strrchr(path, '\\');
  const char* last = last_forward == nullptr    ? last_backward
                     : last_backward == nullptr ? last_forward
                                                : (last_forward > last_backward ? last_forward : last_backward);
  return last == nullptr ? path : last + 1;
}

nlohmann::json FieldValueToJson(const FieldValue& value) {
  return std::visit(
    [](const auto& inner) -> nlohmann::json {
      return inner;
    },
    value);
}

std::string FieldValueToText(const FieldValue& value) {
  return std::visit(
    [](const auto& inner) -> std::string {
      using T = std::decay_t<decltype(inner)>;
      if constexpr (std::is_same_v<T, std::nullptr_t>) {
        return "null";
      } else if constexpr (std::is_same_v<T, bool>) {
        return inner ? "true" : "false";
      } else if constexpr (std::is_same_v<T, std::string>) {
        return nlohmann::json(inner).dump();
      } else {
        return nlohmann::json(inner).dump();
      }
    },
    value);
}

std::string BuildTextMessage(std::string_view message, const Fields& fields) {
  std::string payload(message);
  if (fields.empty()) {
    return payload;
  }

  payload += " |";
  for (const Field& field : fields) {
    payload += ' ';
    payload += field.key;
    payload += '=';
    payload += FieldValueToText(field.value);
  }
  return payload;
}

std::string CurrentLocalTimestamp() {
  using clock = std::chrono::system_clock;
  const auto now = clock::now();
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
  const std::time_t raw_time = clock::to_time_t(now);

  std::tm local_time = {};
#ifdef _WIN32
  localtime_s(&local_time, &raw_time);
#else
  localtime_r(&raw_time, &local_time);
#endif

  std::ostringstream stream;
  stream << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
         << milliseconds.count();
  return stream.str();
}

int CurrentProcessId() {
#ifdef _WIN32
  return _getpid();
#else
  return getpid();
#endif
}

std::uint64_t CurrentThreadId() {
#ifdef _WIN32
  return static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#else
  return static_cast<std::uint64_t>(syscall(SYS_gettid));
#endif
}

std::string BuildJsonMessage(std::string_view logger_name, Level level, std::string_view message, const Fields& fields,
                             const std::source_location& location) {
  nlohmann::json root = nlohmann::json::object();
  root["timestamp"] = CurrentLocalTimestamp();
  root["level"] = ToString(level);
  root["logger"] = logger_name;
  root["message"] = message;
  root["process_id"] = CurrentProcessId();
  root["thread_id"] = CurrentThreadId();

  nlohmann::json source = nlohmann::json::object();
  source["file"] = Basename(location.file_name());
  source["line"] = location.line();
  source["function"] = location.function_name();
  root["source"] = std::move(source);

  nlohmann::json field_object = nlohmann::json::object();
  for (const Field& field : fields) {
    field_object[field.key] = FieldValueToJson(field.value);
  }
  root["fields"] = std::move(field_object);
  return root.dump();
}

std::shared_ptr<spdlog::logger> CreateLoggerUnlocked(State& state) {
  std::shared_ptr<spdlog::logger> logger;
  if (state.configuration.async) {
    logger =
      std::make_shared<spdlog::async_logger>(state.configuration.logger_name, state.sinks.begin(), state.sinks.end(),
                                             state.thread_pool, spdlog::async_overflow_policy::block);
  } else {
    logger = std::make_shared<spdlog::logger>(state.configuration.logger_name, state.sinks.begin(), state.sinks.end());
  }
  logger->set_level(ToSpdlogLevel(state.configuration.level));
  logger->flush_on(ToSpdlogLevel(state.configuration.flush_level));
  return logger;
}

void ConfigureSinkFormattersUnlocked(State& state) {
  const std::string& pattern =
    state.configuration.output_format == OutputFormat::kJson ? std::string("%v") : state.configuration.pattern;
  for (const auto& sink : state.sinks) {
    sink->set_formatter(std::make_unique<spdlog::pattern_formatter>(pattern, spdlog::pattern_time_type::local));
  }
}

bool ValidateConfiguration(const Configuration& configuration, std::string* error_message) {
  if (configuration.logger_name.empty()) {
    if (error_message != nullptr) {
      *error_message = "Logger name must not be empty.";
    }
    return false;
  }
  if (!configuration.console && !configuration.file.enabled) {
    if (error_message != nullptr) {
      *error_message = "Logging configuration produced zero sinks.";
    }
    return false;
  }
  if (configuration.async && configuration.queue_size == 0) {
    if (error_message != nullptr) {
      *error_message = "queue_size must be greater than zero when async logging is enabled.";
    }
    return false;
  }
  if (configuration.file.enabled && configuration.file.path.empty()) {
    if (error_message != nullptr) {
      *error_message = "File sink path must not be empty when file logging is enabled.";
    }
    return false;
  }
  if (configuration.file.rotation_hour < 0 || configuration.file.rotation_hour > 23) {
    if (error_message != nullptr) {
      *error_message = "rotation_hour must be in the range [0, 23].";
    }
    return false;
  }
  if (configuration.file.rotation_minute < 0 || configuration.file.rotation_minute > 59) {
    if (error_message != nullptr) {
      *error_message = "rotation_minute must be in the range [0, 59].";
    }
    return false;
  }
  if (configuration.file.retention_days < 0 ||
      configuration.file.retention_days > static_cast<int>(std::numeric_limits<std::uint16_t>::max())) {
    if (error_message != nullptr) {
      *error_message = "retention_days must be in the range [0, 65535].";
    }
    return false;
  }
  return true;
}

void ResetStateUnlocked(State& state) noexcept {
  if (state.logger != nullptr) {
    try {
      state.logger->flush();
    } catch (...) {}
  }

  state.logger.reset();
  state.sinks.clear();
  state.thread_pool.reset();
  state.configured = false;
  state.configuration = Configuration{};
}

} // namespace

bool Configure(const Configuration& configuration, std::string* error_message) {
  State& state = GlobalState();
  std::unique_lock<std::shared_mutex> lock(state.mutex);
  if (state.configured) {
    if (error_message != nullptr) {
      *error_message =
        "Logging is already configured. Only the host process should call Configure(); call Shutdown() before "
        "reconfiguring.";
    }
    return false;
  }
  if (!ValidateConfiguration(configuration, error_message)) {
    return false;
  }

  try {
    ResetStateUnlocked(state);

    if (configuration.file.enabled) {
      const fs::path file_path(configuration.file.path);
      if (!file_path.parent_path().empty()) {
        fs::create_directories(file_path.parent_path());
      }
      state.sinks.push_back(std::make_shared<spdlog::sinks::daily_file_sink_mt>(
        configuration.file.path, configuration.file.rotation_hour, configuration.file.rotation_minute, false,
        static_cast<std::uint16_t>(configuration.file.retention_days)));
    }

    if (configuration.console) {
      state.sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }

    if (configuration.async) {
      state.thread_pool = std::make_shared<spdlog::details::thread_pool>(configuration.queue_size, 1);
    }

    state.configuration = configuration;
    ConfigureSinkFormattersUnlocked(state);
    state.logger = CreateLoggerUnlocked(state);
    state.configured = true;
  } catch (const std::exception& error) {
    if (error_message != nullptr) {
      *error_message = std::string("Failed to configure logging: ") + error.what();
    }
    ResetStateUnlocked(state);
    return false;
  }

  if (error_message != nullptr) {
    error_message->clear();
  }
  return true;
}

bool Configure(const std::string& config_path, const std::string& base_dir, std::string* error_message) {
  Configuration configuration;
  if (!LoadConfiguration(config_path, base_dir, &configuration, error_message)) {
    return false;
  }
  return Configure(configuration, error_message);
}

void Shutdown() {
  State& state = GlobalState();
  std::unique_lock<std::shared_mutex> lock(state.mutex);
  ResetStateUnlocked(state);
}

bool IsConfigured() {
  State& state = GlobalState();
  std::shared_lock<std::shared_mutex> lock(state.mutex);
  return state.configured;
}

Configuration CurrentConfiguration() {
  State& state = GlobalState();
  std::shared_lock<std::shared_mutex> lock(state.mutex);
  return state.configuration;
}

bool ShouldLog(Level level) {
  State& state = GlobalState();
  std::shared_lock<std::shared_mutex> lock(state.mutex);
  return state.logger != nullptr && state.logger->should_log(ToSpdlogLevel(level));
}

void Log(Level level, std::string_view message, const Fields& fields, const std::source_location& location) {
  State& state = GlobalState();
  std::shared_lock<std::shared_mutex> lock(state.mutex);
  if (state.logger == nullptr || !state.logger->should_log(ToSpdlogLevel(level))) {
    return;
  }

  const std::string payload = state.configuration.output_format == OutputFormat::kJson
                                ? BuildJsonMessage(state.logger->name(), level, message, fields, location)
                                : BuildTextMessage(message, fields);

  const spdlog::source_loc source_location{Basename(location.file_name()), static_cast<int>(location.line()),
                                           location.function_name()};
  state.logger->log(source_location, ToSpdlogLevel(level), "{}", payload);
}

void Log(Level level, std::string_view message, std::initializer_list<Field> fields,
         const std::source_location& location) {
  Log(level, message, Fields(fields), location);
}

} // namespace platform_logging
