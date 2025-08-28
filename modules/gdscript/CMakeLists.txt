# CMakeLists.txt — 子目录：gdscript 模块等效于 SCsub 的逻辑

cmake_minimum_required(VERSION 3.16)

# 确保父级已定义目标 godot
if (NOT TARGET godot)
    message(FATAL_ERROR "Target 'godot' not found. Define it in parent CMakeLists.txt before add_subdirectory.")
endif()

# 选项与开关（对应 SCsub 中的 env.flags）
option(GODOT_EDITOR_BUILD "Enable editor-related sources for this module" ON)
option(GODOT_TESTS "Enable tests for this module" OFF)
option(MODULE_JSONRPC_ENABLED "Enable JSON-RPC module (for LSP)" ON)
option(MODULE_WEBSOCKET_ENABLED "Enable WebSocket module (for LSP)" ON)

# 1) 基础源码：*.cpp
file(GLOB CONFIGURE_DEPENDS
        GDSCRIPT_LOCAL_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 2) editor 相关
set(GDSCRIPT_EDITOR_SOURCES "")
if (GODOT_EDITOR_BUILD)
    file(GLOB CONFIGURE_DEPENDS
            GDSCRIPT_EDITOR_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp"
    )

    # 等效于 SConscript("editor/script_templates/SCsub")：如果该子目录含 CMakeLists，则加入
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/editor/script_templates/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/editor/script_templates" "${CMAKE_CURRENT_BINARY_DIR}/editor/script_templates")
    endif()

    # LSP：只有在 jsonrpc 与 websocket 均启用时才包含 language_server/*.cpp；否则加宏 GDSCRIPT_NO_LSP
    if (MODULE_JSONRPC_ENABLED AND MODULE_WEBSOCKET_ENABLED)
        file(GLOB CONFIGURE_DEPENDS
                GDSCRIPT_LSP_SOURCES
                "${CMAKE_CURRENT_SOURCE_DIR}/language_server/*.cpp"
        )
    else()
        # 在“模块自身”和“主环境”都定义 GDSCRIPT_NO_LSP —— 这里统一加到 godot 目标
        target_compile_definitions(godot PRIVATE GDSCRIPT_NO_LSP)
        set(GDSCRIPT_LSP_SOURCES "")
    endif()
else()
    set(GDSCRIPT_LSP_SOURCES "")
endif()

# 3) tests 相关
set(GDSCRIPT_TEST_SOURCES "")
if (GODOT_TESTS)
    target_compile_definitions(godot PRIVATE TESTS_ENABLED)
    file(GLOB CONFIGURE_DEPENDS
            GDSCRIPT_TEST_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    )
endif()

# 4) 汇总并添加到目标 godot
target_sources(godot PRIVATE
        ${GDSCRIPT_LOCAL_SOURCES}
        ${GDSCRIPT_EDITOR_SOURCES}
        ${GDSCRIPT_LSP_SOURCES}
        ${GDSCRIPT_TEST_SOURCES}
)

# 可选提示
if (GODOT_EDITOR_BUILD)
    if (NOT (MODULE_JSONRPC_ENABLED AND MODULE_WEBSOCKET_ENABLED))
        message(STATUS "[gdscript] LSP disabled -> GDSCRIPT_NO_LSP defined")
    endif()
endif()