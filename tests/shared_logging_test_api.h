#pragma once

#if defined(_WIN32)
#if defined(PLATFORM_LOGGING_SHARED_TEST_BUILDING_LIBRARY)
#define PLATFORM_LOGGING_SHARED_TEST_API __declspec(dllexport)
#else
#define PLATFORM_LOGGING_SHARED_TEST_API __declspec(dllimport)
#endif
#else
#define PLATFORM_LOGGING_SHARED_TEST_API __attribute__((visibility("default")))
#endif
