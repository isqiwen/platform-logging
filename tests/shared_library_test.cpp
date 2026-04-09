#include "test_support.h"
#include "shared_logging_test_api.h"

#include <platform_logging/logging.h>

#include <iostream>
#include <string>

extern "C" PLATFORM_LOGGING_SHARED_TEST_API void platform_logging_shared_lib_a_log();
extern "C" PLATFORM_LOGGING_SHARED_TEST_API void platform_logging_shared_lib_b_log();

int main() {
  const std::filesystem::path build_root =
    platform_logging_test::PrepareTestRoot("platform_logging_shared_library_test");

  platform_logging::Configuration configuration;
  configuration.logger_name = "platform_logging_shared_library_test";
  configuration.console = false;
  configuration.output_format = platform_logging::OutputFormat::kJson;
  configuration.file.path = (build_root / "platform_logging_shared_library_test.log").string();

  std::string error_message;
  if (!platform_logging::Configure(configuration, &error_message)) {
    std::cerr << error_message << '\n';
    return 1;
  }

  PLATFORM_LOG_INFO("host entry");
  platform_logging_shared_lib_a_log();
  platform_logging_shared_lib_b_log();
  platform_logging::Shutdown();

  const std::filesystem::path resolved_log_path =
    platform_logging_test::FindLogFileByPrefix(build_root, "platform_logging_shared_library_test");
  if (resolved_log_path.empty()) {
    std::cerr << "Failed to find shared library output log file under: " << build_root << '\n';
    return 1;
  }

  const std::string log_text = platform_logging_test::ReadFile(resolved_log_path);
  if (log_text.empty()) {
    std::cerr << "Failed to read shared library output log file: " << resolved_log_path << '\n';
    return 1;
  }
  if (log_text.find("\"message\":\"host entry\"") == std::string::npos) {
    std::cerr << "Missing host log entry\n";
    return 1;
  }
  if (log_text.find("\"message\":\"shared library a entry\"") == std::string::npos) {
    std::cerr << "Missing shared library a log entry\n";
    return 1;
  }
  if (log_text.find("\"message\":\"shared library b entry\"") == std::string::npos) {
    std::cerr << "Missing shared library b log entry\n";
    return 1;
  }
  if (platform_logging_test::CountSubstring(log_text, "\"logger\":\"platform_logging_shared_library_test\"") != 3) {
    std::cerr << "Shared libraries should use the host-configured logger instance\n";
    return 1;
  }

  return 0;
}
