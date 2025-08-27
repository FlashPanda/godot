# 子目录 CMakeLists：将本目录与 3d/ 下的所有 .cpp 直接添加到目标 godot

# 确保上层已定义目标 godot
if (NOT TARGET godot)
    message(FATAL_ERROR "Target 'godot' not found. Please define it in the parent CMakeLists.txt before add_subdirectory.")
endif()

# 收集源文件
file(GLOB CONFIGURE_DEPENDS
        LOCAL_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/3d/*.cpp"
)

if (LOCAL_SOURCES STREQUAL "")
    message(WARNING "[editor subdir] No .cpp sources found in: ${CMAKE_CURRENT_SOURCE_DIR} and 3d/")
endif()

# 直接把这些源附加到可执行目标 godot
target_sources(godot PRIVATE
        ${LOCAL_SOURCES}
)
