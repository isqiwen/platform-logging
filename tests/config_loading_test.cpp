#include "test_support.h"

#include <platform_logging/logger.h>

#include <filesystem>
#include <iostream>
#include <string>

int main() {
  const std::filesystem::path build_root = platform_logging_test::PrepareTestRoot("platform_logging_config_test");

  const std::filesystem::path malformed_bool_path = build_root / "malformed_bool.json";
  if (!platform_logging_test::WriteFile(malformed_bool_path, R"({"console":{"console_color":"yes"}})")) {
    std::cerr << "Failed to write malformed bool config\n";
    return 1;
  }

  platform_logging::Configuration configuration;
  std::string error_message;
  if (platform_logging::LoadConfiguration(malformed_bool_path.string(), build_root.string(), &configuration,
                                          &error_message)) {
    std::cerr << "LoadConfiguration should reject malformed boolean fields\n";
    return 1;
  }
  if (error_message.find("must be a boolean") == std::string::npos) {
    std::cerr << "Unexpected malformed boolean error message\n";
    return 1;
  }

  const std::filesystem::path legacy_field_path = build_root / "legacy_level.json";
  if (!platform_logging_test::WriteFile(legacy_field_path, R"({"level":"info"})")) {
    std::cerr << "Failed to write legacy field config\n";
    return 1;
  }
  if (platform_logging::LoadConfiguration(legacy_field_path.string(), build_root.string(), &configuration,
                                          &error_message)) {
    std::cerr << "LoadConfiguration should reject legacy top-level level field\n";
    return 1;
  }
  if (error_message.find("console.level") == std::string::npos) {
    std::cerr << "Unexpected legacy field rejection error message\n";
    return 1;
  }

  const std::filesystem::path malformed_range_path = build_root / "malformed_range.json";
  if (!platform_logging_test::WriteFile(malformed_range_path, R"({"file":{"rotation_hour":25}})")) {
    std::cerr << "Failed to write malformed range config\n";
    return 1;
  }
  if (platform_logging::LoadConfiguration(malformed_range_path.string(), build_root.string(), &configuration,
                                          &error_message)) {
    std::cerr << "LoadConfiguration should reject out-of-range integer fields\n";
    return 1;
  }
  if (error_message.find("out of range") == std::string::npos) {
    std::cerr << "Unexpected malformed range error message\n";
    return 1;
  }

  const std::filesystem::path malformed_object_path = build_root / "malformed_object.json";
  if (!platform_logging_test::WriteFile(malformed_object_path, R"({"file":42})")) {
    std::cerr << "Failed to write malformed object config\n";
    return 1;
  }
  if (platform_logging::Configure(malformed_object_path.string(), build_root.string(), &error_message)) {
    std::cerr << "Configure should reject malformed config files\n";
    return 1;
  }
  if (platform_logging::IsConfigured()) {
    std::cerr << "Malformed configuration should not leave logger configured\n";
    return 1;
  }
  if (error_message.find("Field 'file' must be an object.") == std::string::npos) {
    std::cerr << "Unexpected malformed object error message\n";
    return 1;
  }

  const std::filesystem::path valid_config_path = build_root / "valid_config.json";
  if (!platform_logging_test::WriteFile(
        valid_config_path,
        R"({
  "logger_name": "config_test",
  "async": true,
  "queue_size": 2048,
  "async_worker_count": 3,
  "output_format": "text",
  "console": {
    "enabled": true,
    "level": "warn",
    "console_color": false,
    "pattern": "[console] [%^%l%$] %v",
    "channels": ["status"]
  },
  "file": {
    "enabled": false,
    "level": "debug",
    "pattern": "[file] [%s:%#] %v"
  }
})")) {
    std::cerr << "Failed to write valid config\n";
    return 1;
  }
  if (!platform_logging::LoadConfiguration(valid_config_path.string(), build_root.string(), &configuration,
                                           &error_message)) {
    std::cerr << "LoadConfiguration should accept the nested sink schema: " << error_message << '\n';
    return 1;
  }
  if (configuration.console.console_color) {
    std::cerr << "console.console_color should load as false\n";
    return 1;
  }
  if (configuration.console.pattern != "[console] [%^%l%$] %v" ||
      configuration.file.pattern != "[file] [%s:%#] %v") {
    std::cerr << "Unexpected sink pattern configuration values\n";
    return 1;
  }
  if (configuration.console.level != platform_logging::Level::kWarn ||
      configuration.file.level != platform_logging::Level::kDebug) {
    std::cerr << "Unexpected sink level configuration values\n";
    return 1;
  }
  if (configuration.console.channels.size() != 1 || configuration.console.channels.front() != "status") {
    std::cerr << "Unexpected console channel configuration values\n";
    return 1;
  }
  if (!configuration.async || configuration.async_worker_count != 3 || configuration.queue_size != 2048) {
    std::cerr << "Unexpected async configuration values\n";
    return 1;
  }

  platform_logging::Configuration invalid_configuration;
  invalid_configuration.console.enabled = true;
  invalid_configuration.file.enabled = false;
  invalid_configuration.async = true;
  invalid_configuration.async_worker_count = 0;
  if (platform_logging::Configure(invalid_configuration, &error_message)) {
    std::cerr << "Configure should reject async_worker_count=0 when async is enabled\n";
    return 1;
  }
  if (error_message.find("async_worker_count") == std::string::npos) {
    std::cerr << "Unexpected async_worker_count validation error message\n";
    return 1;
  }

  return 0;
}
