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
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(JUCE CONFIG REQUIRED)
# specgram comes from the overlay port in ports/, which builds the sibling
# specs checkout.
find_package(specgram CONFIG REQUIRED)
# The document is a SQLite database (kits, pads, samples-with-audio-blobs).
find_package(unofficial-sqlite3 CONFIG REQUIRED)

# The document is a single SQLite file (*.spdsx) — no package/UTI needed.
juce_add_gui_app(spdsx-patchedit PRODUCT_NAME "spdsx-patchedit"
    BUNDLE_ID com.jdf.spdsx-patchedit)

target_sources(spdsx-patchedit PRIVATE
    source/audio.cc
    source/device_db.cc
    source/device_document.cc
    source/device_samples.cc
    source/device_sync.cc
    source/kit_chooser.cc
    source/layers.cc
    source/main.cc
    source/main_component.cc
    source/pad_settings.cc
    source/sample_browser.cc
    source/sample_slot.cc
    source/sample_upload.cc
    source/spectro.cc
)
target_compile_definitions(spdsx-patchedit PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
target_link_libraries(spdsx-patchedit PRIVATE
    juce::juce_audio_utils
    juce::juce_gui_basics
    specgram::specgram
    spdsx_device
    unofficial::sqlite3::sqlite3
    juce::juce_recommended_config_flags
)

# ---- Device protocol (SPD-SX PRO serial) ----
# JUCE-free: pure protocol + POSIX/macOS serial, ported from spdsx-py. Kept
# separate so it stays testable and could move to its own project later.
add_library(spdsx_device STATIC
    source/device/kit_image.cc
    source/device/sample_image.cc
    source/device/protocol.cc
    source/device/spdsx_device.cc
)
target_include_directories(spdsx_device PUBLIC source)

# The platform's PortBackend — the only part of the device layer that is not
# portable. Exactly one implementation links; everything else talks to the
# interfaces in device/serial_port.h. Failing here says what is missing,
# rather than leaving a link error to imply it.
if(APPLE)
    target_sources(spdsx_device PRIVATE source/device/serial_port_macos.cc)
else()
    message(FATAL_ERROR
        "No PortBackend for this platform. The device layer is portable; "
        "reaching the hardware is not. Implement SerialPort + PortBackend "
        "(see source/device/serial_port.h and serial_port_macos.cc) and add "
        "it here.")
endif()

# spdutil: CLI for the device — ping/info/dump/kits/samples, the wave
# read/write ops, and padlink. The protocol itself is verified by the
# source/device/*_test.cc suites, byte-for-byte against the captures.
add_executable(spdutil source/spdutil_main.cc)
target_link_libraries(spdutil PRIVATE spdsx_device)

# ---- Unit tests & coverage ----
# One test binary compiles every source/*_test.cc (each header's suite, next
# to the header it covers) alongside the production translation units, so line
# coverage is attributed to our code. It is a JUCE console app because most
# headers pull in JUCE; the two entry points (main.cc, spdutil_main.cc) are
# left out so gtest_main provides main().
option(SPDSX_TESTS "Build unit tests" ON)
option(SPDSX_COVERAGE "Instrument the test build for LLVM source-based coverage" OFF)

if(SPDSX_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)

    juce_add_console_app(spdsx_tests PRODUCT_NAME "spdsx_tests")
    # The suites: one *_test.cc per covered header. Add new ones here.
    target_sources(spdsx_tests PRIVATE
        testing/juce_test_environment.cc
        source/actions_test.cc
        source/device/kit_image_test.cc
        source/device/protocol_test.cc
        source/device/sample_image_test.cc
        source/device/spdsx_device_test.cc
        source/device_db_test.cc
        source/device_document_test.cc
        source/device_model_test.cc
        source/device_sync_test.cc
        source/kit_model_test.cc
        source/layers_test.cc
        source/sample_upload_test.cc
    )
    # The production translation units under test.
    target_sources(spdsx_tests PRIVATE
        source/audio.cc
        source/device_db.cc
        source/device_document.cc
        source/device_samples.cc
        source/device_sync.cc
        source/kit_chooser.cc
        source/layers.cc
        source/main_component.cc
        source/pad_settings.cc
        source/sample_browser.cc
        source/sample_slot.cc
        source/sample_upload.cc
        source/spectro.cc
    )
    target_include_directories(spdsx_tests PRIVATE source testing)
    target_compile_definitions(spdsx_tests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    target_link_libraries(spdsx_tests PRIVATE
        juce::juce_audio_utils
        juce::juce_gui_basics
        specgram::specgram
        spdsx_device
        unofficial::sqlite3::sqlite3
        GTest::gtest
        GTest::gtest_main
        juce::juce_recommended_config_flags
    )

    if(SPDSX_COVERAGE)
        # Source-based coverage (Apple clang): instrument both the test TUs and
        # the JUCE-free device lib, whose pure headers are prime test targets.
        set(_spdsx_cov_flags -fprofile-instr-generate -fcoverage-mapping)
        target_compile_options(spdsx_tests PRIVATE ${_spdsx_cov_flags})
        target_link_options(spdsx_tests PRIVATE ${_spdsx_cov_flags})
        target_compile_options(spdsx_device PRIVATE ${_spdsx_cov_flags})
    endif()

    include(GoogleTest)
    gtest_discover_tests(spdsx_tests)

    # `coverage`: run the suite under instrumentation and print a report scoped
    # to source/ (see testing/coverage.sh). Needs -DSPDSX_COVERAGE=ON, i.e. the
    # `coverage` preset.
    add_custom_target(coverage
        COMMAND ${CMAKE_COMMAND} -E env
            "SPDSX_TESTS_BIN=$<TARGET_FILE:spdsx_tests>"
            "SPDSX_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/source"
            "SPDSX_COVERAGE_DIR=${CMAKE_CURRENT_BINARY_DIR}/coverage"
            bash "${CMAKE_CURRENT_SOURCE_DIR}/testing/coverage.sh"
        DEPENDS spdsx_tests
        USES_TERMINAL
        VERBATIM
    )
endif()
