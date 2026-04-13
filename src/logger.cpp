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
#include <spdlog/details/fmt_helper.h>
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

constexpr std::string_view kDefaultChannel = "default";
constexpr std::string_view kChannelEnvelopePrefix = "\x1eplogch:";
constexpr char kChannelEnvelopeSeparator = '\x1f';

std::string_view ToPatternLevelName(spdlog::level::level_enum level) {
  switch (level) {
    case spdlog::level::trace:
      return "TRACE";
    case spdlog::level::debug:
      return "DEBUG";
    case spdlog::level::info:
      return "INFO";
    case spdlog::level::warn:
      return "WARN";
    case spdlog::level::err:
      return "ERROR";
    case spdlog::level::critical:
      return "CRITICAL";
    case spdlog::level::off:
      return "OFF";
    default:
      return "INFO";
  }
}

class UppercaseLevelFormatter final : public spdlog::custom_flag_formatter {
 public:
  void format(const spdlog::details::log_msg& msg, const std::tm&, spdlog::memory_buf_t& dest) override {
    std::string_view level_name = ToPatternLevelName(msg.level);
    if (padinfo_.truncate_ && padinfo_.width_ < level_name.size()) {
      level_name = level_name.substr(0, padinfo_.width_);
    }

    const std::size_t padding = padinfo_.enabled() && padinfo_.width_ > level_name.size()
                                  ? padinfo_.width_ - level_name.size()
                                  : 0;
    const auto append_padding = [&](std::size_t count) {
      for (std::size_t index = 0; index < count; ++index) {
        dest.push_back(' ');
      }
    };

    if (padding != 0 && padinfo_.side_ == spdlog::details::padding_info::pad_side::left) {
      append_padding(padding);
    } else if (padding != 0 && padinfo_.side_ == spdlog::details::padding_info::pad_side::center) {
      const std::size_t left_padding = padding / 2;
      append_padding(left_padding);
    }

    spdlog::details::fmt_helper::append_string_view(
      spdlog::string_view_t(level_name.data(), level_name.size()), dest);

    if (padding != 0 && padinfo_.side_ == spdlog::details::padding_info::pad_side::right) {
      append_padding(padding);
    } else if (padding != 0 && padinfo_.side_ == spdlog::details::padding_info::pad_side::center) {
      const std::size_t left_padding = padding / 2;
      append_padding(padding - left_padding);
    }
  }

  std::unique_ptr<spdlog::custom_flag_formatter> clone() const override {
    return std::make_unique<UppercaseLevelFormatter>();
  }
};

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

struct DecodedPayload {
  std::string_view channel = kDefaultChannel;
  std::string_view message;
  bool tagged = false;
};

class ChannelFilterSink final : public spdlog::sinks::sink {
 public:
  ChannelFilterSink(spdlog::sink_ptr inner, std::vector<std::string> allowed_channels,
                    bool allow_high_priority_without_channel_match)
      : inner_(std::move(inner)),
        allowed_channels_(std::move(allowed_channels)),
        allow_all_channels_(allowed_channels_.empty()),
        allow_high_priority_without_channel_match_(allow_high_priority_without_channel_match) {}

  void log(const spdlog::details::log_msg& msg) override {
    const DecodedPayload decoded = DecodeChannelPayload(msg.payload);
    if (!allow_all_channels_ && !(allow_high_priority_without_channel_match_ && msg.level >= spdlog::level::warn) &&
        !ChannelAllowed(decoded.channel)) {
      return;
    }

    if (!decoded.tagged) {
      inner_->log(msg);
      return;
    }

    spdlog::details::log_msg forwarded(msg);
    forwarded.payload = spdlog::string_view_t(decoded.message.data(), decoded.message.size());
    inner_->log(forwarded);
  }

  void flush() override {
    inner_->flush();
  }

  void set_pattern(const std::string& pattern) override {
    inner_->set_pattern(pattern);
  }

  void set_formatter(std::unique_ptr<spdlog::formatter> sink_formatter) override {
    inner_->set_formatter(std::move(sink_formatter));
  }

 private:
  static DecodedPayload DecodeChannelPayload(spdlog::string_view_t payload) {
    const std::string_view payload_view(payload.data(), payload.size());
    if (!payload_view.starts_with(kChannelEnvelopePrefix)) {
      return {kDefaultChannel, payload_view, false};
    }

    const std::size_t channel_begin = kChannelEnvelopePrefix.size();
    const std::size_t separator = payload_view.find(kChannelEnvelopeSeparator, channel_begin);
    if (separator == std::string_view::npos) {
      return {kDefaultChannel, payload_view, false};
    }

    std::string_view channel = payload_view.substr(channel_begin, separator - channel_begin);
    if (channel.empty()) {
      channel = kDefaultChannel;
    }
    return {channel, payload_view.substr(separator + 1), true};
  }

