# 子目录：modules/<this>/CMakeLists.txt
cmake_minimum_required(VERSION 3.18)

# 是否构建 ASTC 编码器（对应 SCons: env.editor_build；OFF 则仅解码）
option(ASTC_ENCODER "Build ASTC encoder (if OFF, build decompress-only)" OFF)

# 第三方 astcenc 源
set(THIRDPARTY_DIR "${CMAKE_SOURCE_DIR}/thirdparty/astcenc")
set(THIRDPARTY_SOURCES
        "${THIRDPARTY_DIR}/astcenc_averages_and_directions.cpp"
        "${THIRDPARTY_DIR}/astcenc_block_sizes.cpp"
        "${THIRDPARTY_DIR}/astcenc_color_quantize.cpp"
        "${THIRDPARTY_DIR}/astcenc_color_unquantize.cpp"
        "${THIRDPARTY_DIR}/astcenc_compress_symbolic.cpp"
        "${THIRDPARTY_DIR}/astcenc_compute_variance.cpp"
        "${THIRDPARTY_DIR}/astcenc_decompress_symbolic.cpp"
        "${THIRDPARTY_DIR}/astcenc_diagnostic_trace.cpp"
        "${THIRDPARTY_DIR}/astcenc_entry.cpp"
        "${THIRDPARTY_DIR}/astcenc_find_best_partitioning.cpp"
        "${THIRDPARTY_DIR}/astcenc_ideal_endpoints_and_weights.cpp"
        "${THIRDPARTY_DIR}/astcenc_image.cpp"
        "${THIRDPARTY_DIR}/astcenc_integer_sequence.cpp"
        "${THIRDPARTY_DIR}/astcenc_mathlib.cpp"
        "${THIRDPARTY_DIR}/astcenc_mathlib_softfloat.cpp"
        "${THIRDPARTY_DIR}/astcenc_partition_tables.cpp"
        "${THIRDPARTY_DIR}/astcenc_percentile_tables.cpp"
        "${THIRDPARTY_DIR}/astcenc_pick_best_endpoint_format.cpp"
        "${THIRDPARTY_DIR}/astcenc_quantization.cpp"
        "${THIRDPARTY_DIR}/astcenc_symbolic_physical.cpp"
        "${THIRDPARTY_DIR}/astcenc_weight_align.cpp"
        "${THIRDPARTY_DIR}/astcenc_weight_quant_xfer_tables.cpp"
)

# 将第三方编为 OBJECT，便于禁用告警、设置专用包含路径
add_library(astcenc_thirdparty_obj OBJECT ${THIRDPARTY_SOURCES})
target_include_directories(astcenc_thirdparty_obj PRIVATE "${THIRDPARTY_DIR}")

# 禁用 thirdparty 告警
if(MSVC)
    target_compile_options(astcenc_thirdparty_obj PRIVATE /w)
else()
    target_compile_options(astcenc_thirdparty_obj PRIVATE -w)
endif()

# 非编辑器构建时仅解码
if(NOT ASTC_ENCODER)
    target_compile_definitions(astcenc_thirdparty_obj PRIVATE ASTCENC_DECOMPRESS_ONLY)
endif()

# 模块本地源（等价于 add_source_files(module_obj, "*.cpp")）
file(GLOB MODULE_LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 打包为静态库，名称遵循常见的 module_<name> 约定，便于父级自动收集
add_library(module_astcenc STATIC
        ${MODULE_LOCAL_SOURCES}
        $<TARGET_OBJECTS:astcenc_thirdparty_obj>
)

# 模块能包含第三方头
target_include_directories(module_astcenc PRIVATE "${THIRDPARTY_DIR}")

# 构建顺序上让模块依赖 thirdparty 对象（贴近 SCons 的 Depends 语义）
add_dependencies(module_astcenc astcenc_thirdparty_obj)

# 若父级已有可执行目标 godot，则直接链接进去
if(TARGET godot)
    target_link_libraries(godot PRIVATE module_astcenc)
endif()