cmake_minimum_required(VERSION 3.25)
project(model-repair VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MODELREPAIR_BUILD_CLI   "Build the command-line tool" ON)
option(MODELREPAIR_BUILD_GUI   "Build the Qt 6 GUI"          ON)
option(MODELREPAIR_BUILD_TESTS "Build the test suite"        ON)
option(MODELREPAIR_ENABLE_ASAN "Enable AddressSanitizer"     OFF)

include(cmake/CompilerWarnings.cmake)
include(cmake/FetchDependencies.cmake)

configure_file(
    include/modelrepair/Version.hpp.in
    ${CMAKE_BINARY_DIR}/include/modelrepair/Version.hpp
    @ONLY
)

find_package(CGAL 5.6 REQUIRED COMPONENTS Core)
find_package(Eigen3 REQUIRED NO_MODULE)

add_subdirectory(src)

if(MODELREPAIR_BUILD_CLI)
    add_subdirectory(cli)
endif()

if(MODELREPAIR_BUILD_GUI)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Concurrent OpenGL OpenGLWidgets)
    qt6_standard_project_setup()
    add_subdirectory(gui)
endif()

if(MODELREPAIR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
