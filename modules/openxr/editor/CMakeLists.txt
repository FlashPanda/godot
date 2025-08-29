# 子目录 CMakeLists.txt —— 等价实现 SCsub 中：
#   env_openxr.add_source_files(module_obj, "*.cpp")
#   env.modules_sources += module_obj
#
# 用法说明：
# 1) 确保上层（父目录）已创建名为 godot 的目标（如 add_executable(godot ...) 或 add_library(godot ...)）。
# 2) 在父目录中通过 add_subdirectory(本目录) 将该模块加入构建。
# 3) 本文件会自动收集当前目录的所有 .cpp 并追加到 godot 目标中。

# 1) 校验父级目标是否存在（需在 add_subdirectory(本目录) 之前创建）
if(NOT TARGET godot)
    message(FATAL_ERROR "未找到目标 'godot'。请在 add_subdirectory(本目录) 之前定义该目标。")
endif()

# 2) 收集当前目录的 *.cpp 源文件
#    使用 CONFIGURE_DEPENDS 确保增删源文件时会触发 CMake 重新配置
file(GLOB OPENXR_MODULE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 3) 将模块源文件并入 godot 目标
#    等价于 SCons 中将 module_obj 追加到 env.modules_sources
if(OPENXR_MODULE_SOURCES)
    target_sources(godot PRIVATE ${OPENXR_MODULE_SOURCES})
endif()

# 如需为该模块追加专有的包含目录、宏、编译选项，可在此按需启用：
# target_include_directories(godot PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
# target_compile_definitions(godot PRIVATE GODOT_OPENXR)
# target_compile_options(godot PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W3> $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>)