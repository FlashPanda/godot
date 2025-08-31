# 说明：
# 1. 本 CMakeLists.txt 作为子目录使用，父级应已定义目标 `godot` 并通过 add_subdirectory() 引入当前目录。
# 2. 功能等价于 SCons 中：
#    - env.add_source_files(env.scene_sources, "*.cpp")
#    - SConscript("joints/SCsub")

# 收集当前目录下的所有 C++ 源文件（等价于匹配 "*.cpp"）
file(GLOB LOCAL_SCENE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 将这些源文件附加到父级可执行目标/库 `godot`
# 注意：要求在更高一层的 CMake 中已创建 TARGET(godot)
if(TARGET godot)
    target_sources(godot PRIVATE ${LOCAL_SCENE_SOURCES})

    # 可选：为 IDE 视图分组，便于浏览（类似“scene”分组语义）
    if(LOCAL_SCENE_SOURCES)
        source_group("scene" FILES ${LOCAL_SCENE_SOURCES})
    endif()
else()
    message(FATAL_ERROR "目标 'godot' 未在父级 CMake 中定义，请先在上层创建该目标后再 add_subdirectory 当前目录。")
endif()

# 递归处理 joints 子目录（等价于 SConscript(\"joints/SCsub\")）
# 如不存在则忽略，保持构建脚本的健壮性
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/joints/CMakeLists.txt")
    add_subdirectory(joints)
endif()