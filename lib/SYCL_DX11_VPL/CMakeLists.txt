# ##############################################################################
# Copyright (C) Intel Corporation
#
# SPDX-License-Identifier: MIT
# ##############################################################################
cmake_minimum_required(VERSION 3.10.2)
project(sycl-dx11-vpl)

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

set(TARGET sycl-dx11-vpl)
set(SOURCES
    src/dpcpp_compute.hpp
    src/dpcpp_compute.cpp
    src/shared_resource.hpp
    src/shared_resource.cpp
    src/shared_vpl_l0.hpp
    src/shared_vpl_l0.cpp
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
set_target_properties(${TARGET} PROPERTIES PUBLIC_HEADER "src/dpcpp_compute.hpp; src/shared_resource.hpp; src/shared_vpl_l0.hpp")
target_compile_features(${TARGET} PRIVATE cxx_std_17)

if(MSVC)
  if(NOT DEFINED ENV{VSCMD_VER})
    set(CMAKE_MSVCIDE_RUN_PATH $ENV{PATH})
  endif()
endif()

# set level zero path
set(LEVEL_ZERO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../level-zero CACHE PATH "Level-zero loader headers and libraries installation directory.")
target_include_directories(${TARGET} PUBLIC ${LEVEL_ZERO_DIR}/include)
target_link_libraries(${TARGET} ${LEVEL_ZERO_DIR}/lib/ze_loader.lib)

# vpl for mfx surfaces API
find_package(VPL REQUIRED)
target_link_libraries(${TARGET} VPL::dispatcher)


install(TARGETS ${TARGET} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                                  COMPONENT dev)
