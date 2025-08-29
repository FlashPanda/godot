# cmake

# -----------------------------------------------------------------------------
# 0) 选择父级目标名称
#    - 默认父级目标名为 godot（可执行/库）。
#    - 如父级使用不同目标名，可在 add_subdirectory 前设置 GODOT_PARENT_TARGET 覆盖。
if (NOT DEFINED GODOT_PARENT_TARGET)
    set(GODOT_PARENT_TARGET godot)
endif()

if (NOT TARGET ${GODOT_PARENT_TARGET})
    message(FATAL_ERROR
            "未找到父级目标 '${GODOT_PARENT_TARGET}'。请先在父级 CMakeLists.txt 中定义该目标，"
            "再 add_subdirectory 进入本目录，或通过 set(GODOT_PARENT_TARGET <目标名>) 指定。")
endif()

# -----------------------------------------------------------------------------
# 1) 选项与平台判定
#    - builtin_miniupnpc: 是否内置并编译 thirdparty/miniupnpc（默认开启）
#    - “web” 平台在 SCons 中会禁用 miniupnpc，这里按 Emscripten 进行判定：
#        CMAKE_SYSTEM_NAME STREQUAL "Emscripten" 或 用户显式指定 GODOT_PLATFORM=web
if (NOT DEFINED builtin_miniupnpc)
    option(builtin_miniupnpc "Build bundled miniupnpc from thirdparty/miniupnpc" ON)
endif()

# 允许用户通过 -DGODOT_PLATFORM=web 显式指定
set(IS_WEB FALSE)
if (DEFINED GODOT_PLATFORM AND GODOT_PLATFORM STREQUAL "web")
    set(IS_WEB TRUE)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(IS_WEB TRUE)
endif()

# Windows 判定：用于是否添加 MINIUPNPC_SET_SOCKET_TIMEOUT 宏
set(IS_WINDOWS FALSE)
if (WIN32)
    set(IS_WINDOWS TRUE)
endif()

# -----------------------------------------------------------------------------
# 2) 模块源码（等价 SCons: env_upnp.add_source_files(module_obj, "*.cpp")）
file(GLOB MODULE_LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

if (MODULE_LOCAL_SOURCES)
    target_sources(${GODOT_PARENT_TARGET} PRIVATE ${MODULE_LOCAL_SOURCES})
endif()

# -----------------------------------------------------------------------------
# 3) thirdparty/miniupnpc（与 SCons 逻辑一致：仅在 builtin_miniupnpc=ON 且非 web 平台时启用）
if (builtin_miniupnpc AND NOT IS_WEB)
    # 3.1 thirdparty 源与包含目录
    set(MINIUPNPC_THIRDPARTY_DIR "${PROJECT_SOURCE_DIR}/thirdparty/miniupnpc")

    # 与 SCsub 中启用的清单保持一致（位于 thirdparty/miniupnpc/src/）
    set(MINIUPNPC_SOURCES
            "${MINIUPNPC_THIRDPARTY_DIR}/src/igd_desc_parse.c"
            "${MINIUPNPC_THIRDPARTY_DIR}/src/miniupnpc.c"
            "${MINIUPNPC_THIRDPARTY_DIR}/src/minixml.c"
            "${MINIUPNPC_THIRDPARTY_DIR}/src/minisoap.c"
            "${MINIUPNPC_THIRDPARTY_DIR}/src/minissdpc.c"
            "${MINIUPNPC_THIRDPARTY_DIR}/src/miniwget.c"
            "${MINIUPNPC_THIRDPARTY_DIR}/src/upnpcommands.c"
            "${MINIUPNPC_THIRDPARTY_DIR}/src/upnpdev.c"
            "${MINIUPNPC_THIRDPARTY_DIR}/src/upnpreplyparse.c"
            "${MINIUPNPC_THIRDPARTY_DIR}/src/connecthostport.c"
            "${MINIUPNPC_THIRDPARTY_DIR}/src/portlistingparse.c"
            "${MINIUPNPC_THIRDPARTY_DIR}/src/receivedata.c"
            "${MINIUPNPC_THIRDPARTY_DIR}/src/addr_is_reserved.c"
    )

    # 3.2 包含目录（等价于 Prepend(CPPPATH)）
    target_include_directories(${GODOT_PARENT_TARGET} PRIVATE
            "${MINIUPNPC_THIRDPARTY_DIR}/include"          # 主包含目录
            "${MINIUPNPC_THIRDPARTY_DIR}/include/miniupnpc" # 一些头文件的二级包含路径
    )

    # 3.3 宏定义（等价于 Append(CPPDEFINES)）
    #      - 总是定义 MINIUPNP_STATICLIB
    #      - 非 Windows 额外定义 MINIUPNPC_SET_SOCKET_TIMEOUT
    target_compile_definitions(${GODOT_PARENT_TARGET} PRIVATE
            MINIUPNP_STATICLIB
    )
    if (NOT IS_WINDOWS)
        target_compile_definitions(${GODOT_PARENT_TARGET} PRIVATE
                MINIUPNPC_SET_SOCKET_TIMEOUT
        )
    endif()

    # 3.4 将 thirdparty 源并入同一目标，以在 thirdparty 更新时触发正确的重建
    target_sources(${GODOT_PARENT_TARGET} PRIVATE ${MINIUPNPC_SOURCES})

    # 3.5 尽可能关闭 thirdparty 源的编译告警（模拟 env_thirdparty.disable_warnings()）
    if (MSVC)
        set_source_files_properties(${MINIUPNPC_SOURCES}
                PROPERTIES COMPILE_OPTIONS "/w")
    else()
        set_source_files_properties(${MINIUPNPC_SOURCES}
                PROPERTIES COMPILE_OPTIONS "-w")
    endif()
endif()