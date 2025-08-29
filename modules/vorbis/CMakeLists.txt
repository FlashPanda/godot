# cmake

# -----------------------------------------------------------------------------
# 0) 选择父级目标名称
#    - 默认父级已定义目标名为 godot（可执行/库）。
#    - 如父级使用不同目标名，可在 add_subdirectory 之前设置 GODOT_PARENT_TARGET 覆盖。
if (NOT DEFINED GODOT_PARENT_TARGET)
    set(GODOT_PARENT_TARGET godot)
endif()

if (NOT TARGET ${GODOT_PARENT_TARGET})
    message(FATAL_ERROR
            "未找到父级目标 '${GODOT_PARENT_TARGET}'。请先在父级 CMakeLists.txt 中定义该目标，"
            "再 add_subdirectory 进入本目录，或通过 set(GODOT_PARENT_TARGET <目标名>) 指定。")
endif()

# -----------------------------------------------------------------------------
# 1) 选项（与 SCons 开关对应）
#    - builtin_libvorbis: 是否内置并编译 thirdparty/libvorbis 源
#    - builtin_libogg:    是否将 thirdparty/libogg 的包含目录加入（libvorbis 依赖）
if (NOT DEFINED builtin_libvorbis)
    option(builtin_libvorbis "Build bundled libvorbis from thirdparty/libvorbis" ON)
endif()

if (NOT DEFINED builtin_libogg)
    option(builtin_libogg "Use bundled libogg headers (for include dirs)" ON)
endif()

# -----------------------------------------------------------------------------
# 2) 模块自身源码（等价 SCons: env_vorbis.add_source_files(module_obj, "*.cpp")）
file(GLOB MODULE_LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

if (MODULE_LOCAL_SOURCES)
    target_sources(${GODOT_PARENT_TARGET} PRIVATE ${MODULE_LOCAL_SOURCES})
endif()

# -----------------------------------------------------------------------------
# 3) thirdparty/libvorbis（等价 SCons: 在 builtin_libvorbis=1 时启用）
if (builtin_libvorbis)
    set(VORBIS_THIRDPARTY_DIR "${PROJECT_SOURCE_DIR}/thirdparty/libvorbis")

    # 3.1 源文件清单（与 SCsub 中启用的文件一致）
    set(VORBIS_SOURCES
            "${VORBIS_THIRDPARTY_DIR}/bitrate.c"
            "${VORBIS_THIRDPARTY_DIR}/block.c"
            "${VORBIS_THIRDPARTY_DIR}/codebook.c"
            "${VORBIS_THIRDPARTY_DIR}/envelope.c"
            "${VORBIS_THIRDPARTY_DIR}/floor0.c"
            "${VORBIS_THIRDPARTY_DIR}/floor1.c"
            "${VORBIS_THIRDPARTY_DIR}/info.c"
            "${VORBIS_THIRDPARTY_DIR}/lookup.c"
            "${VORBIS_THIRDPARTY_DIR}/lpc.c"
            "${VORBIS_THIRDPARTY_DIR}/lsp.c"
            "${VORBIS_THIRDPARTY_DIR}/mapping0.c"
            "${VORBIS_THIRDPARTY_DIR}/mdct.c"
            "${VORBIS_THIRDPARTY_DIR}/psy.c"
            "${VORBIS_THIRDPARTY_DIR}/registry.c"
            "${VORBIS_THIRDPARTY_DIR}/res0.c"
            "${VORBIS_THIRDPARTY_DIR}/sharedbook.c"
            "${VORBIS_THIRDPARTY_DIR}/smallft.c"
            "${VORBIS_THIRDPARTY_DIR}/synthesis.c"
            "${VORBIS_THIRDPARTY_DIR}/vorbisfile.c"
            "${VORBIS_THIRDPARTY_DIR}/window.c"
    )

    # 3.2 包含目录（libvorbis 自身 + 可选 libogg）
    target_include_directories(${GODOT_PARENT_TARGET} PRIVATE
            "${VORBIS_THIRDPARTY_DIR}"
    )
    if (builtin_libogg)
        target_include_directories(${GODOT_PARENT_TARGET} PRIVATE
                "${PROJECT_SOURCE_DIR}/thirdparty/libogg"
        )
    endif()

    # 3.3 将 thirdparty 源并入与模块同一目标
    target_sources(${GODOT_PARENT_TARGET} PRIVATE ${VORBIS_SOURCES})

    # 3.4 关闭 thirdparty 源的编译警告（模拟 env_thirdparty.disable_warnings()）
    if (MSVC)
        set_source_files_properties(${VORBIS_SOURCES}
                PROPERTIES COMPILE_OPTIONS "/w")
    else()
        set_source_files_properties(${VORBIS_SOURCES}
                PROPERTIES COMPILE_OPTIONS "-w")
    endif()
endif()

# 说明：
# - 通过把 thirdparty 与模块源文件加入同一目标，天然实现对 thirdparty 变化的依赖追踪，
#   效果等价于 SCons 中的 env.Depends(module_obj, thirdparty_obj)。