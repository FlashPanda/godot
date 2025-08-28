# CMakeLists.txt - freetype 模块子目录

cmake_minimum_required(VERSION 3.16)

# 供上层或用户配置的选项
option(GODOT_BUILTIN_FREETYPE "Build bundled FreeType from thirdparty" ON)
option(GODOT_BUILTIN_LIBPNG "Use bundled libpng headers for FreeType" ON)
option(GODOT_USE_BROTLI "Enable Brotli support in FreeType" OFF)
option(GODOT_DEV_BUILD "Enable dev build flags (e.g., ZLIB_DEBUG)" OFF)

# 依赖：上层应已定义目标 godot
if (NOT TARGET godot)
    message(FATAL_ERROR "Target 'godot' not found. Include this subdirectory after defining target 'godot'.")
endif()

set(LOCAL_CPP_SOURCES "")
file(GLOB CONFIGURE_DEPENDS LOCAL_CPP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
if (LOCAL_CPP_SOURCES)
    target_sources(godot PRIVATE ${LOCAL_CPP_SOURCES})
endif()

# ========= 内置 FreeType =========
if (GODOT_BUILTIN_FREETYPE)
    set(FT_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/freetype")

    # 第三方 freetype 源清单（与 SCsub 保持一致）
    set(FT_SOURCES
            "${FT_ROOT}/src/autofit/autofit.c"
            "${FT_ROOT}/src/base/ftbase.c"
            "${FT_ROOT}/src/base/ftbbox.c"
            "${FT_ROOT}/src/base/ftbdf.c"
            "${FT_ROOT}/src/base/ftbitmap.c"
            "${FT_ROOT}/src/base/ftcid.c"
            "${FT_ROOT}/src/base/ftdebug.c"
            "${FT_ROOT}/src/base/ftfstype.c"
            "${FT_ROOT}/src/base/ftgasp.c"
            "${FT_ROOT}/src/base/ftglyph.c"
            "${FT_ROOT}/src/base/ftgxval.c"
            "${FT_ROOT}/src/base/ftinit.c"
            "${FT_ROOT}/src/base/ftmm.c"
            "${FT_ROOT}/src/base/ftotval.c"
            "${FT_ROOT}/src/base/ftpatent.c"
            "${FT_ROOT}/src/base/ftpfr.c"
            "${FT_ROOT}/src/base/ftstroke.c"
            "${FT_ROOT}/src/base/ftsynth.c"
            "${FT_ROOT}/src/base/ftsystem.c"
            "${FT_ROOT}/src/base/fttype1.c"
            "${FT_ROOT}/src/base/ftwinfnt.c"
            "${FT_ROOT}/src/bdf/bdf.c"
            "${FT_ROOT}/src/bzip2/ftbzip2.c"
            "${FT_ROOT}/src/cache/ftcache.c"
            "${FT_ROOT}/src/cff/cff.c"
            "${FT_ROOT}/src/cid/type1cid.c"
            "${FT_ROOT}/src/gxvalid/gxvalid.c"
            "${FT_ROOT}/src/gzip/ftgzip.c"
            "${FT_ROOT}/src/lzw/ftlzw.c"
            "${FT_ROOT}/src/otvalid/otvalid.c"
            "${FT_ROOT}/src/pcf/pcf.c"
            "${FT_ROOT}/src/pfr/pfr.c"
            "${FT_ROOT}/src/psaux/psaux.c"
            "${FT_ROOT}/src/pshinter/pshinter.c"
            "${FT_ROOT}/src/psnames/psnames.c"
            "${FT_ROOT}/src/raster/raster.c"
            "${FT_ROOT}/src/sdf/sdf.c"
            "${FT_ROOT}/src/svg/svg.c"
            "${FT_ROOT}/src/smooth/smooth.c"
            "${FT_ROOT}/src/truetype/truetype.c"
            "${FT_ROOT}/src/type1/type1.c"
            "${FT_ROOT}/src/type42/type42.c"
            "${FT_ROOT}/src/winfonts/winfnt.c"
    )

    # 特例：sfnt.c 需在 Emscripten 下关闭 SIMD 并 -U__OPTIMIZE__
    set(FT_SFNT "${FT_ROOT}/src/sfnt/sfnt.c")
    list(APPEND FT_SOURCES "${FT_SFNT}")

    add_library(freetype_builtin STATIC ${FT_SOURCES})

    # 包含目录：freetype 自身 +（可选）bundled libpng 头文件
    target_include_directories(freetype_builtin
            PUBLIC
            "${FT_ROOT}/include"
    )
    if (GODOT_BUILTIN_LIBPNG)
        target_include_directories(freetype_builtin
                PRIVATE
                "${CMAKE_SOURCE_DIR}/thirdparty/libpng"
        )
    endif()

    # 预处理宏
    target_compile_definitions(freetype_builtin
            PRIVATE
            FT2_BUILD_LIBRARY
            FT_CONFIG_OPTION_USE_PNG
            FT_CONFIG_OPTION_SYSTEM_ZLIB
    )
    if (GODOT_USE_BROTLI)
        target_compile_definitions(freetype_builtin PRIVATE FT_CONFIG_OPTION_USE_BROTLI)
    endif()
    if (GODOT_DEV_BUILD)
        target_compile_definitions(freetype_builtin PRIVATE ZLIB_DEBUG)
    endif()

    # 针对 Emscripten 的 sfnt.c 特殊编译选项（等价于 SCons 的 -U__OPTIMIZE__ 且可选关警告）
    if (EMSCRIPTEN)
        # 关闭该源文件的优化宏，并静默警告（如需）
        set_source_files_properties("${FT_SFNT}" PROPERTIES
                COMPILE_OPTIONS "-U__OPTIMIZE__;-w"
        )
    endif()

    # 将 freetype 包含给 godot（保持与 SCsub 中“Also needed in main env for scene/”一致）
    target_include_directories(godot PRIVATE "${FT_ROOT}/include")

    # 链接到主目标，确保链接顺序合适（CMake 通常能自动处理）
    target_link_libraries(godot PRIVATE freetype_builtin)
endif()