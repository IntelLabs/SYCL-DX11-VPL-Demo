# Projects Settings
cmake_minimum_required (VERSION 3.23)
project (onevpl-dx11-unified)

# shaders
INCLUDE_DIRECTORIES(
	"${CMAKE_CURRENT_BINARY_DIR}/Shaders/$(Platform)/$(Configuration)"
)

set(SHADERS 
src/nv12_rgba_conv_cs.hlsl
)

set_source_files_properties( ${SHADERS}
    PROPERTIES
        VS_SHADER_MODEL 5.0
        VS_SHADER_DISABLE_OPTIMIZATIONS $<$<CONFIG:Debug>:ON>
        VS_SHADER_ENABLE_DEBUG $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:ON>
        VS_SHADER_OUTPUT_HEADER_FILE "${CMAKE_CURRENT_BINARY_DIR}/Shaders/$(Platform)/$(Configuration)/%(Filename).h"
	VS_SHADER_VARIABLE_NAME "g_%(Filename)"
	VS_SHADER_OBJECT_FILE_NAME ""
)

set_source_files_properties(src/nv12_rgba_conv_cs.hlsl PROPERTIES
 	VS_SHADER_TYPE Compute VS_SHADER_ENTRYPOINT "main_cs"
)


# The main executable
add_executable(main
  src/main.cpp
  src/lf_basics.hpp
  src/render_window.hpp
  src/render_window.cpp
  src/RenderDX11.hpp
  src/RenderDX11.cpp
  src/NV12LFShaders.cpp
  src/NV12LFShaders.hpp
  ${SHADERS}
)

set_property(TARGET main PROPERTY CXX_STANDARD 20)
target_compile_options(main PRIVATE -Wall)
target_compile_features(main PRIVATE cxx_std_20)

# decoder
add_subdirectory(lib/vplcodec-lib EXCLUDE_FROM_ALL)
target_link_libraries(main PRIVATE vplcodec-lib)

# Looking Glass Bridge Runtime
target_include_directories(main PRIVATE ${CMAKE_SOURCE_DIR}/lib/BridgeRuntime)

# sycl-dx11-vpl library
add_subdirectory(lib/SYCL_DX11_VPL EXCLUDE_FROM_ALL)
target_link_libraries(main PRIVATE sycl-dx11-vpl)

# level zero code
#link_directories(lib/level-zero/lib)
#target_link_libraries(main PRIVATE ${CMAKE_SOURCE_DIR}/lib/level-zero/lib/ze_loader.lib)
#target_include_directories(main PRIVATE ${CMAKE_SOURCE_DIR}/lib/level-zero/include)

# OpenCV
find_package(OpenCV REQUIRED)
IF(OpenCV_FOUND)
	target_include_directories(main PRIVATE ${OpenCV_INCLUDE_DIRS})
	target_link_libraries(main PRIVATE ${OpenCV_LIBS})
ELSE(OpenCV_FOUND)
	MESSAGE(FATAL_ERROR "OpenCV library not found")
ENDIF(OpenCV_FOUND)

#VPL
find_package(VPL REQUIRED)
target_link_libraries(main PRIVATE VPL::dispatcher)

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
  set_property(TARGET main PROPERTY VS_DPI_AWARE "PerMonitor")
  copy_target_dlls(main)
#[[
# OpenCV
  target_include_directories(main PRIVATE ${CMAKE_SOURCE_DIR}/lib/OpenCV/include)

  add_library(OpenCV SHARED IMPORTED GLOBAL)
  set_property(TARGET OpenCV PROPERTY IMPORTED_IMPLIB_RELEASE "${CMAKE_SOURCE_DIR}/lib/OpenCV/x64/vc16/lib/opencv_world480.lib")
  set_property(TARGET OpenCV PROPERTY IMPORTED_LOCATION_RELEASE "${CMAKE_SOURCE_DIR}/lib/OpenCV/x64/vc16/bin/opencv_world480.dll")
  set_property(TARGET OpenCV PROPERTY IMPORTED_IMPLIB_DEBUG "${CMAKE_SOURCE_DIR}/lib/OpenCV/x64/vc16/lib/opencv_world480d.lib")
  set_property(TARGET OpenCV PROPERTY IMPORTED_LOCATION_DEBUG "${CMAKE_SOURCE_DIR}/lib/OpenCV/x64/vc16/bin/opencv_world480d.dll")
  set_property(TARGET OpenCV PROPERTY IMPORTED_IMPLIB_MINSIZEREL "${CMAKE_SOURCE_DIR}/lib/OpenCV/x64/vc16/lib/opencv_world480.lib")
  set_property(TARGET OpenCV PROPERTY IMPORTED_LOCATION_MINSIZEREL "${CMAKE_SOURCE_DIR}/lib/OpenCV/x64/vc16/bin/opencv_world480.dll")
  set_property(TARGET OpenCV PROPERTY IMPORTED_IMPLIB_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/lib/OpenCV/x64/vc16/lib/opencv_world480.lib")
  set_property(TARGET OpenCV PROPERTY IMPORTED_LOCATION_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/lib/OpenCV/x64/vc16/bin/opencv_world480.dll")
  add_custom_command(TARGET main POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different 
    $<TARGET_FILE:OpenCV> 
    $<TARGET_FILE_DIR:main> )

  target_link_libraries(main PRIVATE OpenCV)

  # vpl dispatcher copy dlls
  if(VPL_SHARED)
  find_package(VPL REQUIRED)
  add_custom_command(TARGET main POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different 
    $<TARGET_FILE:VPL::dispatcher> 
    $<TARGET_FILE_DIR:main> )
  endif(VPL_SHARED)
]]#
endif(WIN32)

# add test transcoder executable
add_subdirectory(test_transcode)
