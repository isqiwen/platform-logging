#pragma once

#if defined(PLATFORM_LOGGING_STATIC_DEFINE)
#define PLATFORM_LOGGING_API
#elif defined(_WIN32)
#if defined(PLATFORM_LOGGING_BUILDING_LIBRARY)
#define PLATFORM_LOGGING_API __declspec(dllexport)
#else
#define PLATFORM_LOGGING_API __declspec(dllimport)
#endif
#else
#define PLATFORM_LOGGING_API __attribute__((visibility("default")))
#endif
