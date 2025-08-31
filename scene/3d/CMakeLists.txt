# 该 CMakeLists.txt 位于子目录，功能等价于 SCons 中：
#   env.add_source_files(env.scene_sources, "*.cpp")
#   SConscript("physics/SCsub")
#
# 说明：
# - 假定上层工程已先创建名为 godot 的目标（可执行或库），并通过 add_subdirectory()
#   引入了本目录。
# - 若目标名不同，请将下面的 GODOT_TARGET 修改为你的真实目标名。

# 可配置的目标名（必要时修改）
set(GODOT_TARGET godot)

# 基本的健壮性检查：确保上层已创建该目标
if(NOT TARGET ${GODOT_TARGET})
    message(FATAL_ERROR "未找到目标 ${GODOT_TARGET}。请在上层先创建目标，再 add_subdirectory 引入本目录。")
endif()

# 收集当前目录中的所有 .cpp 源文件
# 注意：file(GLOB ...) 结果在新增/删除文件时未必会自动触发 CMake 重新配置；
# 若需严格可追踪性，可改为手动维护列表，或在 IDE/构建系统开启自动 reconfigure。
file(GLOB LOCAL_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 将本目录源文件追加到上层目标
# 若需要在 IDE 中进行分组，可配合 source_group/FILE_SET 使用。
target_sources(${GODOT_TARGET} PRIVATE
        ${LOCAL_SOURCES}
)

# 链式包含 physics 子目录（等价于 SConscript("physics/SCsub")）
# 该子目录应包含自己的 CMakeLists.txt 来完成对应的构建规则。
add_subdirectory(physics)