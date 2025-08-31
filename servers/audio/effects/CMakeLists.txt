# 本文件为“子目录”的 CMake 构建脚本，不在此处设置 cmake_minimum_required()/project()。
# 目标：等价于 SCons 中的：env.add_source_files(env.servers_sources, "*.cpp")
# 做法：收集当前目录下的所有 .cpp 文件，并将其追加到上层已创建的目标（默认：godot）。

# 可选：允许上层在 add_subdirectory 之前通过 -D 或在父 CMakeLists.txt 中设定要追加的目标名。
# 例如在父级：
#   set(GODOT_PARENT_TARGET godot)  # 或者你的实际目标名
if(NOT DEFINED GODOT_PARENT_TARGET)
    set(GODOT_PARENT_TARGET godot)  # 默认目标名
endif()

# 1) 收集本目录所有 C++ 源文件（与 "*.cpp" 匹配）
file(GLOB LOCAL_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 2) 追加到父目标
# 说明：
# - 要求父级 CMake 已创建目标 add_executable(godot ...) 或 add_library(godot ...)。
# - 若你的工程中目标名不同，请在父级通过 set(GODOT_PARENT_TARGET your_target) 覆盖默认值。
if(NOT TARGET ${GODOT_PARENT_TARGET})
    message(FATAL_ERROR
            "未找到目标 ${GODOT_PARENT_TARGET}。请在父级先创建该目标或通过变量 GODOT_PARENT_TARGET 指定正确的目标名。")
endif()

target_sources(${GODOT_PARENT_TARGET} PRIVATE
        ${LOCAL_SOURCES}
)

# 注：
# - 若该子目录日后引入第三方代码并需要特殊编译选项，可在此添加 set_source_files_properties()
# - CMake 会自动追踪源文件时间戳变化，不需要显式的“Depends”来强制重编。