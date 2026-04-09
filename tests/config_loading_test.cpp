#include "test_support.h"

#include <platform_logging/logger.h>

#include <filesystem>
#include <iostream>
#include <string>

int main() {
  const std::filesystem::path build_root = platform_logging_test::PrepareTestRoot("platform_logging_config_test");

  const std::filesystem::path malformed_bool_path = build_root / "malformed_bool.json";
  if (!platform_logging_test::WriteFile(malformed_bool_path, R"({"console":"yes"})")) {
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

  return 0;
}
