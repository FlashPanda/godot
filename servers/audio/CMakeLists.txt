# 本文件为子目录构建脚本，不在此处设置 cmake_minimum_required()/project()。

# 1) 收集本目录下所有 C++ 源文件
#    对应 SCons: env.add_source_files(env.servers_sources, "*.cpp")
file(GLOB LOCAL_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 2) 将这些源码追加到上层已创建的目标
#    注意：
#    - 请确保在更高层 CMake 中已经创建了名为 godot 的目标（add_executable 或 add_library）。
#    - 若目标名不同，请将 godot 替换为实际目标名。
target_sources(godot PRIVATE
        ${LOCAL_SOURCES}
)

# 3) 递归构建 effects 子目录
#    对应 SCons: SConscript("effects/SCsub")
#    要求 effects 子目录内也有自己的 CMakeLists.txt。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/effects/CMakeLists.txt")
    add_subdirectory(effects)
endif()