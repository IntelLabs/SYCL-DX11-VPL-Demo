# Projects Settings
cmake_minimum_required (VERSION 3.25)
project (test_transcode)
#cmake -DCMAKE_C_COMPILER=icx -DCMAKE_CXX_COMPILER=icx
#find_package(IntelSYCL REQUIRED)
# The main executable
add_executable(test_transcode
  src/main.cpp
)
#use dynamic runtimes
set_property(TARGET test_transcode PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# decoder
if(NOT TARGET vplcodec-lib)
    message(STATUS "Adding vplcodec-lib subdirectory")
    add_subdirectory(${CMAKE_SOURCE_DIR}/lib/vplcodec-lib ${CMAKE_CURRENT_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
target_link_libraries(test_transcode PRIVATE vplcodec-lib)

# sycl-dx11-vpl
if(NOT TARGET sycl-dx11-vpl)
    message(STATUS "Adding SYCL_DX11_VPL subdirectory")
    add_subdirectory(${CMAKE_SOURCE_DIR}/lib/SYCL_DX11_VPL ${CMAKE_CURRENT_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
target_link_libraries(test_transcode PRIVATE sycl-dx11-vpl)

#set_property(TARGET test_transcode PROPERTY CXX_STANDARD 20)
#target_compile_options(test_transcode PRIVATE -Wall)
target_compile_features(test_transcode PRIVATE cxx_std_20)
# OpenCV
find_package(OpenCV REQUIRED)
IF(OpenCV_FOUND)
	target_include_directories(test_transcode PRIVATE ${OpenCV_INCLUDE_DIRS})
	target_link_libraries(test_transcode PRIVATE ${OpenCV_LIBS})
ELSE(OpenCV_FOUND)
	MESSAGE(FATAL_ERROR "OpenCV library not found")
ENDIF(OpenCV_FOUND)

#VPL
find_package(VPL REQUIRED)
target_link_libraries(test_transcode PRIVATE VPL::dispatcher)


# level zero code
link_directories(${CMAKE_SOURCE_DIR}/lib/level-zero/lib)
target_link_libraries(test_transcode PRIVATE ${CMAKE_SOURCE_DIR}/lib/level-zero/lib/ze_loader.lib)
target_include_directories(test_transcode PRIVATE ${CMAKE_SOURCE_DIR}/lib/level-zero/include)

function(copy_target_dlls TGT)
  if(WIN32)
    if(ARGC GREATER 1)
      set(DEP ${ARGV1})
    else()
      set(DEP ${TGT})
    endif()
    add_custom_command(
    TARGET "${TGT}"
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_RUNTIME_DLLS:${DEP}>" "$<TARGET_FILE_DIR:${TGT}>"
    COMMAND_EXPAND_LISTS
    COMMENT "Copying DLLs for ${DEP}"
    VERBATIM
    )
  endif()
endfunction()

if(WIN32)
    copy_target_dlls(test_transcode)
#[[  set(DLL_DIR "Win64")
  if("${CMAKE_SIZEOF_VOID_P}" STREQUAL "4")
    set(DLL_DIR "Win32")
  endif()

if(NOT TARGET OpenCV)
    add_library(OpenCV SHARED IMPORTED GLOBAL)
    set_property(TARGET OpenCV PROPERTY IMPORTED_IMPLIB_RELEASE "${CMAKE_SOURCE_DIR}/lib/OpenCV/x64/vc16/lib/opencv_world480.lib")
    set_property(TARGET OpenCV PROPERTY IMPORTED_LOCATION_RELEASE "${CMAKE_SOURCE_DIR}/lib/OpenCV/x64/vc16/bin/opencv_world480.dll")
    set_property(TARGET OpenCV PROPERTY IMPORTED_IMPLIB_DEBUG "${CMAKE_SOURCE_DIR}/lib/OpenCV/x64/vc16/lib/opencv_world480d.lib")
    set_property(TARGET OpenCV PROPERTY IMPORTED_LOCATION_DEBUG "${CMAKE_SOURCE_DIR}/lib/OpenCV/x64/vc16/bin/opencv_world480d.dll")
    set_property(TARGET OpenCV PROPERTY IMPORTED_IMPLIB_MINSIZEREL "${CMAKE_SOURCE_DIR}/lib/OpenCV/x64/vc16/lib/opencv_world480.lib")
    set_property(TARGET OpenCV PROPERTY IMPORTED_LOCATION_MINSIZEREL "${CMAKE_SOURCE_DIR}/lib/OpenCV/x64/vc16/bin/opencv_world480.dll")
    set_property(TARGET OpenCV PROPERTY IMPORTED_IMPLIB_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/lib/OpenCV/x64/vc16/lib/opencv_world480.lib")
    set_property(TARGET OpenCV PROPERTY IMPORTED_LOCATION_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/lib/OpenCV/x64/vc16/bin/opencv_world480.dll")
endif()

add_custom_command(TARGET test_transcode POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different 
  $<TARGET_FILE:OpenCV> 
  $<TARGET_FILE_DIR:test_transcode> )

  # vpl dispatcher copy dlls
  if(VPL_SHARED)
  add_custom_command(TARGET test_transcode POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different 
    $<TARGET_FILE:VPL::dispatcher> 
    $<TARGET_FILE_DIR:test_transcode> )
  endif(VPL_SHARED)
]]#
endif(WIN32)

