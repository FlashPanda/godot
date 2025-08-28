# CMake

# 选项：是否为编辑器构建（决定是否包含 BasisU encoder）
option(EDITOR_BUILD "Enable editor build (build BasisU encoder sources)" ON)
# 选项：是否使用内置 zstd（仅影响包含路径）
option(BUILTIN_ZSTD "Use bundled thirdparty/zstd include directory" ON)

# 路径
set(THIRDPARTY_BASISU_DIR "${CMAKE_SOURCE_DIR}/thirdparty/basis_universal")

# 同步上游 CMakeLists 的 encoder 源列表（仅在 EDITOR_BUILD=ON 时启用）
set(BASISU_ENCODER_SOURCES
        "${THIRDPARTY_BASISU_DIR}/encoder/3rdparty/android_astc_decomp.cpp"
        "${THIRDPARTY_BASISU_DIR}/encoder/basisu_astc_hdr_enc.cpp"
        "${THIRDPARTY_BASISU_DIR}/encoder/basisu_backend.cpp"
        "${THIRDPARTY_BASISU_DIR}/encoder/basisu_basis_file.cpp"
        "${THIRDPARTY_BASISU_DIR}/encoder/basisu_bc7enc.cpp"
        "${THIRDPARTY_BASISU_DIR}/encoder/basisu_opencl.cpp"
        "${THIRDPARTY_BASISU_DIR}/encoder/basisu_comp.cpp"
        "${THIRDPARTY_BASISU_DIR}/encoder/basisu_enc.cpp"
        "${THIRDPARTY_BASISU_DIR}/encoder/basisu_etc.cpp"
        "${THIRDPARTY_BASISU_DIR}/encoder/basisu_frontend.cpp"
        "${THIRDPARTY_BASISU_DIR}/encoder/basisu_gpu_texture.cpp"
        "${THIRDPARTY_BASISU_DIR}/encoder/basisu_kernels_sse.cpp"
        "${THIRDPARTY_BASISU_DIR}/encoder/basisu_pvrtc1_4.cpp"
        "${THIRDPARTY_BASISU_DIR}/encoder/basisu_resampler.cpp"
        "${THIRDPARTY_BASISU_DIR}/encoder/basisu_resample_filters.cpp"
        "${THIRDPARTY_BASISU_DIR}/encoder/basisu_ssim.cpp"
        "${THIRDPARTY_BASISU_DIR}/encoder/basisu_uastc_enc.cpp"
        "${THIRDPARTY_BASISU_DIR}/encoder/pvpngreader.cpp"
)

# 始终包含的 transcoder 源
set(BASISU_TRANSCODER_SOURCES
        "${THIRDPARTY_BASISU_DIR}/transcoder/basisu_transcoder.cpp"
)

# 头文件包含（BasisU 作为系统头，MSVC 下不标记为 SYSTEM）
if(MSVC)
    target_include_directories(godot PRIVATE "${THIRDPARTY_BASISU_DIR}")
else()
    target_include_directories(godot SYSTEM PRIVATE "${THIRDPARTY_BASISU_DIR}")
endif()

# encoder 启用时添加 jpeg-compressor / tinyexr 头
if(EDITOR_BUILD)
    target_include_directories(godot PRIVATE
            "${CMAKE_SOURCE_DIR}/thirdparty/jpeg-compressor"
            "${CMAKE_SOURCE_DIR}/thirdparty/tinyexr"
    )
endif()

# 若使用内置 zstd，则加入头目录
if(BUILTIN_ZSTD)
    target_include_directories(godot PRIVATE
            "${CMAKE_SOURCE_DIR}/thirdparty/zstd"
    )
endif()

# 汇总第三方源码（禁告）
set(THIRDPARTY_SOURCES ${BASISU_TRANSCODER_SOURCES})
if(EDITOR_BUILD)
    list(APPEND THIRDPARTY_SOURCES ${BASISU_ENCODER_SOURCES})
endif()

# 禁用第三方源码告警
if(MSVC)
    set_source_files_properties(${THIRDPARTY_SOURCES} PROPERTIES COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(${THIRDPARTY_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endif()

# 本模块源码（当前目录下所有 *.cpp）
file(GLOB MODULE_LOCAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 并入到已存在的目标 godot
target_sources(godot PRIVATE
        ${THIRDPARTY_SOURCES}
        ${MODULE_LOCAL_SOURCES}
)

# 对应 SCons 中的 CPPDEFINES（禁用部分特性以减小体积）
# - 仅禁用 KTX2 的 ZSTD（encoder 需要 BASISD_SUPPORT_KTX2 为启用状态）
target_compile_definitions(godot PRIVATE
        BASISD_SUPPORT_KTX2_ZSTD=0
        BASISD_SUPPORT_ATC=0
        BASISD_SUPPORT_FXT1=0
        BASISD_SUPPORT_PVRTC1=0
        BASISD_SUPPORT_PVRTC2=0
)