# 子级 CMakeLists：不声明 project()，仅组织本目录与子目录源码

# 确保上层已定义目标 godot（可执行或库）
if (NOT TARGET godot)
    message(FATAL_ERROR "Target 'godot' not found. Please define it in the parent CMakeLists.txt before add_subdirectory.")
endif()

# 收集当前目录与 physics/ 下的 *.cpp
file(GLOB GLTF_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/physics/*.cpp"
)

if (GLTF_SOURCES STREQUAL "")
    message(WARNING "[modules/gltf] No .cpp sources found in: ${CMAKE_CURRENT_SOURCE_DIR} and physics/")
endif()

# 将这些源文件附加到上层目标 godot
target_sources(godot PRIVATE ${GLTF_SOURCES})