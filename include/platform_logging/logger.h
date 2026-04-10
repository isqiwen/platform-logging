#pragma once

#include "platform_logging/config.h"
#include "platform_logging/field.h"

#include <spdlog/fmt/fmt.h>

#include <initializer_list>
#include <source_location>
#include <string>
#include <string_view>

namespace platform_logging {

PLATFORM_LOGGING_API bool Configure(const Configuration& configuration, std::string* error_message = nullptr);
PLATFORM_LOGGING_API bool Configure(const std::string& config_path, const std::string& base_dir,
                                    std::string* error_message = nullptr);
// Flush() blocks new Log() calls briefly, drains pending async work, and flushes all active sinks.
PLATFORM_LOGGING_API void Flush();
// Shutdown() waits for in-flight Log() calls to finish, flushes the active logger, and resets the library state.
PLATFORM_LOGGING_API void Shutdown();

[[nodiscard]] PLATFORM_LOGGING_API bool IsConfigured();
[[nodiscard]] PLATFORM_LOGGING_API Configuration CurrentConfiguration();
[[nodiscard]] PLATFORM_LOGGING_API bool ShouldLog(Level level);
PLATFORM_LOGGING_API void Log(Level level, std::string_view message, const Fields& fields = {},
                              const std::source_location& location = std::source_location::current());
PLATFORM_LOGGING_API void Log(Level level, std::string_view message, std::initializer_list<Field> fields,
                              const std::source_location& location = std::source_location::current());
PLATFORM_LOGGING_API void LogChannel(Level level, std::string_view channel, std::string_view message,
                                     const Fields& fields = {},
                                     const std::source_location& location = std::source_location::current());
PLATFORM_LOGGING_API void LogChannel(Level level, std::string_view channel, std::string_view message,
                                     std::initializer_list<Field> fields,
                                     const std::source_location& location = std::source_location::current());

namespace detail {

struct LogSite {
  void* logger = nullptr;
  OutputFormat output_format = OutputFormat::kText;
};

[[nodiscard]] PLATFORM_LOGGING_API bool BeginLog(Level level, LogSite* site) noexcept;
PLATFORM_LOGGING_API void EndLog(LogSite* site) noexcept;
PLATFORM_LOGGING_API void LogMessage(const LogSite& site, Level level, std::string_view channel,
                                     std::string_view message, const Fields& fields,
                                     const std::source_location& location);
PLATFORM_LOGGING_API void LogFormatted(const LogSite& site, Level level, std::string_view channel,
                                       const Fields* fields,
                                       const std::source_location& location, fmt::string_view format_string,
                                       fmt::format_args args);

} // namespace detail

} // namespace platform_logging
