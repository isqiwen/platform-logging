#include "test_support.h"

#include <platform_logging/logging.h>

#include <iostream>
#include <regex>
#include <string>

int main() {
  const std::filesystem::path build_root = platform_logging_test::PrepareTestRoot("platform_logging_text_test");

  platform_logging::Configuration configuration;
  configuration.logger_name = "platform_logging_text_test";
  configuration.console.enabled = false;
  configuration.output_format = platform_logging::OutputFormat::kText;
  configuration.file.pattern = "[FILE] [%s:%#] %v";
  configuration.file.channels = {"status"};
  configuration.file.path = (build_root / "platform_logging_text_test.log").string();

  std::string error_message;
  if (!platform_logging::Configure(configuration, &error_message)) {
    std::cerr << error_message << '\n';
    return 1;
  }

  PLATFORM_LOG_INFO("default text message should be filtered");
  PLATFORM_LOG_INFO_KV_CH("status", "text mode slice {}", 7, platform_logging::kv("scan_uid", 42),
                          platform_logging::kv("mode", "demo"));
  platform_logging::Shutdown();

  const std::filesystem::path resolved_log_path =
    platform_logging_test::FindLogFileByPrefix(build_root, "platform_logging_text_test");
  if (resolved_log_path.empty()) {
    std::cerr << "Failed to find text output log file under: " << build_root << '\n';
    return 1;
  }

  const std::string log_text = platform_logging_test::ReadFile(resolved_log_path);
  if (log_text.empty()) {
    std::cerr << "Failed to read text output log file: " << resolved_log_path << '\n';
    return 1;
  }
  if (log_text.find("[FILE]") == std::string::npos) {
    std::cerr << "Missing file pattern prefix\n";
    return 1;
  }
  if (log_text.find("default text message should be filtered") != std::string::npos) {
    std::cerr << "Untagged default-channel info log should be filtered out by file.channels\n";
    return 1;
  }
  if (log_text.find("text_logging_test.cpp:") == std::string::npos) {
    std::cerr << "Missing source location in text output\n";
    return 1;
  }
  if (log_text.find("text mode slice 7 | scan_uid=42 mode=\"demo\"") == std::string::npos) {
    std::cerr << "Missing structured fields after message in text output\n";
    return 1;
  }

  return 0;
}
