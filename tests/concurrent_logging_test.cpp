#include "test_support.h"

#include <platform_logging/logging.h>

#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main() {
  const std::filesystem::path build_root = platform_logging_test::PrepareTestRoot("platform_logging_concurrent_test");
  constexpr int kThreadCount = 4;
  constexpr int kMessagesPerThread = 150;

  platform_logging::Configuration configuration;
  configuration.logger_name = "platform_logging_concurrent_test";
  configuration.console = false;
  configuration.output_format = platform_logging::OutputFormat::kJson;
  configuration.file.path = (build_root / "platform_logging_concurrent_test.log").string();

  std::string error_message;
  if (!platform_logging::Configure(configuration, &error_message)) {
    std::cerr << error_message << '\n';
    return 1;
  }

  std::vector<std::thread> workers;
  workers.reserve(kThreadCount);
  for (int thread_index = 0; thread_index < kThreadCount; ++thread_index) {
    workers.emplace_back([thread_index]() {
      for (int message_index = 0; message_index < kMessagesPerThread; ++message_index) {
        PLATFORM_LOG_INFO("concurrent worker {} iteration {}", thread_index, message_index);
      }
    });
  }

  for (std::thread& worker : workers) {
    worker.join();
  }
  platform_logging::Shutdown();

  const std::filesystem::path resolved_log_path =
    platform_logging_test::FindLogFileByPrefix(build_root, "platform_logging_concurrent_test");
  if (resolved_log_path.empty()) {
    std::cerr << "Failed to find concurrent output log file under: " << build_root << '\n';
    return 1;
  }

  const std::string log_text = platform_logging_test::ReadFile(resolved_log_path);
  if (log_text.empty()) {
    std::cerr << "Failed to read concurrent output log file: " << resolved_log_path << '\n';
    return 1;
  }

  const std::size_t message_count = platform_logging_test::CountSubstring(log_text, "\"message\":\"concurrent worker ");
  const std::size_t expected_count = static_cast<std::size_t>(kThreadCount * kMessagesPerThread);
  if (message_count != expected_count) {
    std::cerr << "Unexpected concurrent message count: " << message_count << '\n';
    return 1;
  }
  if (log_text.find("\"message\":\"concurrent worker 0 iteration 0\"") == std::string::npos) {
    std::cerr << "Missing first concurrent log entry\n";
    return 1;
  }
  if (log_text.find("\"message\":\"concurrent worker 3 iteration 149\"") == std::string::npos) {
    std::cerr << "Missing final concurrent log entry\n";
    return 1;
  }

  return 0;
}
