cmake_minimum_required(VERSION 3.14)
project(sindarin-template C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find json-c via vcpkg
find_package(json-c CONFIG REQUIRED)

# Collect source files
file(GLOB_RECURSE LIB_SOURCES "src/*.c")

# Static library
add_library(sindarin-template STATIC ${LIB_SOURCES})
target_include_directories(sindarin-template PUBLIC include)
target_include_directories(sindarin-template PRIVATE src)
target_link_libraries(sindarin-template PRIVATE json-c::json-c)

# Test executable
file(GLOB_RECURSE TEST_SOURCES "test/*.c")
add_executable(sindarin-template-tests ${TEST_SOURCES})
target_include_directories(sindarin-template-tests PRIVATE include src)
target_link_libraries(sindarin-template-tests PRIVATE sindarin-template json-c::json-c)

# CTest integration
enable_testing()
add_test(NAME integration-tests COMMAND sindarin-template-tests)
