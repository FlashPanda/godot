# 子目录 CMakeLists.txt —— 等价实现 SCsub（收集本目录 *.cpp 并加入上层目标 godot）

# 说明：
# - 本文件应在上层 CMakeLists.txt 中完成 add_executable(godot ...) 或 add_library(godot ...) 后，
#   通过 add_subdirectory(...) 引入。
# - 该文件等价于 SCons 中：
#     env_openxr.add_source_files(module_obj, "*.cpp")
#     env.modules_sources += module_obj
#   即将本目录的所有 .cpp 源并入主目标的编译。

# 确认上层已定义目标 godot，若没有则报错提示引入顺序有误。
if(NOT TARGET godot)
    message(FATAL_ERROR "未找到目标 'godot'。请在定义/创建 'godot' 目标之后再 add_subdirectory 到当前目录。")
endif()

# 1) 收集当前目录下的所有 .cpp 源文件（等价于 SCons 的 add_source_files(module_obj, \"*.cpp\")）
#    使用 CONFIGURE_DEPENDS 使 CMake 在增删源文件时能够触发重新配置。
file(GLOB MODULE_SOURCES
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 2) 将收集到的模块源文件附加到上层目标 godot
#    这与 SCons 的 env.modules_sources += module_obj 的效果等价：参与主目标的编译/链接。
if(MODULE_SOURCES)
    target_sources(godot PRIVATE ${MODULE_SOURCES})
endif()

# 如需后续扩展（例如根据平台或开关条件追加特定源文件），可参考以下示例：
# if(ANDROID)
#     list(APPEND MODULE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/platform/specific_android.cpp")
# endif()
# target_sources(godot PRIVATE ${MODULE_SOURCES})