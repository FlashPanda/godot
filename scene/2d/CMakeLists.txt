# 说明：
# - 本 CMakeLists.txt 设计为子目录使用，父级应已创建目标 `godot` 并通过 add_subdirectory() 引入本目录。
# - 等价于 SCons 脚本的行为：
#   1) env.add_source_files(env.scene_sources, "*.cpp")
#   2) SConscript("physics/SCsub")

# 收集当前目录下所有 C++ 源文件（匹配 "*.cpp"）
file(GLOB LOCAL_SCENE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 将这些源文件添加到父级目标 `godot`
if(TARGET godot)
    target_sources(godot PRIVATE ${LOCAL_SCENE_SOURCES})

    # 可选：在 IDE 中分组，便于查看
    if(LOCAL_SCENE_SOURCES)
        source_group("scene" FILES ${LOCAL_SCENE_SOURCES})
    endif()
else()
    message(FATAL_ERROR "未找到目标 'godot'。请在父级 CMake 中先创建该目标后，再 add_subdirectory 引入本目录。")
endif()

# 递归处理 physics 子目录（等价于 SConscript(\"physics/SCsub\")）
# 若子目录不存在则跳过，保证健壮性
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/physics/CMakeLists.txt")
    add_subdirectory(physics)
endif()