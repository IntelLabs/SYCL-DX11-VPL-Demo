# ##############################################################################
# Copyright (C) Intel Corporation
#
# SPDX-License-Identifier: MIT
# ##############################################################################
cmake_minimum_required(VERSION 3.10.2)
project(vplcodec-lib)

# Default install places 64 bit runtimes in the environment, so we want to do a
# 64 bit build by default.
if(WIN32)
  if(NOT DEFINED CMAKE_GENERATOR_PLATFORM)
    set(CMAKE_GENERATOR_PLATFORM
        x64
        CACHE STRING "")
    message(STATUS "Generator Platform set to ${CMAKE_GENERATOR_PLATFORM}")
  endif()
endif()

set(TARGET vplcodec-lib)
set(SOURCES
    src/util.h
    src/util.cpp
    src/vplcodec.cpp
    src/vplcodec.h
    src/OffscreenDX11.cpp
    src/OffscreenDX11.h
    src/NV12shaders.cpp
    src/NV12vertex.hlsl
    src/NV12pixel.hlsl
)

# Set default build type to RelWithDebInfo if not specified
if(NOT CMAKE_BUILD_TYPE)
  message(
    STATUS "Default CMAKE_BUILD_TYPE not set using Release with Debug Info")
  set(CMAKE_BUILD_TYPE
      "RelWithDebInfo"
      CACHE
        STRING
        "Choose build type from: None Debug Release RelWithDebInfo MinSizeRel"
        FORCE)
endif()

add_library(${TARGET} STATIC ${SOURCES})
target_include_directories(${TARGET} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(${TARGET} PROPERTIES PUBLIC_HEADER "src/vplcodec.h; src/util.h")
target_compile_features(${TARGET} PRIVATE cxx_std_17)

if(MSVC)
    set_source_files_properties(src/NV12vertex.hlsl PROPERTIES VS_SHADER_TYPE Vertex VS_SHADER_MODEL 5.0 VS_SHADER_ENTRYPOINT main)
    set_source_files_properties(src/NV12pixel.hlsl PROPERTIES VS_SHADER_TYPE Pixel VS_SHADER_MODEL 5.0 VS_SHADER_ENTRYPOINT main)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  if(NOT DEFINED ENV{VSCMD_VER})
    set(CMAKE_MSVCIDE_RUN_PATH $ENV{PATH})
  endif()
endif()

find_package(VPL REQUIRED)
target_link_libraries(${TARGET} VPL::dispatcher)

if(UNIX)
  set(LIBVA_SUPPORT
      ON
      CACHE BOOL "Enable hardware support.")
  if(LIBVA_SUPPORT)
    find_package(PkgConfig REQUIRED)
    # note: pkg-config version for libva is *API* version
    pkg_check_modules(PKG_LIBVA IMPORTED_TARGET libva>=1.2)
    pkg_check_modules(PKG_LIBVA_DRM IMPORTED_TARGET libva-drm>=1.2)
    if(PKG_LIBVA_FOUND)
      target_compile_definitions(${TARGET} PUBLIC -DLIBVA_SUPPORT)
      set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
      set(THREADS_PREFER_PTHREAD_FLAG TRUE)
      find_package(Threads REQUIRED)
      target_link_libraries(${TARGET} PkgConfig::PKG_LIBVA
                            PkgConfig::PKG_LIBVA_DRM Threads::Threads)
      target_include_directories(${TARGET} PUBLIC ${PKG_LIBVA_INCLUDE_DIRS})
    else()
      message(
        SEND_ERROR
          "libva not found: set LIBVA_SUPPORT=OFF to build ${TARGET} without libva support"
      )
    endif()
  else()
    message(STATUS "Building ${TARGET} without hardware support")
  endif()
endif()

install(TARGETS ${TARGET} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                                  COMPONENT dev)
