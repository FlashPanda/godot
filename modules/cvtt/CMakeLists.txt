# CMakeLists.txt

# 确认上层已定义目标 godot
if(NOT TARGET godot)
    message(FATAL_ERROR "Target 'godot' not found. Please add this subdirectory after defining the 'godot' target.")
endif()

# 1) 模块本目录源码（等价于 env_cvtt.add_source_files(module_obj, \"*.cpp\")）
file(GLOB MODULE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 2) 第三方 cvtt 源码（等价于显式列表 + add_source_files）
set(CVTT_DIR "${CMAKE_SOURCE_DIR}/thirdparty/cvtt")
set(CVTT_SOURCES
        "${CVTT_DIR}/ConvectionKernels_API.cpp"
        "${CVTT_DIR}/ConvectionKernels_ETC.cpp"
        "${CVTT_DIR}/ConvectionKernels_BC67.cpp"
        "${CVTT_DIR}/ConvectionKernels_IndexSelector.cpp"
        "${CVTT_DIR}/ConvectionKernels_BC6H_IO.cpp"
        "${CVTT_DIR}/ConvectionKernels_S3TC.cpp"
        "${CVTT_DIR}/ConvectionKernels_BC7_PrioData.cpp"
        "${CVTT_DIR}/ConvectionKernels_SingleFile.cpp"
        "${CVTT_DIR}/ConvectionKernels_BCCommon.cpp"
        "${CVTT_DIR}/ConvectionKernels_Util.cpp"
)

# 3) 将第三方与模块源码一同添加到 godot 目标
target_sources(godot PRIVATE
        ${MODULE_SOURCES}
        ${CVTT_SOURCES}
)

# 4) 为第三方源码添加包含目录（等价于 env_cvtt.Prepend(CPPPATH=[thirdparty_dir])）
target_include_directories(godot PRIVATE
        "${CVTT_DIR}"
)

# 5) 关闭第三方源码的编译警告（等价于 env_thirdparty.disable_warnings()）
#    MSVC 使用 /w；GCC/Clang 使用 -w
set_source_files_properties(${CVTT_SOURCES} PROPERTIES
        COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/w>$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>"
)