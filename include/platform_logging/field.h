#pragma once

#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace platform_logging {

using FieldValue = std::variant<std::nullptr_t, bool, std::int64_t, std::uint64_t, double, std::string>;

struct Field {
  std::string key;
  FieldValue value;
};

using Fields = std::vector<Field>;

inline Field kv(std::string_view key, std::nullptr_t) {
  return Field{std::string(key), nullptr};
}

inline Field kv(std::string_view key, bool value) {
  return Field{std::string(key), value};
}

template <typename T>
requires(std::integral<T> && std::signed_integral<T> && !std::same_as<std::remove_cvref_t<T>, bool>)
inline Field kv(std::string_view key, T value) {
  return Field{std::string(key), static_cast<std::int64_t>(value)};
}

template <typename T>
requires(std::integral<T> && std::unsigned_integral<T>)
inline Field kv(std::string_view key, T value) {
  return Field{std::string(key), static_cast<std::uint64_t>(value)};
}

template <typename T>
requires std::floating_point<T>
inline Field kv(std::string_view key, T value) {
  return Field{std::string(key), static_cast<double>(value)};
}

inline Field kv(std::string_view key, const char* value) {
  return Field{std::string(key), value == nullptr ? std::string() : std::string(value)};
}

inline Field kv(std::string_view key, std::string value) {
  return Field{std::string(key), std::move(value)};
}

inline Field kv(std::string_view key, std::string_view value) {
  return Field{std::string(key), std::string(value)};
}

}  // namespace platform_logging

