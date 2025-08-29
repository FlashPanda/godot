# CMake

# 说明：
# 1) 这是子目录的 CMakeLists.txt，不声明 project()，仅向父目标追加配置。
# 2) 假定上层已经定义了目标 godot（与 Godot 主二进制/库对应）。
# 3) 该文件等价实现原 SCsub：
#    - 总是编译本目录的 *.cpp 源并加入到 godot 目标；
#    - 若目标平台为 Web（Emscripten），则附加 JS 接口库 library_godot_webrtc.js。

# --------------------------
# 追加当前目录的 C++ 源文件
# --------------------------
file(GLOB WEBRTC_LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 如果父级已创建了目标 godot，则直接把源码并入；否则把源码上浮给父级处理
if(TARGET godot)
    target_sources(godot PRIVATE ${WEBRTC_LOCAL_SOURCES})
else()
    # 兼容：若父级尚未创建目标，可通过该变量在父级统一收集后再添加
    set(MODULE_SOURCES ${MODULE_SOURCES} ${WEBRTC_LOCAL_SOURCES} PARENT_SCOPE)
endif()

# --------------------------------------------
# Web 平台（Emscripten）时，添加 JS 接口库
# --------------------------------------------
# 原 SCsub 在 env["platform"] == "web" 时调用 AddJSLibraries(["library_godot_webrtc.js"])
# 在 Emscripten 下，等效为链接选项：--js-library <path/to/library.js>
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(WEBRTC_JS_LIB "${CMAKE_CURRENT_SOURCE_DIR}/library_godot_webrtc.js")

    if(EXISTS "${WEBRTC_JS_LIB}")
        if(TARGET godot)
            # 仅对 godot 目标生效
            target_link_options(godot PRIVATE
                    "SHELL:--js-library ${WEBRTC_JS_LIB}"
            )
        else()
            # 兼容：若父级尚未创建目标，把选项上浮，由父级在创建目标后应用
            set(MODULE_LINK_OPTIONS ${MODULE_LINK_OPTIONS} "SHELL:--js-library ${WEBRTC_JS_LIB}" PARENT_SCOPE)
        endif()
    else()
        message(WARNING "[webrtc] 未找到 JS 库文件：${WEBRTC_JS_LIB}")
    endif()
endif()