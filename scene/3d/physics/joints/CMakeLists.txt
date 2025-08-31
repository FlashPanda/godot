# 该 CMakeLists.txt 位于子目录，用于将本目录下的所有 *.cpp
# 源文件添加到上层已存在的目标 godot 中，等价于 SCons 中
# 将本目录源追加到 env.scene_sources 的效果。

# 说明：
# - 要求上层 CMake 工程已经创建了名为 godot 的目标（如可执行文件）。
# - 本文件被上层通过 add_subdirectory(<该目录>) 包含后生效。

# 收集当前目录下的所有 .cpp 源文件
# 注意：GLOB 的结果在新增/删除文件时不会自动触发 CMake 重新配置，
# 如需更严格的可追踪性可改为手工维护列表。
file(GLOB LOCAL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 将本目录源文件追加到 godot 目标
# 若希望在 IDE 中清晰可见，可配合 source_group 或 target_sources 的 FILE_SET 使用。
target_sources(godot PRIVATE
        ${LOCAL_SOURCES}
)

# 可选：在需要时，也可以为这些源设置专属的编译选项或包含目录，例如：
# target_compile_options(godot PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall>)
# target_include_directories(godot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})