# 确保上层已定义目标 godot（例如 add_executable(godot ...) 或 add_library(godot ...)）
if (NOT TARGET godot)
    message(FATAL_ERROR "Target 'godot' not found. Please define it in the parent CMakeLists.txt before add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}).")
endif()

# 收集当前目录与 structures/ 下的源文件
file(GLOB LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/structures/*.cpp"
)

# 根据是否开启编辑器构建，追加 editor/ 下的源文件
# 兼容多种上层变量命名：GODOT_EDITOR_BUILD / GODOT_BUILD_EDITOR / EDITOR_BUILD
if (DEFINED GODOT_EDITOR_BUILD OR DEFINED GODOT_BUILD_EDITOR OR DEFINED EDITOR_BUILD)
    if (GODOT_EDITOR_BUILD OR GODOT_BUILD_EDITOR OR EDITOR_BUILD)
        file(GLOB EDITOR_SOURCES CONFIGURE_DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp"
        )
        list(APPEND LOCAL_SOURCES ${EDITOR_SOURCES})
    endif()
endif()

# 将收集到的源文件加入 godot 目标
if (LOCAL_SOURCES)
    target_sources(godot PRIVATE ${LOCAL_SOURCES})
endif()

# 递归进入 extensions 子目录（若存在 CMakeLists.txt）
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extensions/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/extensions")
endif()