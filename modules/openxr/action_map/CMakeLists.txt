# 子目录 CMakeLists.txt —— 等价实现 SCsub 中：
#   env_openxr.add_source_files(module_obj, "*.cpp")
#   env.modules_sources += module_obj
#
# 作用：将本目录所有 *.cpp 源文件并入上层已定义的目标 godot。

# 1) 确认上层已定义目标 godot（需要在 add_subdirectory(本目录) 之前创建）
if(NOT TARGET godot)
    message(FATAL_ERROR "未找到目标 'godot'。请在 add_subdirectory(本目录) 之前定义该目标。")
endif()

# 2) 收集当前目录 *.cpp 源文件
#    使用 CONFIGURE_DEPENDS，可在新增/删除源文件时自动触发 CMake 重新配置
file(GLOB OPENXR_MODULE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 3) 将收集到的模块源文件并入 godot 目标
#    等价于 SCons 中对 module_obj 的累加并追加到 env.modules_sources
if(OPENXR_MODULE_SOURCES)
    target_sources(godot PRIVATE ${OPENXR_MODULE_SOURCES})
endif()

# 如需额外的包含目录/宏/编译选项（例如 OpenXR 特定设置），
# 可在此处按需追加（示例，注释掉，按需启用）：
# target_include_directories(godot PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
# target_compile_definitions(godot PRIVATE GODOT_OPENXR)
# target_compile_options(godot PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W3> $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>)