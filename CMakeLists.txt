cmake_minimum_required(VERSION 3.25)
project(platform_logging VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
option(PLATFORM_LOGGING_BUILD_SHARED "Build platform_logging as a shared library." ON)

if(PLATFORM_LOGGING_BUILD_SHARED)
    set(PLATFORM_LOGGING_LIBRARY_TYPE SHARED)
else()
    set(PLATFORM_LOGGING_LIBRARY_TYPE STATIC)
endif()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

set(PL_PUBLIC_HEADERS
    include/platform_logging/config.h
    include/platform_logging/export.h
    include/platform_logging/field.h
    include/platform_logging/logger.h
    include/platform_logging/logging.h
)

add_library(platform_logging
    ${PLATFORM_LOGGING_LIBRARY_TYPE}
    src/config.cpp
    src/logger.cpp
)
add_library(platform_logging::platform_logging ALIAS platform_logging)

target_sources(platform_logging
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
        FILES ${PL_PUBLIC_HEADERS}
)

target_include_directories(platform_logging
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(platform_logging
    PUBLIC
        nlohmann_json::nlohmann_json
        spdlog::spdlog
)
target_compile_definitions(platform_logging
    PRIVATE
        $<$<BOOL:${PLATFORM_LOGGING_BUILD_SHARED}>:PLATFORM_LOGGING_BUILDING_LIBRARY>
    PUBLIC
        $<$<NOT:$<BOOL:${PLATFORM_LOGGING_BUILD_SHARED}>>:PLATFORM_LOGGING_STATIC_DEFINE>
)
target_compile_features(platform_logging PUBLIC cxx_std_20)
set_target_properties(platform_logging PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME platform_logging
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/platform_loggingConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/platform_loggingConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/platform_logging"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/platform_loggingConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(TARGETS platform_logging
    EXPORT platform_loggingTargets
    FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
install(EXPORT platform_loggingTargets
    FILE platform_loggingTargets.cmake
    NAMESPACE platform_logging::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/platform_logging"
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/platform_loggingConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/platform_loggingConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/platform_logging"
)
install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/config/logging_config.json"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/config"
)

if(BUILD_TESTING)
    function(platform_logging_add_test name source_file)
        add_executable(${name} ${source_file})
        target_link_libraries(${name}
            PRIVATE
                platform_logging
        )
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    platform_logging_add_test(platform_logging_basic_test tests/basic_logging_test.cpp)
    platform_logging_add_test(platform_logging_config_loading_test tests/config_loading_test.cpp)
    platform_logging_add_test(platform_logging_text_test tests/text_logging_test.cpp)
    platform_logging_add_test(platform_logging_async_test tests/async_logging_test.cpp)
    platform_logging_add_test(platform_logging_concurrent_test tests/concurrent_logging_test.cpp)
    platform_logging_add_test(platform_logging_lifecycle_concurrency_test tests/lifecycle_concurrency_test.cpp)

    if(PLATFORM_LOGGING_BUILD_SHARED)
        add_library(platform_logging_shared_test_lib_a SHARED tests/shared_logging_lib_a.cpp)
        add_library(platform_logging_shared_test_lib_b SHARED tests/shared_logging_lib_b.cpp)
        target_link_libraries(platform_logging_shared_test_lib_a
            PRIVATE
                platform_logging
        )
        target_compile_definitions(platform_logging_shared_test_lib_a
            PRIVATE
                PLATFORM_LOGGING_SHARED_TEST_BUILDING_LIBRARY
        )
        target_link_libraries(platform_logging_shared_test_lib_b
            PRIVATE
                platform_logging
        )
        target_compile_definitions(platform_logging_shared_test_lib_b
            PRIVATE
                PLATFORM_LOGGING_SHARED_TEST_BUILDING_LIBRARY
        )
        set_target_properties(platform_logging_shared_test_lib_a PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
        set_target_properties(platform_logging_shared_test_lib_b PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

        add_executable(platform_logging_shared_library_test tests/shared_library_test.cpp)
        target_link_libraries(platform_logging_shared_library_test
            PRIVATE
                platform_logging
                platform_logging_shared_test_lib_a
                platform_logging_shared_test_lib_b
        )
        add_test(NAME platform_logging_shared_library_test COMMAND platform_logging_shared_library_test)
    endif()

    add_test(
        NAME platform_logging_install_smoke
        COMMAND "${CMAKE_COMMAND}"
            -DPROJECT_BINARY_DIR=${CMAKE_BINARY_DIR}
            -DSMOKE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/install_package_smoke
            -DSMOKE_BINARY_DIR=${CMAKE_BINARY_DIR}/install_smoke
            -DINSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
            -DTOOLCHAIN_FILE=${CMAKE_BINARY_DIR}/conan_toolchain.cmake
            -DBUILD_TYPE=${CMAKE_BUILD_TYPE}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunInstallSmokeTest.cmake"
    )
endif()
