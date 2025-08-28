# CMake

# 第三方 jpeg-compressor 源
set(THIRDPARTY_JPEG_DIR "${CMAKE_SOURCE_DIR}/thirdparty/jpeg-compressor")
set(THIRDPARTY_JPEG_SOURCES
        "${THIRDPARTY_JPEG_DIR}/jpgd.cpp"
        "${THIRDPARTY_JPEG_DIR}/jpge.cpp"
)

# 将第三方头文件加入 include path（等价于 Prepend CPPPATH）
target_include_directories(godot PRIVATE "${THIRDPARTY_JPEG_DIR}")

# 禁用第三方源码告警（等价于 env_thirdparty.disable_warnings()）
if(MSVC)
    set_source_files_properties(${THIRDPARTY_JPEG_SOURCES} PROPERTIES COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(${THIRDPARTY_JPEG_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endif()

# 当前模块自身的源文件（等价于 add_source_files(module_obj, "*.cpp")）
file(GLOB MODULE_LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 汇总加入到已有目标 godot（等价于 env.modules_sources += ...）
target_sources(godot PRIVATE
        ${THIRDPARTY_JPEG_SOURCES}
        ${MODULE_LOCAL_SOURCES}
)