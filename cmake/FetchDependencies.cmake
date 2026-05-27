include(FetchContent)

# lib3mf — reference 3MF I/O (Apache-2.0), not packaged for Arch
FetchContent_Declare(lib3mf
    GIT_REPOSITORY https://github.com/3MFConsortium/lib3mf.git
    GIT_TAG        v2.4.1
    GIT_SHALLOW    TRUE
    PATCH_COMMAND  ${CMAKE_COMMAND} -P "${CMAKE_SOURCE_DIR}/cmake/patch_lib3mf_gcc16.cmake"
)
set(LIB3MF_TESTS OFF CACHE BOOL "" FORCE)
# lib3mf v2.4.1 uses cmake_minimum_required < 3.5; CMake 4.x requires this override.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
    set(CMAKE_POLICY_VERSION_MINIMUM "3.5")
endif()
FetchContent_MakeAvailable(lib3mf)
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
    unset(CMAKE_POLICY_VERSION_MINIMUM)
endif()
# lib3mf's bundled libzip has Windows source files that assign LPCSTR/LPCWSTR Win32
# function pointers into void* slots. GCC 16 made -Wincompatible-pointer-types an
# error; suppress it only for lib3mf's C compilation on Windows/MinGW.
if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "GNU")
    target_compile_options(lib3mf PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-Wno-incompatible-pointer-types>)
endif()

# CLI11 — header-only argument parser (MIT)
FetchContent_Declare(CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.4.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(CLI11)

# spdlog — header-only logging (MIT)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.3
    GIT_SHALLOW    TRUE
)
set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

# tinygltf — glTF/GLB mesh I/O (MIT)
FetchContent_Declare(tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG        v2.8.21
    GIT_SHALLOW    TRUE
)
set(TINYGLTF_INSTALL      OFF CACHE BOOL "" FORCE)
set(TINYGLTF_HEADER_ONLY  ON  CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tinygltf)

# Catch2 — test framework (BSL-1.0)
if(MODELREPAIR_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.8.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)
endif()
