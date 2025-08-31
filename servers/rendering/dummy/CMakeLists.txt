# 说明：
# 1) 本 CMakeLists.txt 作为子目录使用，父级应已定义目标 `godot` 并通过 add_subdirectory() 引入当前目录。
# 2) 等价于 SCons 脚本的行为：
#    - env.add_source_files(env.servers_sources, "*.cpp")
#    - SConscript("storage/SCsub")

# 收集当前目录下所有 C++ 源文件（匹配 "*.cpp"）
# 注意：file(GLOB ...) 在增删源文件时可能不会自动触发 CMake 重新配置，
# 若需严格追踪，可改为手工维护列表或在工具链中启用自动 reconfigure。
file(GLOB LOCAL_SERVERS_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 将这些源文件添加到父级目标 `godot`
# 要求在更高一层的 CMake 中已创建 TARGET(godot)
if(TARGET godot)
    target_sources(godot PRIVATE ${LOCAL_SERVERS_SOURCES})

    # 可选：在 IDE 中分组，便于浏览（标记为“servers”分组）
    if(LOCAL_SERVERS_SOURCES)
        source_group("servers" FILES ${LOCAL_SERVERS_SOURCES})
    endif()
else()
    message(FATAL_ERROR "未找到目标 'godot'。请在父级 CMake 中先创建该目标后，再 add_subdirectory 引入本目录。")
endif()

# 递归处理 storage 子目录（等价于 SConscript(\"storage/SCsub\")）
# 若子目录不存在或尚未提供 CMakeLists.txt，则跳过，保证健壮性
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/storage/CMakeLists.txt")
    add_subdirectory(storage)
endif()