  bool ChannelAllowed(std::string_view channel) const {
    for (const std::string& allowed_channel : allowed_channels_) {
      if (allowed_channel == channel) {
        return true;
      }
    }
    return false;
  }

  spdlog::sink_ptr inner_;
  std::vector<std::string> allowed_channels_;
  bool allow_all_channels_ = true;
  bool allow_high_priority_without_channel_match_ = false;
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

std::string_view NormalizeChannel(std::string_view channel) {
  return channel.empty() ? kDefaultChannel : channel;
}

std::string EncodeChannelPayload(std::string_view channel, std::string_view payload) {
  const std::string_view normalized_channel = NormalizeChannel(channel);
  std::string encoded;
  encoded.reserve(kChannelEnvelopePrefix.size() + normalized_channel.size() + 1 + payload.size());
  encoded.append(kChannelEnvelopePrefix);
  encoded.append(normalized_channel);
  encoded.push_back(kChannelEnvelopeSeparator);
  encoded.append(payload);
  return encoded;
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

std::string BuildJsonMessage(std::string_view logger_name, Level level, std::string_view channel, std::string_view message,
                             const Fields& fields, const std::source_location& location) {
  nlohmann::json root = nlohmann::json::object();
  root["timestamp"] = CurrentLocalTimestamp();
  root["level"] = ToString(level);
  root["logger"] = logger_name;
  root["channel"] = NormalizeChannel(channel);
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

Level MostVerboseLevel(Level left, Level right) {
  return static_cast<int>(left) <= static_cast<int>(right) ? left : right;
}

Level ComputeEffectiveLevel(const Configuration& configuration) {
  bool has_enabled_sink = false;
  Level effective_level = Level::kOff;

  const auto consider = [&](const SinkConfig& sink) {
    if (!sink.enabled) {
      return;
    }
    if (!has_enabled_sink) {
      effective_level = sink.level;
      has_enabled_sink = true;
      return;
    }
    effective_level = MostVerboseLevel(effective_level, sink.level);
  };

  consider(configuration.file);
  consider(configuration.console);
  return has_enabled_sink ? effective_level : Level::kOff;
}

std::shared_ptr<spdlog::logger> CreateLoggerUnlocked(State& state, Level effective_level) {
  std::shared_ptr<spdlog::logger> logger;
  if (state.configuration.async) {
    logger =
      std::make_shared<spdlog::async_logger>(state.configuration.logger_name, state.sinks.begin(), state.sinks.end(),
                                             state.thread_pool, spdlog::async_overflow_policy::block);
  } else {
    logger = std::make_shared<spdlog::logger>(state.configuration.logger_name, state.sinks.begin(), state.sinks.end());
  }
  logger->set_level(ToSpdlogLevel(effective_level));
  logger->flush_on(ToSpdlogLevel(state.configuration.flush_level));
  return logger;
}

bool IsLevelEnabled(Level requested_level, Level configured_level) noexcept {
  return configured_level != Level::kOff && static_cast<int>(requested_level) >= static_cast<int>(configured_level);
}

void PublishLoggerUnlocked(State& state, Level effective_level) noexcept {
  state.published_output_format.store(state.configuration.output_format, std::memory_order_release);
  state.published_level.store(effective_level, std::memory_order_release);
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
  const auto make_formatter = [](const std::string& pattern) -> std::unique_ptr<spdlog::formatter> {
    auto formatter = std::make_unique<spdlog::pattern_formatter>(spdlog::pattern_time_type::local);
    formatter->add_flag<UppercaseLevelFormatter>('l');
    formatter->set_pattern(pattern);
    return formatter;
  };

  const std::string pattern = state.configuration.output_format == OutputFormat::kJson ? "%v" : state.configuration.file.pattern;

  std::size_t sink_index = 0;
  if (state.configuration.file.enabled && sink_index < state.sinks.size()) {
    state.sinks[sink_index++]->set_formatter(make_formatter(pattern));
  }
  if (state.configuration.console.enabled && sink_index < state.sinks.size()) {
    const std::string console_pattern =
      state.configuration.output_format == OutputFormat::kJson ? "%v" : state.configuration.console.pattern;
    state.sinks[sink_index++]->set_formatter(make_formatter(console_pattern));
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
  PublishLoggerUnlocked(state, ComputeEffectiveLevel(state.configuration));
}

bool ValidateChannels(std::string_view sink_name, const SinkConfig& sink, std::string* error_message) {
  for (const std::string& channel : sink.channels) {
    if (!channel.empty()) {
      continue;
    }
    if (error_message != nullptr) {
      *error_message = std::string(sink_name) + ".channels must not contain empty channel names.";
    }
    return false;
  }
  return true;
}

bool ValidateConfiguration(const Configuration& configuration, std::string* error_message) {
  if (configuration.logger_name.empty()) {
    if (error_message != nullptr) {
      *error_message = "Logger name must not be empty.";
    }
    return false;
  }
  if (!configuration.console.enabled && !configuration.file.enabled) {
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
  if (configuration.file.max_files < 0 ||
      configuration.file.max_files > static_cast<int>(std::numeric_limits<std::uint16_t>::max())) {
    if (error_message != nullptr) {
      *error_message = "max_files must be in the range [0, 65535].";
    }
    return false;
  }
  if (!ValidateChannels("console", configuration.console, error_message) ||
      !ValidateChannels("file", configuration.file, error_message)) {
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

spdlog::sink_ptr MakeConfiguredSink(spdlog::sink_ptr inner_sink, const SinkConfig& sink_configuration,
                                    bool allow_high_priority_without_channel_match) {
  auto sink = std::make_shared<ChannelFilterSink>(std::move(inner_sink), sink_configuration.channels,
                                                  allow_high_priority_without_channel_match);
  sink->set_level(ToSpdlogLevel(sink_configuration.level));
  return sink;
}

void EmitPayload(spdlog::logger* logger, const spdlog::source_loc& source_location, Level level, std::string_view channel,
                 std::string_view payload) {
  if (logger == nullptr) {
    return;
  }

  if (channel.empty()) {
    logger->log(source_location, ToSpdlogLevel(level), spdlog::string_view_t(payload.data(), payload.size()));
    return;
  }

  const std::string encoded_payload = EncodeChannelPayload(channel, payload);
  logger->log(source_location, ToSpdlogLevel(level),
              spdlog::string_view_t(encoded_payload.data(), encoded_payload.size()));
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
      state.sinks.push_back(MakeConfiguredSink(
        std::make_shared<spdlog::sinks::daily_file_sink_mt>(configuration.file.path, configuration.file.rotation_hour,
                                                            configuration.file.rotation_minute, false,
                                                            static_cast<std::uint16_t>(configuration.file.max_files)),
        configuration.file, false));
    }

    if (configuration.console.enabled) {
      spdlog::sink_ptr console_sink;
      if (configuration.console.console_color) {
        console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
      } else {
        console_sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
      }
      state.sinks.push_back(MakeConfiguredSink(std::move(console_sink), configuration.console, true));
    }

    if (configuration.async) {
      state.thread_pool =
        std::make_shared<spdlog::details::thread_pool>(configuration.queue_size, configuration.async_worker_count);
    }

    state.configuration = configuration;
    ConfigureSinkFormattersUnlocked(state);
    const Level effective_level = ComputeEffectiveLevel(configuration);
    state.logger = CreateLoggerUnlocked(state, effective_level);
    state.configured = true;
    PublishLoggerUnlocked(state, effective_level);
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

void detail::LogMessage(const detail::LogSite& site, Level level, std::string_view channel, std::string_view message,
                        const Fields& fields, const std::source_location& location) {
  auto* logger = static_cast<spdlog::logger*>(site.logger);
  if (logger == nullptr) {
    return;
  }

  const spdlog::source_loc source_location{Basename(location.file_name()), static_cast<int>(location.line()),
                                           location.function_name()};

  if (site.output_format == OutputFormat::kJson) {
    const std::string payload = BuildJsonMessage(logger->name(), level, channel, message, fields, location);
    EmitPayload(logger, source_location, level, channel, payload);
    return;
  }

  if (fields.empty()) {
    EmitPayload(logger, source_location, level, channel, message);
    return;
  }

  const std::string payload = BuildTextMessage(message, fields);
  EmitPayload(logger, source_location, level, channel, payload);
}

void detail::LogFormatted(const detail::LogSite& site, Level level, std::string_view channel, const Fields* fields,
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
    detail::LogMessage(site, level, channel, formatted_message, {}, location);
    return;
  }

  detail::LogMessage(site, level, channel, formatted_message, *fields, location);
}

void Log(Level level, std::string_view message, const Fields& fields, const std::source_location& location) {
  detail::LogSite site;
  if (!detail::BeginLog(level, &site)) {
    return;
  }

  detail::LogMessage(site, level, {}, message, fields, location);
  detail::EndLog(&site);
}

void Log(Level level, std::string_view message, std::initializer_list<Field> fields,
         const std::source_location& location) {
  Log(level, message, Fields(fields), location);
}

void LogChannel(Level level, std::string_view channel, std::string_view message, const Fields& fields,
                const std::source_location& location) {
  detail::LogSite site;
  if (!detail::BeginLog(level, &site)) {
    return;
  }

  detail::LogMessage(site, level, channel, message, fields, location);
  detail::EndLog(&site);
}

void LogChannel(Level level, std::string_view channel, std::string_view message, std::initializer_list<Field> fields,
                const std::source_location& location) {
  LogChannel(level, channel, message, Fields(fields), location);
}

} // namespace platform_logging
