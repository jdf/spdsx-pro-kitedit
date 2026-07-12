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

# The document is a folder; declaring it as a package (LSTypeIsPackage +
# a UTI conforming to com.apple.package) makes Finder and the file
# choosers treat *.spdsx as a single file.
juce_add_gui_app(spdsx-patchedit PRODUCT_NAME "spdsx-patchedit"
    BUNDLE_ID com.jdf.spdsx-patchedit
    PLIST_TO_MERGE [[
<plist version="1.0">
<dict>
  <key>CFBundleDocumentTypes</key>
  <array>
    <dict>
      <key>CFBundleTypeName</key>
      <string>SPD-SX PRO Device</string>
      <key>CFBundleTypeRole</key>
      <string>Editor</string>
      <key>LSTypeIsPackage</key>
      <true/>
      <key>LSItemContentTypes</key>
      <array>
        <string>com.jdf.spdsx-device</string>
      </array>
    </dict>
  </array>
  <key>UTExportedTypeDeclarations</key>
  <array>
    <dict>
      <key>UTTypeIdentifier</key>
      <string>com.jdf.spdsx-device</string>
      <key>UTTypeDescription</key>
      <string>SPD-SX PRO Device</string>
      <key>UTTypeConformsTo</key>
      <array>
        <string>com.apple.package</string>
      </array>
      <key>UTTypeTagSpecification</key>
      <dict>
        <key>public.filename-extension</key>
        <array>
          <string>spdsx</string>
        </array>
      </dict>
    </dict>
  </array>
</dict>
</plist>
]])

target_sources(spdsx-patchedit PRIVATE
    source/audio.cc
    source/device_document.cc
    source/kit_chooser.cc
    source/layers.cc
    source/main.cc
    source/main_component.cc
    source/sample_browser.cc
    source/sample_slot.cc
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
    juce::juce_recommended_config_flags
)

# ---- Device protocol (SPD-SX PRO serial) ----
# JUCE-free: pure protocol + POSIX/macOS serial, ported from spdsx-py. Kept
# separate so it stays testable and could move to its own project later.
add_library(spdsx_device STATIC
    source/device/kit_image.cc
    source/device/protocol.cc
    source/device/serial_port.cc
    source/device/spdsx_device.cc
)
target_include_directories(spdsx_device PUBLIC source)

# spdutil: CLI for the device — ping/info/padlink, offline selftest —
# and the harness that verifies the protocol port byte-for-byte.
add_executable(spdutil source/spdutil_main.cc)
target_link_libraries(spdutil PRIVATE spdsx_device)
