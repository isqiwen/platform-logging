#include "shared_logging_test_api.h"

#include <platform_logging/logging.h>

extern "C" PLATFORM_LOGGING_SHARED_TEST_API void platform_logging_shared_lib_b_log() {
  PLATFORM_LOG_INFO("shared library b entry");
}
