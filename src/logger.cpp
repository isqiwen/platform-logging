#include "platform_logging/logger.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
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
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>
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
  mutable std::mutex mutex;
  std::condition_variable no_active_logs;
  Configuration configuration;
  std::vector<spdlog::sink_ptr> sinks;
  std::shared_ptr<spdlog::logger> logger;
  std::shared_ptr<spdlog::details::thread_pool> thread_pool;
  std::atomic<spdlog::logger*> published_logger{nullptr};
  std::atomic<Level> published_level{Level::kOff};
  std::atomic<OutputFormat> published_output_format{OutputFormat::kText};
  std::atomic<std::size_t> active_log_calls{0};
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

bool IsLevelEnabled(Level requested_level, Level configured_level) noexcept {
  return configured_level != Level::kOff &&
         static_cast<int>(requested_level) >= static_cast<int>(configured_level);
}

void PublishLoggerUnlocked(State& state) noexcept {
  state.published_output_format.store(state.configuration.output_format, std::memory_order_release);
  state.published_level.store(state.configuration.level, std::memory_order_release);
  state.published_logger.store(state.logger.get(), std::memory_order_release);
}

void UnpublishLoggerUnlocked(State& state) noexcept {
  state.published_level.store(Level::kOff, std::memory_order_release);
  state.published_logger.store(nullptr, std::memory_order_release);
}

template <typename Lock>
void WaitForActiveLogsUnlocked(State& state, Lock& lock) {
  state.no_active_logs.wait(lock, [&state] { return state.active_log_calls.load(std::memory_order_acquire) == 0; });
}

void ConfigureSinkFormattersUnlocked(State& state) {
  const std::string& pattern =
    state.configuration.output_format == OutputFormat::kJson ? std::string("%v") : state.configuration.pattern;
  for (const auto& sink : state.sinks) {
    sink->set_formatter(std::make_unique<spdlog::pattern_formatter>(pattern, spdlog::pattern_time_type::local));
  }
}

