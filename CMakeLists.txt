# CMakeLists.txt -- Build system for the cuda-kernel-loader (qmlp) modules
#
# Copyright (c) 2022 Sebastian Weiss <sebastian13.weiss@tum.de>
#
# All rights reserved. Use of this source code is governed by a
# MIT-style license that can be found in the LICENSE file.

cmake_minimum_required(VERSION 3.4...3.18)
cmake_policy(SET CMP0127 NEW)

# Extract project version from source
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src_cpp/include/qmlp/common.h"
     qmlp_version_defines REGEX "#define QUICKMLP_VERSION_(MAJOR|MINOR|PATCH) ")

foreach(ver ${qmlp_version_defines})
  if(ver MATCHES [[#define QUICKMLP_VERSION_(MAJOR|MINOR|PATCH) +([^ ]+)$]])
    set(QUICKMLP_VERSION_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
  endif()
endforeach()

if(QUICKMLP_VERSION_PATCH MATCHES [[\.([a-zA-Z0-9]+)$]])
  set(qmlp_VERSION_TYPE "${CMAKE_MATCH_1}")
endif()
string(REGEX MATCH "^[0-9]+" QUICKMLP_VERSION_PATCH "${QUICKMLP_VERSION_PATCH}")

project(
  qmlp
  LANGUAGES CXX CUDA
  VERSION "${QUICKMLP_VERSION_MAJOR}.${QUICKMLP_VERSION_MINOR}.${QUICKMLP_VERSION_PATCH}")

# Standard includes
include(CMakePackageConfigHelpers)
include(CMakeDependentOption)

if(NOT qmlp_FIND_QUIETLY)
  message(STATUS "qmlp v${qmlp_VERSION} ${qmlp_VERSION_TYPE}")
endif()

# Avoid infinite recursion if tests include this as a subdirectory
if(DEFINED QUICKMLP_MASTER_PROJECT)
  set(QUICKMLP_TEST OFF)
endif()

# Check if qmlp is being used directly or via add_subdirectory
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR AND NOT DEFINED QUICKMLP_MASTER_PROJECT)
  ### Warn if not an out-of-source builds
  if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_CURRENT_BINARY_DIR)
    set(lines
        "You are building in-place. If that is not what you intended to "
        "do, you can clean the source directory with:\n"
        "rm -r CMakeCache.txt CMakeFiles/ cmake_uninstall.cmake qmlpConfig.cmake "
        "qmlpConfigVersion.cmake tests/CMakeFiles/\n")
    message(AUTHOR_WARNING ${lines})
  endif()

  set(QUICKMLP_MASTER_PROJECT ON)

  if(OSX AND CMAKE_VERSION VERSION_LESS 3.7)
    # Bug in macOS CMake < 3.7 is unable to download catch
    message(WARNING "CMAKE 3.7+ needed on macOS to download catch, and newer HIGHLY recommended")
  elseif(WINDOWS AND CMAKE_VERSION VERSION_LESS 3.8)
    # Only tested with 3.8+ in CI.
    message(WARNING "CMAKE 3.8+ tested on Windows, previous versions untested")
  endif()

  message(STATUS "CMake ${CMAKE_VERSION}")

  if(CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
  endif()

  set(qmlp_system "")
else()
  set(QUICKMLP_MASTER_PROJECT OFF)
  set(qmlp_system SYSTEM)
endif()

# Options
option(QUICKMLP_TEST "Build qmlp test suite?" ${QUICKMLP_MASTER_PROJECT})
option(QUICKMLP_SHARED "Build qmlp as a shared library?" OFF)
option(QUICKMLP_PYTORCH_BINDINGS "Build PyTorch bindings?" OFF)
option(QUICKMLP_PYTORCH_BINDINGS_SHARED "Build PyTorch bindings as a shared library?. Dynamic loading in python requires this." ON)

# CUDA toolkit
find_package(CUDAToolkit REQUIRED)

# Third-party
add_subdirectory(third-party/cuda-kernel-loader)
if (NOT nlohmann_json_FOUND AND NOT NLOHMANN_JSON_FOUND)
  # Check for compatibility with tiny-cuda-nn whether the JSON library was already found elsewhere.
  add_subdirectory(third-party/json)
endif()
add_subdirectory(third-party/magic_enum)
if(NOT TARGET spdlog::spdlog)
    add_subdirectory(third-party/spdlog)
endif()

# currently not needed
# set(CUTLASS_ENABLE_HEADERS_ONLY ON CACHE BOOL "Enable only the header library")
# add_subdirectory(third-party/cutlass) 

# Files
set(QUICKMLP_HEADERS
    src_cpp/include/qmlp/common.h
    src_cpp/include/qmlp/activation.h
    src_cpp/include/qmlp/iencoding.h
    src_cpp/include/qmlp/encoding_identity.h
    src_cpp/include/qmlp/encoding_hashgrid.h
    src_cpp/include/qmlp/encoding_line_integration.h
    src_cpp/include/qmlp/fused_network.h
    src_cpp/include/qmlp/qmlp.h
    src_cpp/include/qmlp/errors.h
    src_cpp/include/qmlp/tensor.h
    src_cpp/include/qmlp/tmp_memory.h
    src_cpp/include/qmlp/utils.h
    )
    
set(QUICKMLP_RESOURCES
    src_cpp/include/qmlp/builtin-activations.json

    src_cpp/include/qmlp/kernels/common.cuh
    src_cpp/include/qmlp/kernels/loops.cuh
    src_cpp/include/qmlp/kernels/tensor.cuh
    src_cpp/include/qmlp/kernels/encoding_identity.cuh
    src_cpp/include/qmlp/kernels/encoding_hashgrid.cuh
    src_cpp/include/qmlp/kernels/encoding_hashgrid_config.cuh
    src_cpp/include/qmlp/kernels/encoding_line_integration.cuh
    src_cpp/include/qmlp/kernels/encoding_line_integration_config.cuh
    src_cpp/include/qmlp/kernels/layer.cuh
    src_cpp/include/qmlp/kernels/network_forward.cuh
    src_cpp/include/qmlp/kernels/network_backward.cuh
    src_cpp/include/qmlp/kernels/network_weight_update_block.cuh
    src_cpp/include/qmlp/kernels/activation_kernels.cuh
    src_cpp/include/qmlp/kernels/encoding_kernels.cuh
    src_cpp/include/qmlp/kernels/pullpush.cuh
)

set(QUICKMLP_SOURCES
    src_cpp/src/qmlp.cpp
    src_cpp/src/activation.cpp
    src_cpp/src/tensor.cpp
    src_cpp/src/encoding_identity.cpp
    src_cpp/src/encoding_hashgrid.cpp
    src_cpp/src/encoding_line_integration.cpp
    src_cpp/src/iencoding.cpp
    src_cpp/src/fused_network.cpp
    src_cpp/src/utils.cpp
    )

# Compare with grep and warn if mismatched
if(QUICKMLP_MASTER_PROJECT AND NOT CMAKE_VERSION VERSION_LESS 3.12)
  file(
    GLOB_RECURSE _qmlp_header_check
    LIST_DIRECTORIES false
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    CONFIGURE_DEPENDS "src_cpp/include/qmlp/*")
  set(_qmlp_here_only ${QUICKMLP_HEADERS} ${QUICKMLP_RESOURCES})
  set(_qmlp_disk_only ${_qmlp_header_check})
  list(REMOVE_ITEM _qmlp_here_only ${_qmlp_header_check})
  list(REMOVE_ITEM _qmlp_disk_only ${QUICKMLP_HEADERS} ${QUICKMLP_RESOURCES})
  if(_qmlp_here_only)
    message(AUTHOR_WARNING "QUICKMLP_HEADERS+QUICKMLP_RESOURCES has extra files:" ${_qmlp_here_only})
  endif()
  if(_qmlp_disk_only)
    message(AUTHOR_WARNING "QUICKMLP_HEADERS+QUICKMLP_RESOURCES is missing files:" ${_qmlp_disk_only})
  endif()

  file(
    GLOB_RECURSE _qmlp_src_check
    LIST_DIRECTORIES false
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    CONFIGURE_DEPENDS "src_cpp/src/*.cpp")
  set(_qmlp_here_only ${QUICKMLP_SOURCES})
  set(_qmlp_disk_only ${_qmlp_src_check})
  list(REMOVE_ITEM _qmlp_here_only ${_qmlp_src_check})
  list(REMOVE_ITEM _qmlp_disk_only ${QUICKMLP_SOURCES})
  if(_qmlp_here_only)
    message(AUTHOR_WARNING "QUICKMLP_SOURCES has extra files:" ${_qmlp_here_only})
  endif()
  if(_qmlp_disk_only)
    message(AUTHOR_WARNING "QUICKMLP_SOURCES is missing files:" ${_qmlp_disk_only})
  endif()
endif()

# CMake 3.12 added list(TRANSFORM <list> PREPEND
# But we can't use it yet
string(REPLACE "src_cpp/include/" "${CMAKE_CURRENT_SOURCE_DIR}/src_cpp/include/" QUICKMLP_HEADERS
               "${QUICKMLP_HEADERS}")
string(REPLACE "src_cpp/include/" "${CMAKE_CURRENT_SOURCE_DIR}/src_cpp/include/" QUICKMLP_RESOURCES
               "${QUICKMLP_RESOURCES}")
string(REPLACE "src_cpp/src/" "${CMAKE_CURRENT_SOURCE_DIR}/src_cpp/src/" QUICKMLP_SOURCES
               "${QUICKMLP_SOURCES}")

# Cache variable so this can be used in parent projects
set(qmlp_INCLUDE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/src_cpp/include"
    CACHE INTERNAL "Directory where qmlp headers are located")

# Backward compatible variable for add_subdirectory mode
if(NOT QUICKMLP_MASTER_PROJECT)
  set(QUICKMLP_INCLUDE_DIR
      "${qmlp_INCLUDE_DIR}"
      CACHE INTERNAL "")
endif()

# Note: when creating targets, you cannot use if statements at configure time -
# you need generator expressions, because those will be placed in the target file.
# You can also place ifs *in* the Config.in, but not here.

# This section builds targets
# Non-IMPORT targets cannot be defined twice
if(NOT TARGET qmlp-library)
  # Build the headers-only target (no Python included):
  # (long name used here to keep this from clashing in subdirectory mode)
  if (QUICKMLP_SHARED)
    add_library(qmlp-library SHARED
        ${QUICKMLP_HEADERS} ${QUICKMLP_RESOURCES} ${QUICKMLP_SOURCES})
  else()
    add_library(qmlp-library STATIC
        ${QUICKMLP_HEADERS} ${QUICKMLP_RESOURCES} ${QUICKMLP_SOURCES})
  endif()
  add_library(qmlp::qmlp-library ALIAS qmlp-library) # to match exported target

  set_target_properties(qmlp-library PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )
  source_group("CUDA" REGULAR_EXPRESSION ".*\.cuh")

  target_include_directories(
    qmlp-library ${qmlp_system} PUBLIC ${qmlp_INCLUDE_DIR})
  target_include_directories(qmlp-library PRIVATE 
    ${CMAKE_CURRENT_BINARY_DIR} third-party/tinyformat)

  # CUDA Stuff
  target_link_libraries(qmlp-library 
    PUBLIC
        CUDA::cudart 
        ckl::ckl 
        nlohmann_json::nlohmann_json
        spdlog::spdlog
    PRIVATE
        #nvidia::cutlass::cutlass 
        magic_enum::magic_enum)
  
else()
  # It is invalid to install a target twice, too.
  set(QUICKMLP_INSTALL OFF)
endif()


# BUILD_TESTING takes priority, but only if this is the master project
if(QUICKMLP_MASTER_PROJECT AND BUILD_TESTING)
    add_subdirectory(tests_cpp)
else()
  if(QUICKMLP_TEST)
    add_subdirectory(tests_cpp)
  endif()
endif()

# BUILD PYTORCH BINDINGS
if (QUICKMLP_PYTORCH_BINDINGS)
    add_subdirectory(src_cpp/pytorch-bindings)
endif()

# Better symmetry with find_package(qmlp CONFIG) mode.
if(NOT QUICKMLP_MASTER_PROJECT)
  set(qmlp_FOUND
      TRUE
      CACHE INTERNAL "True if qmlp and all required components found on the system")
endif()
