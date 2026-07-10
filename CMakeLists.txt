cmake_minimum_required(VERSION 3.25)

include(cmake/prelude.cmake)

# Locate the vcpkg toolchain: an explicit CMAKE_TOOLCHAIN_FILE wins, then
# $VCPKG_ROOT, then ~/vcpkg. (Same convention as specs.)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})
        set(_vcpkg_root "$ENV{VCPKG_ROOT}")
    else()
        set(_vcpkg_root "$ENV{HOME}/vcpkg")
    endif()
    set(CMAKE_TOOLCHAIN_FILE "${_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")
endif()
if(NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
    message(FATAL_ERROR
        "vcpkg toolchain not found at ${CMAKE_TOOLCHAIN_FILE}; "
        "set VCPKG_ROOT (or CMAKE_TOOLCHAIN_FILE) to your vcpkg checkout")
endif()

project(
    spdsx-patchedit
    VERSION 0.1.0
    DESCRIPTION "Interactive kit designer for the Roland SPD-SX"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Slint ships no prebuilt C++ binaries for macOS arm64, so build the C++ API
# from source; this compiles the Slint runtime with cargo via Corrosion and
# needs a Rust toolchain on PATH.
include(FetchContent)
FetchContent_Declare(
    Slint
    GIT_REPOSITORY https://github.com/slint-ui/slint.git
    GIT_TAG v1.17.1
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR api/cpp
)
FetchContent_MakeAvailable(Slint)

# miniaudio is header-only; its vcpkg port ships no CMake config.
find_path(MINIAUDIO_INCLUDE_DIRS "miniaudio.h" REQUIRED)

add_executable(spdsx-patchedit
    source/main.cpp
    source/audio.cpp
)
slint_target_sources(spdsx-patchedit ui/app-window.slint)
target_include_directories(spdsx-patchedit PRIVATE "${MINIAUDIO_INCLUDE_DIRS}")
target_link_libraries(spdsx-patchedit PRIVATE Slint::Slint)
if(APPLE)
    # miniaudio's Core Audio backend.
    target_link_libraries(spdsx-patchedit PRIVATE
        "-framework CoreFoundation"
        "-framework CoreAudio"
        "-framework AudioToolbox"
    )
endif()

install(TARGETS spdsx-patchedit RUNTIME)
