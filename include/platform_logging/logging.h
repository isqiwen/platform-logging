#pragma once

#include "platform_logging/config.h"
#include "platform_logging/field.h"
#include "platform_logging/logger.h"

#include <array>
#include <spdlog/fmt/fmt.h>
#include <tuple>
#include <type_traits>
#include <utility>

namespace platform_logging
{

namespace detail
{

template <typename T>
inline constexpr bool kIsFieldArg = std::is_same_v<std::remove_cvref_t<T>, Field>;

template <typename T>
using FormatStringArg = std::conditional_t<kIsFieldArg<T>, std::string_view, std::remove_cvref_t<T>>;

template <typename... Args>
consteval bool FieldsAreTrailing()
{
  constexpr std::array<bool, sizeof...(Args)> is_field_arg = {kIsFieldArg<Args>...};
  bool seen_field = false;
  for (bool current : is_field_arg)
  {
    if (current)
    {
      seen_field = true;
      continue;
    }
    if (seen_field)
    {
      return false;
    }
  }
  return true;
}

template <typename... Args>
consteval std::size_t CountTrailingFieldArgs()
{
  constexpr std::array<bool, sizeof...(Args)> is_field_arg = {kIsFieldArg<Args>...};
  std::size_t count = 0;
  for (std::size_t index = is_field_arg.size(); index > 0 && is_field_arg[index - 1]; --index)
  {
    ++count;
  }
  return count;
}

template <typename... Args>
inline constexpr std::size_t kTrailingFieldArgCount = CountTrailingFieldArgs<Args...>();

template <typename... Args>
inline constexpr std::size_t kFormatArgCount = sizeof...(Args) - kTrailingFieldArgCount<Args...>;

template <typename... Args, std::size_t... Indices>
auto MakeKvFormatString(std::index_sequence<Indices...>)
  -> fmt::format_string<FormatStringArg<std::tuple_element_t<Indices, std::tuple<Args...>>>...>;

template <typename... Args>
using KvFormatString = decltype(MakeKvFormatString<Args...>(std::make_index_sequence<kFormatArgCount<Args...>>{}));

template <typename FormatString, typename Tuple, std::size_t... Indices>
std::string FormatTupleArgs(const FormatString& format_string,
                            Tuple&& args,
                            std::index_sequence<Indices...>)
{
  return fmt::vformat(
    fmt::string_view(format_string),
    fmt::make_format_args(std::get<Indices>(std::forward<Tuple>(args))...));
}

template <std::size_t Offset, typename Tuple, std::size_t... Indices>
Fields BuildFields(Tuple&& args, std::index_sequence<Indices...>)
{
  Fields fields;
  fields.reserve(sizeof...(Indices));
  (fields.emplace_back(std::get<Offset + Indices>(std::forward<Tuple>(args))), ...);
  return fields;
}

} // namespace detail

template <typename... Args>
std::string Format(fmt::format_string<Args...> format_string, Args&&... args)
{
  return fmt::format(format_string, std::forward<Args>(args)...);
}

template <typename... Args>
void LogFormat(Level level,
               const std::source_location& location,
               fmt::format_string<Args...> format_string,
               Args&&... args)
{
  Log(level, Format(format_string, std::forward<Args>(args)...), {}, location);
}

template <typename... Args>
void LogFormat(Level level,
               const Fields& fields,
               const std::source_location& location,
               fmt::format_string<Args...> format_string,
               Args&&... args)
{
  Log(level, Format(format_string, std::forward<Args>(args)...), fields, location);
}

template <typename... Args>
void LogFormatKv(Level level,
                 const std::source_location& location,
                 detail::KvFormatString<Args...> format_string,
                 Args&&... args)
{
  static_assert(
    detail::FieldsAreTrailing<Args...>(),
    "Structured fields must appear after all format arguments.");

  auto tuple = std::forward_as_tuple(std::forward<Args>(args)...);
  constexpr std::size_t format_arg_count = detail::kFormatArgCount<Args...>;
  constexpr std::size_t field_arg_count = sizeof...(Args) - format_arg_count;

  Log(
    level,
    detail::FormatTupleArgs(format_string, tuple, std::make_index_sequence<format_arg_count>{}),
    detail::BuildFields<format_arg_count>(tuple, std::make_index_sequence<field_arg_count>{}),
    location);
}

} // namespace platform_logging

#define PLATFORM_LOG_TRACE(...)                                                           \
  do                                                                                      \
  {                                                                                       \
    if (::platform_logging::ShouldLog(::platform_logging::Level::kTrace))                 \
    {                                                                                     \
      ::platform_logging::LogFormat(                                                      \
        ::platform_logging::Level::kTrace, std::source_location::current(), __VA_ARGS__); \
    }                                                                                     \
  } while (false)

#define PLATFORM_LOG_FUNCTION_ENTER() PLATFORM_LOG_TRACE("Entering {}", __FUNCTION__)