void FlushUnlocked(State& state) noexcept {
  if (state.logger == nullptr) {
    return;
  }

  try {
    state.logger->flush();
  } catch (...) {
    return;
  }

  if (state.configuration.async && state.thread_pool != nullptr) {
    while (state.thread_pool->queue_size() != 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  for (const auto& sink : state.sinks) {
    try {
      sink->flush();
    } catch (...) {
    }
  }
}

void FlushWithPublishingPausedUnlocked(State& state, std::unique_lock<std::mutex>& lock) noexcept {
  if (state.logger == nullptr) {
    return;
  }

  UnpublishLoggerUnlocked(state);
  WaitForActiveLogsUnlocked(state, lock);
  FlushUnlocked(state);
  PublishLoggerUnlocked(state);
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
  if (configuration.async && configuration.async_worker_count == 0) {
    if (error_message != nullptr) {
      *error_message = "async_worker_count must be greater than zero when async logging is enabled.";
    }
    return false;
  }
  if (configuration.async && configuration.async_worker_count > 1000) {
    if (error_message != nullptr) {
      *error_message = "async_worker_count must be in the range [1, 1000] when async logging is enabled.";
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

void ResetStateUnlocked(State& state, std::unique_lock<std::mutex>& lock) noexcept {
  UnpublishLoggerUnlocked(state);
  WaitForActiveLogsUnlocked(state, lock);
  FlushUnlocked(state);

  state.logger.reset();
  state.sinks.clear();
  state.thread_pool.reset();
  state.configured = false;
  state.configuration = Configuration{};
  state.published_output_format.store(OutputFormat::kText, std::memory_order_release);
}

} // namespace

bool Configure(const Configuration& configuration, std::string* error_message) {
  State& state = GlobalState();
  std::unique_lock<std::mutex> lock(state.mutex);
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
    ResetStateUnlocked(state, lock);

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
      if (configuration.console_color) {
        state.sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
      } else {
        state.sinks.push_back(std::make_shared<spdlog::sinks::stdout_sink_mt>());
      }
    }

    if (configuration.async) {
      state.thread_pool =
        std::make_shared<spdlog::details::thread_pool>(configuration.queue_size, configuration.async_worker_count);
    }

    state.configuration = configuration;
    ConfigureSinkFormattersUnlocked(state);
    state.logger = CreateLoggerUnlocked(state);
    state.configured = true;
    PublishLoggerUnlocked(state);
  } catch (const std::exception& error) {
    if (error_message != nullptr) {
      *error_message = std::string("Failed to configure logging: ") + error.what();
    }
    ResetStateUnlocked(state, lock);
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

void Flush() {
  State& state = GlobalState();
  std::unique_lock<std::mutex> lock(state.mutex);
  FlushWithPublishingPausedUnlocked(state, lock);
}

void Shutdown() {
  State& state = GlobalState();
  std::unique_lock<std::mutex> lock(state.mutex);
  ResetStateUnlocked(state, lock);
}

bool IsConfigured() {
  State& state = GlobalState();
  std::lock_guard<std::mutex> lock(state.mutex);
  return state.configured;
}

Configuration CurrentConfiguration() {
  State& state = GlobalState();
  std::lock_guard<std::mutex> lock(state.mutex);
  return state.configuration;
}

bool ShouldLog(Level level) {
  State& state = GlobalState();
  return state.published_logger.load(std::memory_order_acquire) != nullptr &&
         IsLevelEnabled(level, state.published_level.load(std::memory_order_relaxed));
}

bool detail::BeginLog(Level level, detail::LogSite* site) noexcept {
  State& state = GlobalState();
  if (site == nullptr || !IsLevelEnabled(level, state.published_level.load(std::memory_order_acquire))) {
    return false;
  }

  while (true) {
    spdlog::logger* logger = state.published_logger.load(std::memory_order_acquire);
    if (logger == nullptr) {
      return false;
    }

    state.active_log_calls.fetch_add(1, std::memory_order_acq_rel);
    if (logger == state.published_logger.load(std::memory_order_acquire)) {
      site->logger = logger;
      site->output_format = state.published_output_format.load(std::memory_order_relaxed);
      return true;
    }

    if (state.active_log_calls.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      state.no_active_logs.notify_all();
    }
  }
}

void detail::EndLog(detail::LogSite* site) noexcept {
  if (site == nullptr || site->logger == nullptr) {
    return;
  }

  State& state = GlobalState();
  site->logger = nullptr;
  if (state.active_log_calls.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    state.no_active_logs.notify_all();
  }
}

void detail::LogMessage(const detail::LogSite& site, Level level, std::string_view message, const Fields& fields,
                        const std::source_location& location) {
  auto* logger = static_cast<spdlog::logger*>(site.logger);
  if (logger == nullptr) {
    return;
  }

  const spdlog::source_loc source_location{Basename(location.file_name()), static_cast<int>(location.line()),
                                           location.function_name()};

  if (site.output_format == OutputFormat::kJson) {
    const std::string payload = BuildJsonMessage(logger->name(), level, message, fields, location);
    logger->log(
      source_location, ToSpdlogLevel(level), spdlog::string_view_t(payload.data(), payload.size()));
    return;
  }

  if (fields.empty()) {
    logger->log(source_location, ToSpdlogLevel(level), spdlog::string_view_t(message.data(), message.size()));
    return;
  }

  const std::string payload = BuildTextMessage(message, fields);
  logger->log(source_location, ToSpdlogLevel(level), spdlog::string_view_t(payload.data(), payload.size()));
}

void detail::LogFormatted(const detail::LogSite& site, Level level, const Fields* fields,
                          const std::source_location& location, fmt::string_view format_string,
                          fmt::format_args args) {
  auto* logger = static_cast<spdlog::logger*>(site.logger);
  if (logger == nullptr) {
    return;
  }

  fmt::memory_buffer buffer;
  fmt::vformat_to(fmt::appender(buffer), format_string, args);

  const std::string_view formatted_message(buffer.data(), buffer.size());
  if (fields == nullptr) {
    detail::LogMessage(site, level, formatted_message, {}, location);
    return;
  }

  detail::LogMessage(site, level, formatted_message, *fields, location);
}

void Log(Level level, std::string_view message, const Fields& fields, const std::source_location& location) {
  detail::LogSite site;
  if (!detail::BeginLog(level, &site)) {
    return;
  }

  detail::LogMessage(site, level, message, fields, location);
  detail::EndLog(&site);
}

void Log(Level level, std::string_view message, std::initializer_list<Field> fields,
         const std::source_location& location) {
  Log(level, message, Fields(fields), location);
}

} // namespace platform_logging
