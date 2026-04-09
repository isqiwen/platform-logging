#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace platform_logging_test {

namespace fs = std::filesystem;

inline bool Fail(std::string_view message) {
  std::cerr << message << '\n';
  return false;
}

inline bool Expect(bool condition, std::string_view message) {
  if (!condition) {
    return Fail(message);
  }
  return true;
}

inline fs::path PrepareTestRoot(std::string_view name) {
  const fs::path build_root = fs::temp_directory_path() / name;
  std::error_code ec;
  fs::remove_all(build_root, ec);
  fs::create_directories(build_root, ec);
  return build_root;
}

inline fs::path FindLogFileByPrefix(const fs::path& root, std::string_view prefix) {
  for (const fs::directory_entry& entry : fs::directory_iterator(root)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    if (entry.path().filename().string().rfind(prefix, 0) == 0) {
      return entry.path();
    }
  }
  return {};
}

inline std::string ReadFile(const fs::path& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    return {};
  }

  std::stringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

inline bool WriteFile(const fs::path& path, std::string_view content) {
  std::ofstream output(path);
  if (!output.is_open()) {
    return false;
  }
  output << content;
  return static_cast<bool>(output);
}

inline std::size_t CountSubstring(std::string_view text, std::string_view needle) {
  if (needle.empty()) {
    return 0;
  }

  std::size_t count = 0;
  std::size_t offset = 0;
  while (true) {
    const std::size_t position = text.find(needle, offset);
    if (position == std::string_view::npos) {
      return count;
    }
    ++count;
    offset = position + needle.size();
  }
}

} // namespace platform_logging_test