#define PLATFORM_LOG_FUNCTION_EXIT() PLATFORM_LOG_TRACE("Exiting {}", __FUNCTION__)

#define PLATFORM_LOG_DEBUG(...)                                                           \
  do                                                                                      \
  {                                                                                       \
    if (::platform_logging::ShouldLog(::platform_logging::Level::kDebug))                 \
    {                                                                                     \
      ::platform_logging::LogFormat(                                                      \
        ::platform_logging::Level::kDebug, std::source_location::current(), __VA_ARGS__); \
    }                                                                                     \
  } while (false)

#define PLATFORM_LOG_INFO(...)                                                           \
  do                                                                                     \
  {                                                                                      \
    if (::platform_logging::ShouldLog(::platform_logging::Level::kInfo))                 \
    {                                                                                    \
      ::platform_logging::LogFormat(                                                     \
        ::platform_logging::Level::kInfo, std::source_location::current(), __VA_ARGS__); \
    }                                                                                    \
  } while (false)

#define PLATFORM_LOG_WARN(...)                                                           \
  do                                                                                     \
  {                                                                                      \
    if (::platform_logging::ShouldLog(::platform_logging::Level::kWarn))                 \
    {                                                                                    \
      ::platform_logging::LogFormat(                                                     \
        ::platform_logging::Level::kWarn, std::source_location::current(), __VA_ARGS__); \
    }                                                                                    \
  } while (false)

#define PLATFORM_LOG_ERROR(...)                                                           \
  do                                                                                      \
  {                                                                                       \
    if (::platform_logging::ShouldLog(::platform_logging::Level::kError))                 \
    {                                                                                     \
      ::platform_logging::LogFormat(                                                      \
        ::platform_logging::Level::kError, std::source_location::current(), __VA_ARGS__); \
    }                                                                                     \
  } while (false)

#define PLATFORM_LOG_CRITICAL(...)                                                           \
  do                                                                                         \
  {                                                                                          \
    if (::platform_logging::ShouldLog(::platform_logging::Level::kCritical))                 \
    {                                                                                        \
      ::platform_logging::LogFormat(                                                         \
        ::platform_logging::Level::kCritical, std::source_location::current(), __VA_ARGS__); \
    }                                                                                        \
  } while (false)

#define PLATFORM_LOG_TRACE_KV(...)                                                        \
  do                                                                                      \
  {                                                                                       \
    if (::platform_logging::ShouldLog(::platform_logging::Level::kTrace))                 \
    {                                                                                     \
      ::platform_logging::LogFormatKv(                                                    \
        ::platform_logging::Level::kTrace, std::source_location::current(), __VA_ARGS__); \
    }                                                                                     \
  } while (false)

#define PLATFORM_LOG_DEBUG_KV(...)                                                        \
  do                                                                                      \
  {                                                                                       \
    if (::platform_logging::ShouldLog(::platform_logging::Level::kDebug))                 \
    {                                                                                     \
      ::platform_logging::LogFormatKv(                                                    \
        ::platform_logging::Level::kDebug, std::source_location::current(), __VA_ARGS__); \
    }                                                                                     \
  } while (false)

#define PLATFORM_LOG_INFO_KV(...)                                                        \
  do                                                                                     \
  {                                                                                      \
    if (::platform_logging::ShouldLog(::platform_logging::Level::kInfo))                 \
    {                                                                                    \
      ::platform_logging::LogFormatKv(                                                   \
        ::platform_logging::Level::kInfo, std::source_location::current(), __VA_ARGS__); \
    }                                                                                    \
  } while (false)

#define PLATFORM_LOG_WARN_KV(...)                                                        \
  do                                                                                     \
  {                                                                                      \
    if (::platform_logging::ShouldLog(::platform_logging::Level::kWarn))                 \
    {                                                                                    \
      ::platform_logging::LogFormatKv(                                                   \
        ::platform_logging::Level::kWarn, std::source_location::current(), __VA_ARGS__); \
    }                                                                                    \
  } while (false)

#define PLATFORM_LOG_ERROR_KV(...)                                                        \
  do                                                                                      \
  {                                                                                       \
    if (::platform_logging::ShouldLog(::platform_logging::Level::kError))                 \
    {                                                                                     \
      ::platform_logging::LogFormatKv(                                                    \
        ::platform_logging::Level::kError, std::source_location::current(), __VA_ARGS__); \
    }                                                                                     \
  } while (false)

#define PLATFORM_LOG_CRITICAL_KV(...)                                                        \
  do                                                                                         \
  {                                                                                          \
    if (::platform_logging::ShouldLog(::platform_logging::Level::kCritical))                 \
    {                                                                                        \
      ::platform_logging::LogFormatKv(                                                       \
        ::platform_logging::Level::kCritical, std::source_location::current(), __VA_ARGS__); \
    }                                                                                        \
  } while (false)
