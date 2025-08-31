# 子目录 CMakeLists.txt —— Web/Emscripten 平台构建辅助
# 用法（建议在父级中按需配置后 add_subdirectory(本目录)）：
# 1) 指定需要注入 emcc 选项的目标列表（对应“build_targets”概念）：
#    set(WEB_BUILD_TARGETS godot godot_template_debug godot_template_release)
# 2) 指定预/后置 JS、js-library、externs、导出函数/运行时方法等（均为可选）：
#    set(WEB_PRE_JS        "${CMAKE_CURRENT_SOURCE_DIR}/pre.js")
#    set(WEB_POST_JS       "${CMAKE_CURRENT_SOURCE_DIR}/post.js")
#    set(WEB_JS_LIBS       "${CMAKE_CURRENT_SOURCE_DIR}/library_godot.js;${CMAKE_CURRENT_SOURCE_DIR}/library_extra.js")
#    set(WEB_EXTERNS       "${CMAKE_CURRENT_SOURCE_DIR}/externs.json")  # 或者 .js
#    set(WEB_EXPORTED_FUNCTIONS "_main;_malloc;_free")                   # 分号分隔即可
#    set(WEB_EXPORTED_RUNTIME_METHODS "ccall;cwrap;UTF8ToString")        # 分号分隔即可
# 3) 如果有“包装/封装列表”（wrap_list/js_wrapped），可通过：
#    set(WEB_WRAP_LIST     "glBind*;glDraw*")        # 将被转换为 -Wl,--wrap=<pattern>（通配符将按需展开/保留）
#    set(WEB_JS_WRAPPED    "myWrappedFunc;otherOne") # 供你在自定义流程中使用（本文件不强制消费）
# 4) 可切换 wasm/js 引擎与系统特性：
#    set(WEB_WASM              ON)                          # 打包为 wasm（默认 ON）
#    set(WEB_JS_ENGINE         "wasm")                      # 标记使用的 JS 引擎名称/类型（自由字段，可用于条件）
#    set(WEB_SYS               "webgl2;filesystem;threads") # 自由字段：系统/子系统标记，下面会按需映射到 -s 选项
#    set(WEB_PORT              "8060")                      # 自由字段：仅记录（与构建无强绑定，可供工具/Run脚本用）
# 5) 可追加额外 emcc 链接选项（例如优化级别、内存大小等）：
#    set(WEB_EXTRA_LINK_FLAGS  "-O3;-sALLOW_MEMORY_GROWTH=1;-sEXIT_RUNTIME=0")
#
# 注意：
# - 本文件不会创建新的目标，仅对父级已存在的目标进行“就地增强”。
# - 未指定的变量均为可选，且不会影响未配置部分的默认行为。
# - 你可以将本文件看作把原脚本中的：port/web_files/sys_env/lib/js/ext/build/build_targets/sys/wasm/engine/externs/js_engine/wrap_list/js_wrapped 等变量
#   映射为一组标准的 CMake 变量，并在 Emscripten 环境中转换为 emcc 的链接参数。

cmake_minimum_required(VERSION 3.20)

