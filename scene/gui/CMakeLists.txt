# 本文件为子目录的构建脚本，不在此处设置 cmake_minimum_required()/project()。

# 1) 收集当前目录下的所有 C++ 源文件（对应 SCons 的：env.add_source_files(env.scene_sources, "*.cpp")）
file(GLOB LOCAL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 2) 将这些源码追加到上层已创建的目标 godot
#    注意：
#    - 需要在更高层的 CMake 脚本中已有：add_executable(godot ...) 或 add_library(godot ...)
#    - 若目标名不同，请将 godot 替换为实际目标名
target_sources(godot PRIVATE
        ${LOCAL_SOURCES}
)