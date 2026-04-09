if(NOT DEFINED PROJECT_BINARY_DIR)
  message(FATAL_ERROR "PROJECT_BINARY_DIR is required.")
endif()
if(NOT DEFINED SMOKE_SOURCE_DIR)
  message(FATAL_ERROR "SMOKE_SOURCE_DIR is required.")
endif()
if(NOT DEFINED SMOKE_BINARY_DIR)
  message(FATAL_ERROR "SMOKE_BINARY_DIR is required.")
endif()
if(NOT DEFINED INSTALL_PREFIX)
  message(FATAL_ERROR "INSTALL_PREFIX is required.")
endif()

if(EXISTS "${SMOKE_BINARY_DIR}")
  file(REMOVE_RECURSE "${SMOKE_BINARY_DIR}")
endif()

set(_install_command "${CMAKE_COMMAND}" --install "${PROJECT_BINARY_DIR}")
if(DEFINED BUILD_TYPE AND NOT BUILD_TYPE STREQUAL "")
  list(APPEND _install_command --config "${BUILD_TYPE}")
endif()
execute_process(
  COMMAND ${_install_command}
  RESULT_VARIABLE install_result
)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "Failed to install platform_logging for smoke test.")
endif()

set(_configure_command
  "${CMAKE_COMMAND}"
  -S "${SMOKE_SOURCE_DIR}"
  -B "${SMOKE_BINARY_DIR}"
  "-DCMAKE_PREFIX_PATH=${INSTALL_PREFIX}"
  "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF"
  "-Dplatform_logging_DIR=${INSTALL_PREFIX}/lib/cmake/platform_logging"
)
if(DEFINED TOOLCHAIN_FILE AND EXISTS "${TOOLCHAIN_FILE}")
  list(APPEND _configure_command "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
if(DEFINED BUILD_TYPE AND NOT BUILD_TYPE STREQUAL "")
  list(APPEND _configure_command "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()
execute_process(
  COMMAND ${_configure_command}
  RESULT_VARIABLE configure_result
)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "Failed to configure install smoke consumer.")
endif()

set(_build_command "${CMAKE_COMMAND}" --build "${SMOKE_BINARY_DIR}")
if(DEFINED BUILD_TYPE AND NOT BUILD_TYPE STREQUAL "")
  list(APPEND _build_command --config "${BUILD_TYPE}")
endif()
execute_process(
  COMMAND ${_build_command}
  RESULT_VARIABLE build_result
)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Failed to build install smoke consumer.")
endif()
