# CMake

# -----------------------------------------------------------------------------
# 0) 选择父级目标名称
#    - 默认使用 godot 作为父级已定义目标（可执行/库）。
#    - 如父级使用不同目标名，可在 add_subdirectory 之前设置 GODOT_PARENT_TARGET 覆盖。
if (NOT DEFINED GODOT_PARENT_TARGET)
    set(GODOT_PARENT_TARGET godot)
endif()

if (NOT TARGET ${GODOT_PARENT_TARGET})
    message(FATAL_ERROR
            "未找到父级目标 '${GODOT_PARENT_TARGET}'。"
            "请先在父级 CMakeLists.txt 中定义该目标，再 add_subdirectory 进入本目录，"
            "或通过 set(GODOT_PARENT_TARGET <目标名>) 指定。"
    )
endif()

# -----------------------------------------------------------------------------
# 1) 选项与开关（尽量复用父级已定义变量；未定义时提供默认值）
#    这些选项对应 SCons 环境中的开关。
#    - builtin_libtheora: 是否内置并编译 thirdparty/libtheora
#    - builtin_libogg / builtin_libvorbis: 是否需要将其包含路径加入（libtheora 依赖）
#    - x86_libtheora_opt_gcc / x86_libtheora_opt_vc: 是否启用 x86 优化源（不同路径/实现）
if (NOT DEFINED builtin_libtheora)
    option(builtin_libtheora "Build bundled libtheora from thirdparty/libtheora" ON)
endif()

if (NOT DEFINED builtin_libogg)
    option(builtin_libogg "Use bundled libogg headers (for include dirs)" ON)
endif()

if (NOT DEFINED builtin_libvorbis)
    option(builtin_libvorbis "Use bundled libvorbis headers (for include dirs)" ON)
endif()

if (NOT DEFINED x86_libtheora_opt_gcc)
    option(x86_libtheora_opt_gcc "Enable x86 optimizations for libtheora (GCC-style sources)" OFF)
endif()

if (NOT DEFINED x86_libtheora_opt_vc)
    option(x86_libtheora_opt_vc "Enable x86 optimizations for libtheora (MSVC-style sources)" OFF)
endif()

# -----------------------------------------------------------------------------
# 2) 收集本模块源码（等价于 SCons: env_theora.add_source_files(module_obj, \"*.cpp\")）
file(GLOB MODULE_LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

if (MODULE_LOCAL_SOURCES)
    target_sources(${GODOT_PARENT_TARGET} PRIVATE ${MODULE_LOCAL_SOURCES})
endif()

# -----------------------------------------------------------------------------
# 3) thirdparty/libtheora 源文件清单与包含路径（在 builtin_libtheora=ON 时启用）
if (builtin_libtheora)
    # thirdparty 路径：使用工程根路径，适配从任意子目录调用。
    set(THEORA_THIRDPARTY_DIR "${PROJECT_SOURCE_DIR}/thirdparty/libtheora")

    # 3.1 基础源（与 SCsub 中启用的清单一致，注释掉的源不加入）
    set(THEORA_SOURCES
            "${THEORA_THIRDPARTY_DIR}/bitpack.c"
            "${THEORA_THIRDPARTY_DIR}/decinfo.c"
            "${THEORA_THIRDPARTY_DIR}/decode.c"
            "${THEORA_THIRDPARTY_DIR}/dequant.c"
            "${THEORA_THIRDPARTY_DIR}/fragment.c"
            "${THEORA_THIRDPARTY_DIR}/huffdec.c"
            "${THEORA_THIRDPARTY_DIR}/idct.c"
            "${THEORA_THIRDPARTY_DIR}/info.c"
            "${THEORA_THIRDPARTY_DIR}/internal.c"
            "${THEORA_THIRDPARTY_DIR}/quant.c"
            "${THEORA_THIRDPARTY_DIR}/state.c"
    )

    # 3.2 x86 优化源（GCC 风格）
    set(THEORA_SOURCES_X86_GCC
            "${THEORA_THIRDPARTY_DIR}/x86/mmxfrag.c"
            "${THEORA_THIRDPARTY_DIR}/x86/mmxidct.c"
            "${THEORA_THIRDPARTY_DIR}/x86/mmxstate.c"
            "${THEORA_THIRDPARTY_DIR}/x86/sse2idct.c"
            "${THEORA_THIRDPARTY_DIR}/x86/x86cpu.c"
            "${THEORA_THIRDPARTY_DIR}/x86/x86state.c"
    )

    # 3.3 x86 优化源（MSVC 风格）
    set(THEORA_SOURCES_X86_VC
            "${THEORA_THIRDPARTY_DIR}/x86_vc/mmxfrag.c"
            "${THEORA_THIRDPARTY_DIR}/x86_vc/mmxidct.c"
            "${THEORA_THIRDPARTY_DIR}/x86_vc/mmxstate.c"
            "${THEORA_THIRDPARTY_DIR}/x86_vc/x86cpu.c"
            "${THEORA_THIRDPARTY_DIR}/x86_vc/x86state.c"
    )

    # 3.4 在 x86/amd64 平台允许附加优化源，避免在非 x86 体系上误编译
    set(IS_X86 FALSE)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|X86_64|amd64|AMD64|i[3-6]86|x86)$")
        set(IS_X86 TRUE)
    endif()

    if (IS_X86)
        # 与 SCons 相同的两个开关：任选其一或同时开启均视为需要定义 OC_X86_ASM
        if (x86_libtheora_opt_gcc)
            list(APPEND THEORA_SOURCES ${THEORA_SOURCES_X86_GCC})
        endif()
        if (x86_libtheora_opt_vc)
            list(APPEND THEORA_SOURCES ${THEORA_SOURCES_X86_VC})
        endif()

        if (x86_libtheora_opt_gcc OR x86_libtheora_opt_vc)
            target_compile_definitions(${GODOT_PARENT_TARGET} PRIVATE OC_X86_ASM)
        endif()
    endif()

    # 3.5 包含目录：libtheora 自身 + （可选）libogg/libvorbis
    target_include_directories(${GODOT_PARENT_TARGET} PRIVATE
            "${THEORA_THIRDPARTY_DIR}"
    )

    if (builtin_libogg)
        target_include_directories(${GODOT_PARENT_TARGET} PRIVATE
                "${PROJECT_SOURCE_DIR}/thirdparty/libogg"
        )
    endif()

    if (builtin_libvorbis)
        target_include_directories(${GODOT_PARENT_TARGET} PRIVATE
                "${PROJECT_SOURCE_DIR}/thirdparty/libvorbis"
        )
    endif()

    # 3.6 将 thirdparty 源并入与模块同一目标
    #     - 这样能确保 thirdparty 文件发生变化时，相关对象与依赖会被正确重建，
    #       等价于 SCons 中对 module_obj 依赖 thirdparty_obj 的效果。
    if (THEORA_SOURCES)
        target_sources(${GODOT_PARENT_TARGET} PRIVATE ${THEORA_SOURCES})
    endif()
endif()