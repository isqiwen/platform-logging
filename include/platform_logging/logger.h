#pragma once

#include "platform_logging/config.h"
#include "platform_logging/field.h"

#include <initializer_list>
#include <source_location>
#include <string>
#include <string_view>

namespace platform_logging {

PLATFORM_LOGGING_API bool Configure(const Configuration& configuration, std::string* error_message = nullptr);
PLATFORM_LOGGING_API bool Configure(const std::string& config_path, const std::string& base_dir,
                                    std::string* error_message = nullptr);
// Shutdown() waits for in-flight Log() calls to finish, flushes the active logger, and resets the library state.
PLATFORM_LOGGING_API void Shutdown();

[[nodiscard]] PLATFORM_LOGGING_API bool IsConfigured();
[[nodiscard]] PLATFORM_LOGGING_API Configuration CurrentConfiguration();
[[nodiscard]] PLATFORM_LOGGING_API bool ShouldLog(Level level);
PLATFORM_LOGGING_API void Log(Level level, std::string_view message, const Fields& fields = {},
                              const std::source_location& location = std::source_location::current());
PLATFORM_LOGGING_API void Log(Level level, std::string_view message, std::initializer_list<Field> fields,
                              const std::source_location& location = std::source_location::current());

} // namespace platform_logging
