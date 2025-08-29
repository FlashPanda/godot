# 子级 CMakeLists：复现 SCsub 的行为，不声明 project()

cmake_minimum_required(VERSION 3.16)

# 确保上层已定义目标 godot（可执行或库）
if (NOT TARGET godot)
    message(FATAL_ERROR "Target 'godot' not found. Define it in the parent CMakeLists.txt before add_subdirectory.")
endif()

# 选项开关（对应 SCons 中的 env["builtin_pcre2"] 与 env["builtin_pcre2_with_jit"]）
option(GODOT_BUILTIN_PCRE2 "Build bundled thirdparty PCRE2 for this module" ON)
option(GODOT_BUILTIN_PCRE2_WITH_JIT "Enable PCRE2 JIT when building bundled thirdparty" ON)

# -----------------------------
# 1) （可选）内置 thirdparty: pcre2
# -----------------------------
set(_PCRE2_TP_DIR "${CMAKE_SOURCE_DIR}/thirdparty/pcre2/src")

set(_pcre2_rel_sources
        # 与 SCsub 一致的源列表；注：某些文件由其他源包含（见注释），不应单独编译
        "pcre2_auto_possess.c"
        "pcre2_chartables.c"
        "pcre2_chkdint.c"
        "pcre2_compile.c"
        "pcre2_config.c"
        "pcre2_context.c"
        "pcre2_convert.c"
        "pcre2_dfa_match.c"
        "pcre2_error.c"
        "pcre2_extuni.c"
        "pcre2_find_bracket.c"
        "pcre2_jit_compile.c"       # pcre2_jit_match.c / pcre2_jit_misc.c 被该文件包含，无需单独列出
        "pcre2_maketables.c"
        "pcre2_match.c"
        "pcre2_match_data.c"
        "pcre2_newline.c"
        "pcre2_ord2utf.c"
        "pcre2_pattern_info.c"
        "pcre2_script_run.c"
        "pcre2_serialize.c"
        "pcre2_string_utils.c"
        "pcre2_study.c"
        "pcre2_substitute.c"
        "pcre2_substring.c"
        "pcre2_tables.c"
        "pcre2_ucd.c"
        # "pcre2_ucptables.c"        # 被 pcre2_tables.c 包含，不应单独编译
        "pcre2_valid_utf.c"
        "pcre2_xclass.c"
)

# 仅在选择内置构建时，构造实际路径并创建两个宽度的对象库（16/32）
if (GODOT_BUILTIN_PCRE2)
    # 将相对路径补全为绝对路径
    set(_pcre2_sources "")
    foreach(_rel IN LISTS _pcre2_rel_sources)
        list(APPEND _pcre2_sources "${_PCRE2_TP_DIR}/${_rel}")
    endforeach()

    # 公共编译定义（来自 SCsub）：PCRE2_STATIC, HAVE_CONFIG_H, SUPPORT_UNICODE
    set(_PCRE2_COMMON_DEFS PCRE2_STATIC HAVE_CONFIG_H SUPPORT_UNICODE)
    if (GODOT_BUILTIN_PCRE2_WITH_JIT)
        list(APPEND _PCRE2_COMMON_DEFS SUPPORT_JIT)
    endif()

    # 为 thirdparty 源关闭警告
    set(_DISABLE_WARN_MSVC /w)
    set(_DISABLE_WARN_GNU -w)

    # 对象库：PCRE2_CODE_UNIT_WIDTH=16
    add_library(pcre2_width16 OBJECT ${_pcre2_sources})
    target_compile_definitions(pcre2_width16 PRIVATE
            ${_PCRE2_COMMON_DEFS}
            PCRE2_CODE_UNIT_WIDTH=16
    )
    target_include_directories(pcre2_width16 PRIVATE "${_PCRE2_TP_DIR}")
    if (MSVC)
        target_compile_options(pcre2_width16 PRIVATE ${_DISABLE_WARN_MSVC})
    else()
        target_compile_options(pcre2_width16 PRIVATE ${_DISABLE_WARN_GNU})
    endif()

    # 对象库：PCRE2_CODE_UNIT_WIDTH=32
    add_library(pcre2_width32 OBJECT ${_pcre2_sources})
    target_compile_definitions(pcre2_width32 PRIVATE
            ${_PCRE2_COMMON_DEFS}
            PCRE2_CODE_UNIT_WIDTH=32
    )
    target_include_directories(pcre2_width32 PRIVATE "${_PCRE2_TP_DIR}")
    if (MSVC)
        target_compile_options(pcre2_width32 PRIVATE ${_DISABLE_WARN_MSVC})
    else()
        target_compile_options(pcre2_width32 PRIVATE ${_DISABLE_WARN_GNU})
    endif()

    # 将 thirdparty 对象加入到 godot 目标，并建立依赖
    target_sources(godot PRIVATE
            $<TARGET_OBJECTS:pcre2_width16>
            $<TARGET_OBJECTS:pcre2_width32>
    )
    add_dependencies(godot pcre2_width16 pcre2_width32)
endif()

# --------------------------------
# 2) 模块本体源码：本目录下的 *.cpp
#    要求定义 PCRE2_CODE_UNIT_WIDTH=0
# --------------------------------
file(GLOB CONFIGURE_DEPENDS REGEX_MODULE_LOCAL_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 使用对象库承载“PCRE2_CODE_UNIT_WIDTH=0”这一专属定义
add_library(regex_module_obj OBJECT ${REGEX_MODULE_LOCAL_SOURCES})
target_compile_definitions(regex_module_obj PRIVATE PCRE2_CODE_UNIT_WIDTH=0)

# 若启用内置 PCRE2，则为模块编译提供其头文件（与 SCsub 的 CPPPATH 行为等价）
if (GODOT_BUILTIN_PCRE2)
    target_include_directories(regex_module_obj PRIVATE "${_PCRE2_TP_DIR}")
endif()

# 将模块对象加入 godot，并建立依赖（确保 thirdparty 变更时本模块会触发重编）
target_sources(godot PRIVATE $<TARGET_OBJECTS:regex_module_obj>)
add_dependencies(godot regex_module_obj)

# 友好输出
if (GODOT_BUILTIN_PCRE2)
    message(STATUS "[regex module] Using bundled PCRE2 (JIT: ${GODOT_BUILTIN_PCRE2_WITH_JIT})")
else()
    message(STATUS "[regex module] Using system PCRE2 (bundled disabled)")
endif()