# 0) 仅在 Emscripten 环境下生效（防止误用）
if(NOT "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
    message(STATUS "[web] 非 Emscripten 平台，跳过 Web 构建辅助逻辑。")
    return()
endif()

# 1) 统一的开关与输入（均可由父级传入；未传则给默认值）
set(WEB_BUILD_TARGETS                ""          CACHE STRING "要注入 emcc 选项的目标列表（分号分隔）")
set(WEB_WASM                         ON          CACHE BOOL   "是否启用 WASM 打包")
set(WEB_JS_ENGINE                    "wasm"      CACHE STRING "JS 引擎/运行时标识（自由字段）")
set(WEB_SYS                          ""          CACHE STRING "系统/子系统标识列表（分号分隔，自由字段）")
set(WEB_PORT                         ""          CACHE STRING "调试/运行端口（自由字段，不直接影响构建）")

set(WEB_PRE_JS                       ""          CACHE FILEPATH "emcc --pre-js 输入（单文件）")
set(WEB_POST_JS                      ""          CACHE FILEPATH "emcc --post-js 输入（单文件）")
set(WEB_JS_LIBS                      ""          CACHE STRING  "emcc --js-library 列表（分号分隔）")
set(WEB_EXTERNS                      ""          CACHE FILEPATH "Externs 文件（.json 或 .js），用于导出函数配置")
set(WEB_EXPORTED_FUNCTIONS           ""          CACHE STRING  "显式导出函数名（分号分隔，例如 _main;_malloc;_free）")
set(WEB_EXPORTED_RUNTIME_METHODS     ""          CACHE STRING  "导出运行时方法名（分号分隔，例如 ccall;cwrap）")
set(WEB_WRAP_LIST                    ""          CACHE STRING  "链接层 wrap 列表（分号分隔，每个元素映射为 -Wl,--wrap=<name>）")
set(WEB_JS_WRAPPED                   ""          CACHE STRING  "JS 层封装函数名列表（分号分隔，仅记录，不强制使用）")
set(WEB_EXTRA_LINK_FLAGS             ""          CACHE STRING  "附加 emcc 链接选项（分号分隔，例如 -O3;-sALLOW_MEMORY_GROWTH=1）")

# 2) 将“自由字段”的系统特性（WEB_SYS）映射到常见的 emcc -s 选项（按需扩充/调整）
set(_web_sys_link_flags "")
foreach(_sys IN LISTS WEB_SYS)
    string(TOLOWER "${_sys}" _sys_lc)
    if(_sys_lc STREQUAL "webgl" OR _sys_lc STREQUAL "webgl2")
        list(APPEND _web_sys_link_flags "-sMIN_WEBGL_VERSION=2" "-sMAX_WEBGL_VERSION=2")
    elseif(_sys_lc STREQUAL "filesystem" OR _sys_lc STREQUAL "fs")
        list(APPEND _web_sys_link_flags "-sFORCE_FILESYSTEM=1")
    elseif(_sys_lc STREQUAL "threads" OR _sys_lc STREQUAL "pthreads")
        list(APPEND _web_sys_link_flags "-sUSE_PTHREADS=1")
    elseif(_sys_lc STREQUAL "sdl2")
        list(APPEND _web_sys_link_flags "-sUSE_SDL=2")
    endif()
endforeach()

# 3) 将变量转换为 emcc 可消费的链接选项
set(_web_link_flags "")

# 3.1) wasm/js 输出控制（典型：生成 .wasm 并启用 MODULARIZE/EXPORT_NAME 等，按需扩展）
if(WEB_WASM)
    list(APPEND _web_link_flags
            "-sWASM=1"
            "-sMODULARIZE=1"
            "-sEXPORT_NAME=createGodotInstance"
    )
else()
    list(APPEND _web_link_flags "-sWASM=0")
endif()

# 3.2) externs：支持 .json（@file 语法）或 .js（--extern-post-js）
if(WEB_EXTERNS)
    get_filename_component(_ext_ext "${WEB_EXTERNS}" EXT)
    if(_ext_ext STREQUAL ".json")
        # 采用 @file 语法让 emcc 读取导出列表
        list(APPEND _web_link_flags "-sEXPORTED_FUNCTIONS=@${WEB_EXTERNS}")
    else()
        # 作为额外的 extern 注入脚本
        list(APPEND _web_link_flags "--extern-post-js" "${WEB_EXTERNS}")
    endif()
endif()

# 3.3) 直接指定导出函数/运行时方法（分号分隔 => 组装为 JS 数组）
if(WEB_EXPORTED_FUNCTIONS)
    # 拼接为 ["_main","_malloc",...]
    string(REPLACE ";" "\",\"" _ef_body "${WEB_EXPORTED_FUNCTIONS}")
    set(_ef_val "[\"${_ef_body}\"]")
    list(APPEND _web_link_flags "-sEXPORTED_FUNCTIONS=${_ef_val}")
endif()

if(WEB_EXPORTED_RUNTIME_METHODS)
    string(REPLACE ";" "\",\"" _erm_body "${WEB_EXPORTED_RUNTIME_METHODS}")
    set(_erm_val "[\"${_erm_body}\"]")
    list(APPEND _web_link_flags "-sEXPORTED_RUNTIME_METHODS=${_erm_val}")
endif()

# 3.4) 预/后置 JS
if(WEB_PRE_JS)
    list(APPEND _web_link_flags "--pre-js" "${WEB_PRE_JS}")
endif()
if(WEB_POST_JS)
    list(APPEND _web_link_flags "--post-js" "${WEB_POST_JS}")
endif()

# 3.5) js-library 列表
if(WEB_JS_LIBS)
    foreach(_lib IN LISTS WEB_JS_LIBS)
        list(APPEND _web_link_flags "--js-library" "${_lib}")
    endforeach()
endif()

# 3.6) wrap 列表（链接器层）
if(WEB_WRAP_LIST)
    foreach(_w IN LISTS WEB_WRAP_LIST)
        list(APPEND _web_link_flags "-Wl,--wrap=${_w}")
    endforeach()
endif()

# 3.7) 系统特性映射与额外链接选项
list(APPEND _web_link_flags ${_web_sys_link_flags})
list(APPEND _web_link_flags ${WEB_EXTRA_LINK_FLAGS})

# 4) 将汇总出的链接选项注入到指定目标（WEB_BUILD_TARGETS）
if(WEB_BUILD_TARGETS)
    foreach(_tgt IN LISTS WEB_BUILD_TARGETS)
        if(TARGET "${_tgt}")
            # 链接阶段生效（emcc 主要在 link 阶段消费这些选项）
            target_link_options("${_tgt}" PRIVATE ${_web_link_flags})

            # 可选：向目标暴露一些编译期定义，方便 C++ 代码内做条件编译/查询
            target_compile_definitions("${_tgt}" PRIVATE
                    GODOT_WEB_WASM=$<IF:$<BOOL:${WEB_WASM}>,1,0>
                    GODOT_WEB_JS_ENGINE="${WEB_JS_ENGINE}"
                    GODOT_WEB_PORT="${WEB_PORT}"
            )
        else()
            message(WARNING "[web] 目标 '${_tgt}' 不存在，跳过注入。")
        endif()
    endforeach()
else()
    message(STATUS "[web] 未指定 WEB_BUILD_TARGETS，未对任何目标注入 emcc 选项。")
endif()

# 5) 记录/导出信息（供父级或其他脚本使用）
#    这些变量可帮助父级在打包阶段收集 web 相关文件（类似“web_files”/“ext”概念）
set(WEB_EFFECTIVE_LINK_FLAGS   "${_web_link_flags}" PARENT_SCOPE)
set(WEB_EFFECTIVE_SYS_FLAGS    "${_web_sys_link_flags}" PARENT_SCOPE)
set(WEB_JS_WRAPPED             "${WEB_JS_WRAPPED}" PARENT_SCOPE)
set(WEB_JS_LIBS                "${WEB_JS_LIBS}" PARENT_SCOPE)
set(WEB_PRE_JS                 "${WEB_PRE_JS}" PARENT_SCOPE)
set(WEB_POST_JS                "${WEB_POST_JS}" PARENT_SCOPE)
set(WEB_EXTERNS                "${WEB_EXTERNS}" PARENT_SCOPE)
set(WEB_EXPORTED_FUNCTIONS     "${WEB_EXPORTED_FUNCTIONS}" PARENT_SCOPE)
set(WEB_EXPORTED_RUNTIME_METHODS "${WEB_EXPORTED_RUNTIME_METHODS}" PARENT_SCOPE)
set(WEB_WASM                   "${WEB_WASM}" PARENT_SCOPE)
set(WEB_JS_ENGINE              "${WEB_JS_ENGINE}" PARENT_SCOPE)
set(WEB_SYS                    "${WEB_SYS}" PARENT_SCOPE)
set(WEB_PORT                   "${WEB_PORT}" PARENT_SCOPE)