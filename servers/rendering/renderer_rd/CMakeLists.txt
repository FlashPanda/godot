# 本文件为“子目录”的构建脚本，不在此处设置 cmake_minimum_required()/project()。
# 目标：复刻 SCons 脚本的行为：
#  1) 收集当前目录下的所有 *.cpp 源文件，并把它们加入到上层已有目标。
#  2) 递归进入若干子目录进行构建（等价于多个 SConscript("xxx/SCsub")）。
#
# 使用方式（示例）：
#   上层应已创建目标，例如：
#     add_executable(godot ...)
#   并 add_subdirectory(本目录)。
#   如目标名不是 godot，可通过 -DGODOT_TARGET=自定义名 传入。

# 可配置项：上层已创建的构建目标名称
set(GODOT_TARGET "godot" CACHE STRING "上层已创建的构建目标名称")

# 校验目标是否存在，避免后续 target_sources() 报错
if(NOT TARGET ${GODOT_TARGET})
    message(FATAL_ERROR "未找到构建目标 ${GODOT_TARGET}。请在上层先创建该目标，或通过 -DGODOT_TARGET=... 指定。")
endif()

# 1) 收集当前目录下的所有 C++ 源文件，并加入到上层目标
#    等价于：env.add_source_files(env.servers_sources, "*.cpp")
file(GLOB LOCAL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

if(LOCAL_SOURCES)
    # 将源文件附加到既有目标，PRIVATE 表示仅此目标使用
    target_sources(${GODOT_TARGET} PRIVATE ${LOCAL_SOURCES})
endif()

# 2) 递归进入子目录（等价于 SConscript 调用）
#    这些子目录应各自包含自己的 CMakeLists.txt 以实现其内部逻辑。
set(_SUBDIRS
        effects
        environment
        forward_clustered
        forward_mobile
        deferred
        shaders
        spirv-reflect
        storage_rd
)

foreach(_dir IN LISTS _SUBDIRS)
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}")
        add_subdirectory("${_dir}")
    else()
        # 若有子目录暂时不存在，不报错，仅提示（与 SCons 中不存在的 SConscript 类似宽容）
        message(STATUS "子目录不存在，跳过：${_dir}")
    endif()
endforeach()