# 本文件为子目录的构建脚本，不在此处设置 cmake_minimum_required()/project()。

# 目标名约定：
# - 默认将当前目录下的所有 *.cpp 源加入到名为 godot 的上层目标中。
# - 如上层目标名不同，可在进入本子目录前通过变量 GODOT_TARGET 指定目标名：
#     set(GODOT_TARGET your_target_name)
#     add_subdirectory(this_dir)
# - 若既没有名为 godot 的目标，也没有设置 GODOT_TARGET，则会报错。

# 1) 解析目标名
set(_GODOT_TARGET "godot")
if(DEFINED GODOT_TARGET)
    set(_GODOT_TARGET "${GODOT_TARGET}")
endif()

# 2) 校验目标是否存在
if(NOT TARGET "${_GODOT_TARGET}")
    message(FATAL_ERROR
            "未找到上层构建目标 \"${_GODOT_TARGET}\"。"
            "\n请在上层 CMake 中先创建目标（如 add_executable 或 add_library），"
            "并确保在其后调用 add_subdirectory(本目录)。"
            "\n如目标名不同，请在进入本目录前设置变量 GODOT_TARGET。")
endif()

# 3) 收集当前目录下所有 C++ 源文件（等价于 SCons: env.add_source_files(env.servers_sources, \"*.cpp\")）
#    说明：
#    - 使用 GLOB 便于与原有通配行为对应；若希望更严格的可维护性，也可改为手工维护列表。
file(GLOB LOCAL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 4) 将源文件加入到上层已创建的目标
#    PRIVATE 表示这些源仅参与该目标的编译，不影响其接口。
target_sources("${_GODOT_TARGET}" PRIVATE
        ${LOCAL_SOURCES}
)

# 提示：
# - 若本目录没有 *.cpp，LOCAL_SOURCES 为空不会报错，上层目标不会添加任何新源文件。
# - 依赖关系与增量构建由 CMake/生成器自动处理，无需额外手工声明。