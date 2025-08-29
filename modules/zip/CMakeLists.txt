# 子目录 CMakeLists.txt —— 等价实现 SCsub 中：
#   env_zip.add_source_files(env.modules_sources, "*.cpp")
#
# 用法：
# 1) 父目录需先定义名为 godot 的目标（如 add_executable(godot ...) 或 add_library(godot ...)）。
# 2) 在父目录中通过 add_subdirectory(本目录) 添加本模块。
# 3) 本文件将自动收集当前目录下所有 *.cpp 并并入 godot 目标的源码中。

# 1) 校验父级目标是否存在（必须在 add_subdirectory(本目录) 之前创建）
if(NOT TARGET godot)
    message(FATAL_ERROR "未找到目标 'godot'。请在 add_subdirectory(本目录) 之前定义该目标。")
endif()

# 2) 收集当前目录的 *.cpp 源文件（等价于 add_source_files(..., \"*.cpp\")）
#    使用 CONFIGURE_DEPENDS 使新增/删除源文件时可触发 CMake 重新配置
file(GLOB ZIP_MODULE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 3) 将模块源文件并入上层目标 godot（等价于将 module_obj 追加到 env.modules_sources）
if(ZIP_MODULE_SOURCES)
    target_sources(godot PRIVATE ${ZIP_MODULE_SOURCES})
endif()