# cmake

# -----------------------------------------------------------------------------
# 0) 选择父级目标名称
#    - 默认父级目标名为 godot（可执行/库）。
#    - 若父级使用不同目标名，可在 add_subdirectory 之前设置 GODOT_PARENT_TARGET 覆盖。
if (NOT DEFINED GODOT_PARENT_TARGET)
    set(GODOT_PARENT_TARGET godot)
endif()

if (NOT TARGET ${GODOT_PARENT_TARGET})
    message(FATAL_ERROR
            "未找到父级目标 '${GODOT_PARENT_TARGET}'。请先在父级 CMakeLists.txt 中定义该目标，"
            "再 add_subdirectory 进入本目录，或通过 set(GODOT_PARENT_TARGET <目标名>) 指定。")
endif()

# -----------------------------------------------------------------------------
# 1) 选项与开关
#    - builtin_libwebp: 是否使用工程内置 thirdparty/libwebp 源码
if (NOT DEFINED builtin_libwebp)
    option(builtin_libwebp "Build bundled libwebp from thirdparty/libwebp" ON)
endif()

# -----------------------------------------------------------------------------
# 2) 本模块源码：将当前目录下 *.cpp 并入父级目标
file(GLOB MODULE_LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

if (MODULE_LOCAL_SOURCES)
    target_sources(${GODOT_PARENT_TARGET} PRIVATE ${MODULE_LOCAL_SOURCES})
endif()

# -----------------------------------------------------------------------------
# 3) thirdparty/libwebp（在 builtin_libwebp=ON 时启用）
if (builtin_libwebp)
    set(WEBP_THIRDPARTY_DIR "${PROJECT_SOURCE_DIR}/thirdparty/libwebp")

    # 3.1 源文件清单（与 SCsub 中所列一致）
    set(WEBP_SOURCES
            "${WEBP_THIRDPARTY_DIR}/sharpyuv/sharpyuv.c"
            "${WEBP_THIRDPARTY_DIR}/sharpyuv/sharpyuv_cpu.c"
            "${WEBP_THIRDPARTY_DIR}/sharpyuv/sharpyuv_csp.c"
            "${WEBP_THIRDPARTY_DIR}/sharpyuv/sharpyuv_dsp.c"
            "${WEBP_THIRDPARTY_DIR}/sharpyuv/sharpyuv_gamma.c"
            "${WEBP_THIRDPARTY_DIR}/sharpyuv/sharpyuv_neon.c"
            "${WEBP_THIRDPARTY_DIR}/sharpyuv/sharpyuv_sse2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dec/alpha_dec.c"
            "${WEBP_THIRDPARTY_DIR}/src/dec/buffer_dec.c"
            "${WEBP_THIRDPARTY_DIR}/src/dec/frame_dec.c"
            "${WEBP_THIRDPARTY_DIR}/src/dec/idec_dec.c"
            "${WEBP_THIRDPARTY_DIR}/src/dec/io_dec.c"
            "${WEBP_THIRDPARTY_DIR}/src/dec/quant_dec.c"
            "${WEBP_THIRDPARTY_DIR}/src/dec/tree_dec.c"
            "${WEBP_THIRDPARTY_DIR}/src/dec/vp8_dec.c"
            "${WEBP_THIRDPARTY_DIR}/src/dec/vp8l_dec.c"
            "${WEBP_THIRDPARTY_DIR}/src/dec/webp_dec.c"
            "${WEBP_THIRDPARTY_DIR}/src/demux/anim_decode.c"
            "${WEBP_THIRDPARTY_DIR}/src/demux/demux.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/alpha_processing.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/alpha_processing_mips_dsp_r2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/alpha_processing_neon.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/alpha_processing_sse2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/alpha_processing_sse41.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/cost.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/cost_mips32.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/cost_mips_dsp_r2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/cost_neon.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/cost_sse2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/cpu.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/dec.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/dec_clip_tables.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/dec_mips32.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/dec_mips_dsp_r2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/dec_msa.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/dec_neon.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/dec_sse2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/dec_sse41.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/enc_mips32.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/enc_mips_dsp_r2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/enc_msa.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/enc_neon.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/enc_sse2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/enc_sse41.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/filters.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/filters_mips_dsp_r2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/filters_msa.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/filters_neon.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/filters_sse2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/lossless.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/lossless_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/lossless_enc_mips32.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/lossless_enc_mips_dsp_r2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/lossless_enc_msa.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/lossless_enc_neon.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/lossless_enc_sse2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/lossless_enc_sse41.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/lossless_mips_dsp_r2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/lossless_msa.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/lossless_neon.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/lossless_sse2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/lossless_sse41.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/rescaler.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/rescaler_mips32.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/rescaler_mips_dsp_r2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/rescaler_msa.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/rescaler_neon.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/rescaler_sse2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/ssim.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/ssim_sse2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/upsampling.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/upsampling_mips_dsp_r2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/upsampling_msa.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/upsampling_neon.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/upsampling_sse2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/upsampling_sse41.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/yuv.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/yuv_mips32.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/yuv_mips_dsp_r2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/yuv_neon.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/yuv_sse2.c"
            "${WEBP_THIRDPARTY_DIR}/src/dsp/yuv_sse41.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/alpha_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/analysis_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/backward_references_cost_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/backward_references_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/config_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/cost_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/filter_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/frame_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/histogram_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/iterator_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/near_lossless_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/picture_csp_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/picture_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/picture_psnr_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/picture_rescale_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/picture_tools_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/predictor_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/quant_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/syntax_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/token_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/tree_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/vp8l_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/enc/webp_enc.c"
            "${WEBP_THIRDPARTY_DIR}/src/mux/anim_encode.c"
            "${WEBP_THIRDPARTY_DIR}/src/mux/muxedit.c"
            "${WEBP_THIRDPARTY_DIR}/src/mux/muxinternal.c"
            "${WEBP_THIRDPARTY_DIR}/src/mux/muxread.c"
            "${WEBP_THIRDPARTY_DIR}/src/utils/bit_reader_utils.c"
            "${WEBP_THIRDPARTY_DIR}/src/utils/bit_writer_utils.c"
            "${WEBP_THIRDPARTY_DIR}/src/utils/color_cache_utils.c"
            "${WEBP_THIRDPARTY_DIR}/src/utils/filters_utils.c"
            "${WEBP_THIRDPARTY_DIR}/src/utils/huffman_encode_utils.c"
            "${WEBP_THIRDPARTY_DIR}/src/utils/huffman_utils.c"
            "${WEBP_THIRDPARTY_DIR}/src/utils/palette.c"
            "${WEBP_THIRDPARTY_DIR}/src/utils/quant_levels_dec_utils.c"
            "${WEBP_THIRDPARTY_DIR}/src/utils/quant_levels_utils.c"
            "${WEBP_THIRDPARTY_DIR}/src/utils/random_utils.c"
            "${WEBP_THIRDPARTY_DIR}/src/utils/rescaler_utils.c"
            "${WEBP_THIRDPARTY_DIR}/src/utils/thread_utils.c"
            "${WEBP_THIRDPARTY_DIR}/src/utils/utils.c"
    )

    # 3.2 包含目录（等价于 Prepend(CPPPATH=[thirdparty_dir, thirdparty_dir + "src/"]））
    target_include_directories(${GODOT_PARENT_TARGET} PRIVATE
            "${WEBP_THIRDPARTY_DIR}"
            "${WEBP_THIRDPARTY_DIR}/src"
    )

    # 3.3 将 thirdparty 源加入父级目标
    target_sources(${GODOT_PARENT_TARGET} PRIVATE ${WEBP_SOURCES})

    # 3.4 为 thirdparty 源尽量关闭告警（模拟 env_thirdparty.disable_warnings()）
    if (MSVC)
        set_source_files_properties(${WEBP_SOURCES}
                PROPERTIES COMPILE_OPTIONS "/w")
    else()
        set_source_files_properties(${WEBP_SOURCES}
                PROPERTIES COMPILE_OPTIONS "-w")
    endif()
endif()