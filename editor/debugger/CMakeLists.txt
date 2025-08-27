# 子目录 CMakeLists.txt

# 收集当前目录所有 *.cpp 源文件（CONFIGURE_DEPENDS 便于增删文件时自动触发重配）
file(GLOB LOCAL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 将源码附加到上层定义的可执行目标 godot
if(TARGET godot)
    target_sources(godot PRIVATE ${LOCAL_SOURCES})
else()
    message(FATAL_ERROR "Target 'godot' not found. Please add this subdirectory after defining the 'godot' target.")
endif()

# 递归处理 debug_adapter 子目录（若存在）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/debug_adapter/CMakeLists.txt")
    add_subdirectory(debug_adapter)
endif()