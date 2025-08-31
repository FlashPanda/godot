# CMake 最低版本要求可由上层工程统一设置；此处为子目录 CMakeLists，不重复设置。
# 本文件的作用等价于 SCons 中：
#   env.add_source_files(env.scene_sources, "*.cpp")
#   SConscript("joints/SCsub")

# 说明：
# - 假定上层工程已创建名为 godot 的构建目标（可执行或库）。
# - 本文件会在上层通过 add_subdirectory(<该目录>) 被包含后生效。

# 收集当前目录中所有 .cpp 源文件
# 注意：file(GLOB ...) 在新增/删除文件时可能不会自动触发 CMake 重新配置，
# 如需严格追踪可改为手工维护列表，或在构建系统/IDE 中开启自动 reconfigure。
file(GLOB LOCAL_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 将本目录的源文件追加到上层已存在的目标 godot
# 若希望在 IDE 中分组，可结合 source_group 或 FILE_SET 使用。
target_sources(godot PRIVATE
        ${LOCAL_SOURCES}
)

# 链式加载子目录 joints，等价于 SConscript("joints/SCsub")
# 要求 joints 子目录下具备相应的 CMakeLists.txt。
add_subdirectory(joints)