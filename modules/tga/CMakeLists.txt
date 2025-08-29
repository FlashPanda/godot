# 子目录 CMakeLists：不调用 project()，仅把本目录源码并入父级目标。
# 该文件应放在与 SCsub 相同的目录下使用。

# 1) 确保父级已先定义最终链接目标（可执行或库），这里默认目标名为 godot。
#    如果你的父级使用了不同目标名，可在 add_subdirectory 之前定义变量
#    GODOT_PARENT_TARGET 来覆盖（例如 set(GODOT_PARENT_TARGET my_godot)）。
if (NOT DEFINED GODOT_PARENT_TARGET)
    set(GODOT_PARENT_TARGET godot)
endif()

if (NOT TARGET ${GODOT_PARENT_TARGET})
    message(FATAL_ERROR
            "未找到目标 '${GODOT_PARENT_TARGET}'。请在父级 CMakeLists.txt 中先定义该目标，"
            "再 add_subdirectory 进入本目录，或通过设置 GODOT_PARENT_TARGET 指定目标名。"
    )
endif()

# 2) 收集当前目录下全部 .cpp 源文件（等价于 SCons 中的 add_source_files(env.modules_sources, \"*.cpp\")）
file(GLOB LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 3) 将这些源文件追加到父级目标，作用类似于把本模块源码并入统一构建。
if (LOCAL_SOURCES)
    target_sources(${GODOT_PARENT_TARGET} PRIVATE ${LOCAL_SOURCES})
endif()