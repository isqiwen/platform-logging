#include "test_support.h"

#include <platform_logging/logging.h>

#include <nlohmann/json.hpp>
#include <iostream>
#include <regex>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>

int main() {
  const std::filesystem::path build_root = platform_logging_test::PrepareTestRoot("platform_logging_basic_test");
  const auto original_default_logger = spdlog::default_logger();

  if (platform_logging::IsConfigured()) {
    std::cerr << "Logging should start unconfigured\n";
    return 1;
  }
  if (platform_logging::ShouldLog(platform_logging::Level::kInfo)) {
    std::cerr << "Unconfigured logger should not log\n";
    return 1;
  }
  PLATFORM_LOG_INFO("dropped before configure");

  platform_logging::Configuration invalid_configuration;
  invalid_configuration.console.enabled = false;
  invalid_configuration.file.enabled = false;
  std::string error_message;
  if (platform_logging::Configure(invalid_configuration, &error_message)) {
    std::cerr << "Configure should reject zero-sink configuration\n";
    return 1;
  }
  if (error_message.find("zero sinks") == std::string::npos) {
    std::cerr << "Missing zero-sink validation error message\n";
    return 1;
  }

  platform_logging::Configuration configuration;
  configuration.logger_name = "platform_logging_test";
  configuration.console.enabled = false;
  configuration.output_format = platform_logging::OutputFormat::kJson;
  configuration.file.path = (build_root / "platform_logging_test.log").string();

  if (!platform_logging::Configure(configuration, &error_message)) {
    std::cerr << error_message << '\n';
    return 1;
  }

  platform_logging::Configuration replacement = configuration;
  replacement.logger_name = "replacement_logger";
  if (platform_logging::Configure(replacement, &error_message)) {
    std::cerr << "Configure should reject duplicate initialization\n";
    return 1;
  }
  if (error_message.find("already configured") == std::string::npos) {
    std::cerr << "Missing duplicate configure error message\n";
    return 1;
  }

  const platform_logging::Configuration snapshot_before_shutdown = platform_logging::CurrentConfiguration();
  if (snapshot_before_shutdown.logger_name != "platform_logging_test") {
    std::cerr << "Unexpected configuration snapshot before shutdown\n";
    return 1;
  }
  if (!snapshot_before_shutdown.console.console_color || snapshot_before_shutdown.async_worker_count != 1 ||
      snapshot_before_shutdown.console.pattern != platform_logging::kDefaultTextPattern ||
      snapshot_before_shutdown.file.pattern != platform_logging::kDefaultTextPattern ||
      !snapshot_before_shutdown.console.channels.empty() || !snapshot_before_shutdown.file.channels.empty()) {
    std::cerr << "Unexpected default console/async worker configuration before shutdown\n";
    return 1;
  }
  if (spdlog::default_logger() != original_default_logger) {
    std::cerr << "platform_logging should not replace spdlog default logger\n";
    return 1;
  }

  PLATFORM_LOG_INFO("hello from {}", "platform_logging");
  PLATFORM_LOG_INFO_KV("structured info for slice {}", 3, platform_logging::kv("scan_uid", 42),
                       platform_logging::kv("slice", 3));
  PLATFORM_LOG_WARN_KV("plain structured info", platform_logging::kv("mode", "plain"));
  platform_logging::Shutdown();

  if (platform_logging::IsConfigured()) {
    std::cerr << "Logging should be unconfigured after shutdown\n";
    return 1;
  }
  if (platform_logging::ShouldLog(platform_logging::Level::kInfo)) {
    std::cerr << "Shutdown logger should not log\n";
    return 1;
  }
  if (spdlog::default_logger() != original_default_logger) {
    std::cerr << "platform_logging should not shutdown spdlog default logger\n";
    return 1;
  }
  if (snapshot_before_shutdown.logger_name != "platform_logging_test") {
    std::cerr << "Configuration snapshot should remain valid after shutdown\n";
    return 1;
  }
  const platform_logging::Configuration snapshot_after_shutdown = platform_logging::CurrentConfiguration();
  if (snapshot_after_shutdown.logger_name != "platform_logging" ||
      snapshot_after_shutdown.file.path != "logs/platform_logging.log" || !snapshot_after_shutdown.console.console_color ||
      snapshot_after_shutdown.async_worker_count != 1 ||
      snapshot_after_shutdown.console.pattern != platform_logging::kDefaultTextPattern ||
      snapshot_after_shutdown.file.pattern != platform_logging::kDefaultTextPattern) {
    std::cerr << "Unexpected configuration snapshot after shutdown\n";
    return 1;
  }
  PLATFORM_LOG_INFO("dropped after shutdown");

  const std::filesystem::path resolved_log_path =
    platform_logging_test::FindLogFileByPrefix(build_root, "platform_logging_test");
  if (resolved_log_path.empty()) {
    std::cerr << "Failed to find output log file under: " << build_root << '\n';
    return 1;
  }

  const std::string log_text = platform_logging_test::ReadFile(resolved_log_path);
  if (log_text.empty()) {
    std::cerr << "Failed to open output log file: " << resolved_log_path << '\n';
    return 1;
  }
  if (log_text.find("\"message\":\"hello from platform_logging\"") == std::string::npos) {
    std::cerr << "Missing info log entry\n";
    return 1;
  }
  if (log_text.find("\"message\":\"structured info for slice 3\"") == std::string::npos) {
    std::cerr << "Missing structured log entry\n";
    return 1;
  }
  if (log_text.find("\"scan_uid\":42") == std::string::npos) {
    std::cerr << "Missing structured field\n";
    return 1;
  }
  if (log_text.find("\"message\":\"plain structured info\"") == std::string::npos) {
    std::cerr << "Missing kv-only log entry\n";
    return 1;
  }
  if (log_text.find("dropped before configure") != std::string::npos) {
    std::cerr << "Unexpected pre-config log entry\n";
    return 1;
  }
  if (log_text.find("dropped after shutdown") != std::string::npos) {
    std::cerr << "Unexpected post-shutdown log entry\n";
    return 1;
  }
  if (log_text.find("\"mode\":\"plain\"") == std::string::npos) {
    std::cerr << "Missing kv-only field\n";
    return 1;
  }
  if (log_text.find("\"logger\":\"platform_logging_test\"") == std::string::npos) {
    std::cerr << "Missing logger field\n";
    return 1;
  }
  if (log_text.find("\"channel\":\"default\"") == std::string::npos) {
    std::cerr << "Missing default channel field in JSON output\n";
    return 1;
  }
  if (log_text.find("\"module\":") != std::string::npos) {
    std::cerr << "Unexpected module field\n";
    return 1;
  }

  std::istringstream log_stream(log_text);
  std::string first_line;
  if (!std::getline(log_stream, first_line)) {
    std::cerr << "Failed to read first JSON log line\n";
    return 1;
  }
  const nlohmann::json first_record = nlohmann::json::parse(first_line);
  const std::string timestamp = first_record.at("timestamp").get<std::string>();
  const std::regex local_timestamp_pattern(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}$)");
  if (!std::regex_match(timestamp, local_timestamp_pattern)) {
    std::cerr << "JSON timestamp should be emitted in plain local time\n";
    return 1;
  }

  return 0;
}
