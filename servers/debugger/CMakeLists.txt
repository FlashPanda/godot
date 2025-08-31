# 本文件为子目录的构建脚本：
# - 不在此处调用 cmake_minimum_required()/project()；
# - 仅负责收集当前目录下的源文件并追加到上层已有目标中。
#
# 该文件实现与 SCons 中：
#   env.add_source_files(env.servers_sources, "*.cpp")
# 等效的功能，即：收集本目录所有 .cpp 源并加入“服务器（servers）相关”的汇总编译目标。

# 1) 收集当前目录下的所有 C++ 源文件（匹配 *.cpp）
#    使用绝对路径便于更可靠地加入到目标中。
file(GLOB LOCAL_SERVERS_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 2) 将这些源码追加到上层已创建的目标 godot
#    注意：
#    - 需要上层 CMake 中已存在 add_executable(godot ...) 或 add_library(godot ...)
#    - 如果你的主目标名并非 godot，请替换为实际目标名
#    - 使用 PRIVATE 表示这些源文件仅在该目标内部可见
target_sources(godot PRIVATE
        ${LOCAL_SERVERS_SOURCES}
)

# 3) 可选：如需在调试时查看收集到的文件，可取消下一行注释以打印列表
# message(STATUS "Servers sources in ${CMAKE_CURRENT_SOURCE_DIR}: ${LOCAL_SERVERS_SOURCES}")