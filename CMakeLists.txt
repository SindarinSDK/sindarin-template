cmake_minimum_required(VERSION 3.14)
project(sindarin-template C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Find json-c via vcpkg
find_package(json-c CONFIG REQUIRED)

# Collect source files
file(GLOB_RECURSE LIB_SOURCES "src/*.c")

# Static library
add_library(sindarin-template STATIC ${LIB_SOURCES})
add_library(sindarin-template::sindarin-template ALIAS sindarin-template)

target_include_directories(sindarin-template
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        src
)

# json-c is PUBLIC because handlebars.h includes <json-c/json.h>
target_link_libraries(sindarin-template PUBLIC json-c::json-c)

# ---- Install rules ----

install(TARGETS sindarin-template
    EXPORT sindarin-template-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES include/handlebars.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT sindarin-template-targets
    FILE sindarin-template-targets.cmake
    NAMESPACE sindarin-template::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sindarin-template
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sindarin-template-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/sindarin-template-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sindarin-template
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sindarin-template-config.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sindarin-template
)

# ---- Tests (only when building standalone, not via vcpkg) ----

option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
    file(GLOB_RECURSE TEST_SOURCES "test/*.c")
    add_executable(sindarin-template-tests ${TEST_SOURCES})
    target_include_directories(sindarin-template-tests PRIVATE include src)
    target_link_libraries(sindarin-template-tests PRIVATE sindarin-template json-c::json-c)

    enable_testing()
    add_test(NAME integration-tests COMMAND sindarin-template-tests)
endif()
