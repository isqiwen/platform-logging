#include "test_support.h"

#include <platform_logging/logging.h>

#include <iostream>
#include <string>

int main() {
  const std::filesystem::path build_root = platform_logging_test::PrepareTestRoot("platform_logging_async_test");
  constexpr int kMessageCount = 400;

  platform_logging::Configuration configuration;
  configuration.logger_name = "platform_logging_async_test";
  configuration.console = false;
  configuration.async = true;
  configuration.queue_size = 1024;
  configuration.output_format = platform_logging::OutputFormat::kJson;
  configuration.file.path = (build_root / "platform_logging_async_test.log").string();

  std::string error_message;
  if (!platform_logging::Configure(configuration, &error_message)) {
    std::cerr << error_message << '\n';
    return 1;
  }

  for (int index = 0; index < kMessageCount; ++index) {
    PLATFORM_LOG_INFO_KV("async message {}", index, platform_logging::kv("index", index));
  }
  platform_logging::Shutdown();

  const std::filesystem::path resolved_log_path =
    platform_logging_test::FindLogFileByPrefix(build_root, "platform_logging_async_test");
  if (resolved_log_path.empty()) {
    std::cerr << "Failed to find async output log file under: " << build_root << '\n';
    return 1;
  }

  const std::string log_text = platform_logging_test::ReadFile(resolved_log_path);
  if (log_text.empty()) {
    std::cerr << "Failed to read async output log file: " << resolved_log_path << '\n';
    return 1;
  }

  const std::size_t message_count = platform_logging_test::CountSubstring(log_text, "\"message\":\"async message ");
  if (message_count != static_cast<std::size_t>(kMessageCount)) {
    std::cerr << "Unexpected async message count: " << message_count << '\n';
    return 1;
  }
  if (log_text.find("\"index\":399") == std::string::npos) {
    std::cerr << "Missing final async message field\n";
    return 1;
  }
  if (log_text.find("\"logger\":\"platform_logging_async_test\"") == std::string::npos) {
    std::cerr << "Missing async logger name\n";
    return 1;
  }

  return 0;
}
