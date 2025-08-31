# 本文件为子目录的构建脚本，不在此处设置 cmake_minimum_required()/project()。
# 等效于 SCons 中的：
#   env.add_source_files(env.scene_sources, "*.cpp")
#   env.add_source_files(env.scene_sources, "skeleton/*.cpp")

# 1) 收集当前目录下所有 .cpp 源文件
file(GLOB LOCAL_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 2) 收集 skeleton 子目录下所有 .cpp 源文件
file(GLOB SKELETON_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/skeleton/*.cpp"
)

# 3) 将上述源文件加入到上层已定义的目标 godot
#    注意：
#    - 需要在更高一层的 CMake 脚本中已有：add_executable(godot ...) 或 add_library(godot ...)
#    - 若你的目标名不同，请将 godot 替换为实际目标名
target_sources(godot PRIVATE
        ${LOCAL_SOURCES}
        ${SKELETON_SOURCES}
)

# 4) 可选：为 IDE 分组，便于浏览（非必需）
# source_group("scene" FILES ${LOCAL_SOURCES})
# source_group("scene\\skeleton" FILES ${SKELETON_SOURCES})