# 子目录 CMakeLists：复现 modules/text_server_fb/SCsub 的逻辑
# - 不声明 project()，仅作为父工程的子目录被包含
# - 将本模块源码并注入父目标 `godot`
# - 对齐 SCons 的条件：svg/msdfgen/freetype 等开关、包含目录与宏定义

cmake_minimum_required(VERSION 3.16)

# 0) 前置：确保父级已定义目标 `godot`（可执行或库）
if (NOT TARGET godot)
    message(FATAL_ERROR "Target 'godot' not found. Define it in the parent CMakeLists.txt before add_subdirectory.")
endif()

# 1) 选项开关（映射 SCons 中的 env[...] 与模块开关）
#    如果上层已设定这些开关，这里只作为默认值。
option(MODULE_SVG_ENABLED         "Enable cooperation with svg module (ThorVG headers/defines)" ON)  # 对齐：'svg' in env.module_list
option(MODULE_FREETYPE_ENABLED    "Enable freetype-dependent parts in this module"              ON)  # 对齐：'freetype' in env.module_list
option(MODULE_MSDFGEN_ENABLED     "Enable msdfgen-dependent parts in this module"               ON)  # 对齐：'msdfgen' in env.module_list

option(GODOT_BUILTIN_MSDFGEN      "Use bundled msdfgen headers"                                 ON)  # 对齐：env['builtin_msdfgen']
option(GODOT_BUILTIN_FREETYPE     "Use bundled freetype headers"                                ON)  # 对齐：env['builtin_freetype']

# 第三方根目录（按 Godot 源树惯例）
set(_TP_ROOT "${CMAKE_SOURCE_DIR}/thirdparty")

# ---------------------------------------------------------------------------------------------------
# 2) 与 svg 模块的协作：当启用时，为 ThorVG 提供包含目录与宏（等价于 SCsub 顶部逻辑）
# ---------------------------------------------------------------------------------------------------
if (MODULE_SVG_ENABLED)
    # 对齐：
    # CPPPATH += ["#thirdparty/thorvg/inc", "#thirdparty/thorvg/src/common", "#thirdparty/thorvg/src/renderer"]
    target_include_directories(godot PRIVATE
            "${_TP_ROOT}/thorvg/inc"
            "${_TP_ROOT}/thorvg/src/common"
            "${_TP_ROOT}/thorvg/src/renderer"
    )
    # 对齐：定义 TVG_STATIC 以启用 ThorVG 的静态对象链接
    target_compile_definitions(godot PRIVATE TVG_STATIC)
endif()

# ---------------------------------------------------------------------------------------------------
# 3) msdfgen 头文件：启用且选择使用内置时，按编译器差异设置包含目录，并定义 MSDFGEN_PUBLIC 为空
# ---------------------------------------------------------------------------------------------------
if (GODOT_BUILTIN_MSDFGEN AND MODULE_MSDFGEN_ENABLED)
    # SCons：非 MSVC 下把第三方头当作 system 头以抑制警告；MSVC 不支持 -isystem，因此普通包含
    if (MSVC)
        target_include_directories(godot PRIVATE "${_TP_ROOT}/msdfgen")
    else()
        target_include_directories(godot SYSTEM PRIVATE "${_TP_ROOT}/msdfgen")
    endif()
    target_compile_definitions(godot PRIVATE MSDFGEN_PUBLIC=)
endif()

# ---------------------------------------------------------------------------------------------------
# 4) freetype：启用且选择使用内置时，定义 FT_CONFIG_OPTION_USE_BROTLI 并提供头路径
# ---------------------------------------------------------------------------------------------------
if (GODOT_BUILTIN_FREETYPE AND MODULE_FREETYPE_ENABLED)
    target_compile_definitions(godot PRIVATE FT_CONFIG_OPTION_USE_BROTLI)
    target_include_directories(godot PRIVATE "${_TP_ROOT}/freetype/include")
endif()

# ---------------------------------------------------------------------------------------------------
# 5) 本模块源码：将当前目录下所有 *.cpp 并入父目标 `godot`
# ---------------------------------------------------------------------------------------------------
file(GLOB CONFIGURE_DEPENDS TEXT_SERVER_FB_LOCAL_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

if (TEXT_SERVER_FB_LOCAL_SOURCES STREQUAL "")
    message(STATUS "[text_server_fb] No local .cpp found in ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

target_sources(godot PRIVATE ${TEXT_SERVER_FB_LOCAL_SOURCES})

# 友好提示
message(STATUS "[text_server_fb] SVG:${MODULE_SVG_ENABLED} Freetype:${MODULE_FREETYPE_ENABLED} msdfgen:${MODULE_MSDFGEN_ENABLED}")
message(STATUS "[text_server_fb] builtin: FT=${GODOT_BUILTIN_FREETYPE} msdfgen=${GODOT_BUILTIN_MSDFGEN}")