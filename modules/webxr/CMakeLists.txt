# CMake 子目录配置（webxr 模块）
# 等价于 SCons 中的 SCsub：
# - 若平台为 web（Emscripten）：添加 JS 库 native/library_godot_webxr.js
#   以及 externs 文件 native/webxr.externs.js；
# - 始终编译本目录下的 *.cpp 并合入父目标 godot。
#
# 说明：
# - 本文件是子目录 CMakeLists.txt，不声明 project()；
# - 假定父级已创建目标 godot；若尚未创建，则将信息“上浮”给父级使用。

# ----------------------------------------
# 本模块源文件
# ----------------------------------------
file(GLOB WEBXR_LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# ----------------------------------------
# 平台为 Web（Emscripten）时，追加 JS 库与 externs
# 对应 SCons:
#   env.AddJSLibraries(["native/library_godot_webxr.js"])
#   env.AddJSExterns(["native/webxr.externs.js"])
# 在 Emscripten 中分别映射为：
#   --js-library <path/to/library.js>
#   --closure-args=--externs <path/to/externs.js>
# ----------------------------------------
set(_WEBXR_LINK_OPTIONS "")
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(_WEBXR_JS_LIB "${CMAKE_CURRENT_SOURCE_DIR}/native/library_godot_webxr.js")
    if(EXISTS "${_WEBXR_JS_LIB}")
        list(APPEND _WEBXR_LINK_OPTIONS "SHELL:--js-library ${_WEBXR_JS_LIB}")
    else()
        message(WARNING "[webxr] 未找到 JS 库：${_WEBXR_JS_LIB}")
    endif()

    set(_WEBXR_EXTERNS "${CMAKE_CURRENT_SOURCE_DIR}/native/webxr.externs.js")
    if(EXISTS "${_WEBXR_EXTERNS}")
        # 注意：只有在启用 --closure 时，--externs 才会被使用；
        # 这里先把参数添加上，是否启用 closure 由顶层工具链/构建设置决定。
        list(APPEND _WEBXR_LINK_OPTIONS "SHELL:--closure-args=--externs ${_WEBXR_EXTERNS}")
    else()
        message(WARNING "[webxr] 未找到 externs：${_WEBXR_EXTERNS}")
    endif()
endif()

# ----------------------------------------
# 将所有配置应用到目标
# 若父级已创建目标 godot，则直接应用；
# 否则将信息上浮到父级，由父级在创建目标后统一应用。
# ----------------------------------------
if(TARGET godot)
    if(WEBXR_LOCAL_SOURCES)
        target_sources(godot PRIVATE ${WEBXR_LOCAL_SOURCES})
    endif()

    if(_WEBXR_LINK_OPTIONS)
        target_link_options(godot PRIVATE ${_WEBXR_LINK_OPTIONS})
    endif()
else()
    # 上浮：源文件
    set(MODULE_SOURCES ${MODULE_SOURCES} ${WEBXR_LOCAL_SOURCES} PARENT_SCOPE)
    # 上浮：链接选项（供父级统一应用）
    set(MODULE_LINK_OPTIONS ${MODULE_LINK_OPTIONS} ${_WEBXR_LINK_OPTIONS} PARENT_SCOPE)
endif()
