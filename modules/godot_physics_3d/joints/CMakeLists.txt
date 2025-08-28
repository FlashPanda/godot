# 确保上层已定义目标（例如 add_executable(godot ...) 或 add_library(godot ...)）
if (NOT TARGET godot)
    message(FATAL_ERROR "未找到目标 'godot'。请在上层 CMakeLists.txt 中先定义该目标后，再 add_subdirectory 到本目录。")
endif()

# 收集本目录下的所有 .cpp 源文件
file(GLOB LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 将源文件追加到 godot 目标
if (LOCAL_SOURCES)
    target_sources(godot PRIVATE ${LOCAL_SOURCES})
endif()