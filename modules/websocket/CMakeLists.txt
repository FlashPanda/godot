# CMake 子目录配置（websocket 模块）
# 等价于 SCons SCsub 的构建逻辑：
# - 若平台为 web（Emscripten）：仅添加 JS 接口库 library_godot_websocket.js；
# - 否则若启用内置 wslay：编译 thirdparty/wslay 源并定义相关宏；
# - 始终编译本目录下的 *.cpp；若为编辑器构建，还编译 editor/*.cpp。

# ----------------------------------------
# 选项与父级变量兼容
# ----------------------------------------
# 是否为编辑器构建（对应 env.editor_build）
option(WEBSOCKET_EDITOR_BUILD "Build websocket module with editor sources" OFF)
if(DEFINED GODOT_EDITOR_BUILD) # 兼容父级变量
    set(WEBSOCKET_EDITOR_BUILD "${GODOT_EDITOR_BUILD}")
endif()

# 是否使用内置 wslay（对应 env["builtin_wslay"]）
option(WEBSOCKET_BUILTIN_WSLAY "Build websocket module with bundled wslay" ON)

# ----------------------------------------
# 本模块源文件
# ----------------------------------------
file(GLOB WEBSOCKET_LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

if(WEBSOCKET_EDITOR_BUILD)
    file(GLOB WEBSOCKET_EDITOR_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp"
    )
endif()

# 将本模块源合并
set(_WEBSOCKET_ALL_LOCAL_SOURCES
        ${WEBSOCKET_LOCAL_SOURCES}
        ${WEBSOCKET_EDITOR_SOURCES}
)

# ----------------------------------------
# 平台为 Web（Emscripten）时，追加 JS 接口库
# 对应：env.AddJSLibraries(["library_godot_websocket.js"])
# ----------------------------------------
set(_WEBSOCKET_LINK_OPTIONS "") # 兼容父级尚未创建目标的情况
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(_WS_JS_LIB "${CMAKE_CURRENT_SOURCE_DIR}/library_godot_websocket.js")
    if(EXISTS "${_WS_JS_LIB}")
        list(APPEND _WEBSOCKET_LINK_OPTIONS "SHELL:--js-library ${_WS_JS_LIB}")
    else()
        message(WARNING "[websocket] 未找到 JS 库文件：${_WS_JS_LIB}")
    endif()
endif()

# ----------------------------------------
# 非 Web 且启用内置 wslay：第三方库集成
# 对应：thirdparty/wslay + 宏定义 + 关闭告警
# ----------------------------------------
set(_WEBSOCKET_TP_SOURCES "")
set(_WEBSOCKET_TP_INCLUDES "")
set(_WEBSOCKET_TP_DEFINES "")
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten" AND WEBSOCKET_BUILTIN_WSLAY)
    set(_WS_TP_DIR "${CMAKE_SOURCE_DIR}/thirdparty/wslay")
    set(_WEBSOCKET_TP_INCLUDES "${_WS_TP_DIR}")
    set(_WEBSOCKET_TP_DEFINES "HAVE_CONFIG_H")

    set(_WEBSOCKET_TP_SOURCES
            "${_WS_TP_DIR}/wslay_net.c"
            "${_WS_TP_DIR}/wslay_event.c"
            "${_WS_TP_DIR}/wslay_queue.c"
            "${_WS_TP_DIR}/wslay_frame.c"
    )

    # 平台相关宏：Windows 使用 HAVE_WINSOCK2_H，否则使用 HAVE_NETINET_IN_H
    if(WIN32)
        list(APPEND _WEBSOCKET_TP_DEFINES "HAVE_WINSOCK2_H")
    else()
        list(APPEND _WEBSOCKET_TP_DEFINES "HAVE_NETINET_IN_H")
    endif()

    # 关闭第三方源码告警
    if(MSVC)
        set_source_files_properties(${_WEBSOCKET_TP_SOURCES} PROPERTIES COMPILE_OPTIONS "/w")
    else()
        set_source_files_properties(${_WEBSOCKET_TP_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
    endif()
endif()

# ----------------------------------------
# 将所有配置应用到目标
# 若父级已创建目标 godot，则直接应用；
# 否则将信息上浮到父级，由父级在创建目标后统一应用。
# ----------------------------------------
if(TARGET godot)
    # 源文件
    if(_WEBSOCKET_ALL_LOCAL_SOURCES)
        target_sources(godot PRIVATE ${_WEBSOCKET_ALL_LOCAL_SOURCES})
    endif()
    if(_WEBSOCKET_TP_SOURCES)
        target_sources(godot PRIVATE ${_WEBSOCKET_TP_SOURCES})
    endif()

    # 第三方头路径
    if(_WEBSOCKET_TP_INCLUDES)
        target_include_directories(godot PRIVATE ${_WEBSOCKET_TP_INCLUDES})
    endif()

    # 宏定义
    if(_WEBSOCKET_TP_DEFINES)
        target_compile_definitions(godot PRIVATE ${_WEBSOCKET_TP_DEFINES})
    endif()

    # Emscripten JS 库链接选项
    if(_WEBSOCKET_LINK_OPTIONS)
        target_link_options(godot PRIVATE ${_WEBSOCKET_LINK_OPTIONS})
    endif()
else()
    # 上浮：源文件
    set(MODULE_SOURCES ${MODULE_SOURCES} ${_WEBSOCKET_ALL_LOCAL_SOURCES} PARENT_SCOPE)
    set(MODULE_SOURCES ${MODULE_SOURCES} ${_WEBSOCKET_TP_SOURCES} PARENT_SCOPE)

    # 上浮：包含目录 / 宏 / 链接选项（供父级统一应用）
    set(MODULE_INCLUDE_DIRS ${MODULE_INCLUDE_DIRS} ${_WEBSOCKET_TP_INCLUDES} PARENT_SCOPE)
    set(MODULE_COMPILE_DEFINES ${MODULE_COMPILE_DEFINES} ${_WEBSOCKET_TP_DEFINES} PARENT_SCOPE)
    set(MODULE_LINK_OPTIONS ${MODULE_LINK_OPTIONS} ${_WEBSOCKET_LINK_OPTIONS} PARENT_SCOPE)
endif()