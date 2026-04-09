cmake_minimum_required(VERSION 3.25)
project(platform_logging_install_smoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(platform_logging CONFIG REQUIRED)

add_executable(platform_logging_install_smoke ../install_smoke.cpp)
target_link_libraries(platform_logging_install_smoke PRIVATE platform_logging::platform_logging)
