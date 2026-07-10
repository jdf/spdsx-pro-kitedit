cmake_minimum_required(VERSION 3.14)

include(cmake/prelude.cmake)

project(
    spdsx-patchedit
    VERSION 0.1.0
    DESCRIPTION "Short description"
    HOMEPAGE_URL "https://example.com/"
    LANGUAGES CXX
)

include(cmake/project-is-top-level.cmake)
include(cmake/variables.cmake)

# ---- Declare library ----

add_library(
    spdsx-patchedit_lib OBJECT
    source/lib.cpp
)

target_include_directories(
    spdsx-patchedit_lib ${warning_guard}
    PUBLIC
    "\$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/source>"
)

target_compile_features(spdsx-patchedit_lib PUBLIC cxx_std_20)

find_package(fmt REQUIRED)
target_link_libraries(spdsx-patchedit_lib PRIVATE fmt::fmt)

# ---- Declare executable ----

add_executable(spdsx-patchedit_exe source/main.cpp)
add_executable(spdsx-patchedit::exe ALIAS spdsx-patchedit_exe)

set_property(TARGET spdsx-patchedit_exe PROPERTY OUTPUT_NAME spdsx-patchedit)

target_compile_features(spdsx-patchedit_exe PRIVATE cxx_std_20)

target_link_libraries(spdsx-patchedit_exe PRIVATE spdsx-patchedit_lib)

# ---- Install rules ----

if(NOT CMAKE_SKIP_INSTALL_RULES)
  include(cmake/install-rules.cmake)
endif()

# ---- Developer mode ----

if(NOT spdsx-patchedit_DEVELOPER_MODE)
  return()
elseif(NOT PROJECT_IS_TOP_LEVEL)
  message(
      AUTHOR_WARNING
      "Developer mode is intended for developers of spdsx-patchedit"
  )
endif()

include(cmake/dev-mode.cmake)
