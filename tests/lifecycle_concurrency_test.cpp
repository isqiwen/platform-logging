#include "test_support.h"

#include <platform_logging/logging.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main() {
  const std::filesystem::path build_root = platform_logging_test::PrepareTestRoot("platform_logging_lifecycle_test");
  constexpr int kWorkerCount = 3;
  constexpr int kPhaseCount = 5;

  platform_logging::Configuration configuration;
  configuration.logger_name = "platform_logging_lifecycle_test";
  configuration.console.enabled = false;
  configuration.output_format = platform_logging::OutputFormat::kJson;
  configuration.file.path = (build_root / "platform_logging_lifecycle_test.log").string();

  std::atomic<bool> stop_workers = false;
  std::atomic<int> worker_attempts = 0;
  std::vector<std::thread> workers;
  workers.reserve(kWorkerCount);
  for (int worker_id = 0; worker_id < kWorkerCount; ++worker_id) {
    workers.emplace_back([worker_id, &stop_workers, &worker_attempts]() {
      int iteration = 0;
      while (!stop_workers.load(std::memory_order_relaxed)) {
        PLATFORM_LOG_INFO("race worker {} iteration {}", worker_id, iteration++);
        worker_attempts.fetch_add(1, std::memory_order_relaxed);
        if ((iteration % 32) == 0) {
          std::this_thread::yield();
        }
      }
    });
  }

  std::string error_message;
  for (int phase = 0; phase < kPhaseCount; ++phase) {
    if (!platform_logging::Configure(configuration, &error_message)) {
      std::cerr << error_message << '\n';
      stop_workers.store(true, std::memory_order_relaxed);
      for (std::thread& worker : workers) {
        worker.join();
      }
      return 1;
    }
    PLATFORM_LOG_INFO("host phase {} configured", phase);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    platform_logging::Shutdown();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  if (!platform_logging::Configure(configuration, &error_message)) {
    std::cerr << error_message << '\n';
    stop_workers.store(true, std::memory_order_relaxed);
    for (std::thread& worker : workers) {
      worker.join();
    }
    return 1;
  }
  PLATFORM_LOG_INFO("host final phase configured");
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  stop_workers.store(true, std::memory_order_relaxed);
  for (std::thread& worker : workers) {
    worker.join();
  }
  platform_logging::Shutdown();

  if (worker_attempts.load(std::memory_order_relaxed) == 0) {
    std::cerr << "Workers did not attempt any log writes\n";
    return 1;
  }

  const std::filesystem::path resolved_log_path =
    platform_logging_test::FindLogFileByPrefix(build_root, "platform_logging_lifecycle_test");
  if (resolved_log_path.empty()) {
    std::cerr << "Failed to find lifecycle output log file under: " << build_root << '\n';
    return 1;
  }

  const std::string log_text = platform_logging_test::ReadFile(resolved_log_path);
  if (log_text.empty()) {
    std::cerr << "Failed to read lifecycle output log file: " << resolved_log_path << '\n';
    return 1;
  }
  if (log_text.find("\"message\":\"host phase 0 configured\"") == std::string::npos) {
    std::cerr << "Missing lifecycle phase start marker\n";
    return 1;
  }
  if (log_text.find("\"message\":\"host final phase configured\"") == std::string::npos) {
    std::cerr << "Missing lifecycle final marker\n";
    return 1;
  }
  if (log_text.find("\"message\":\"race worker ") == std::string::npos) {
    std::cerr << "Missing worker log entries during lifecycle test\n";
    return 1;
  }

  return 0;
}
