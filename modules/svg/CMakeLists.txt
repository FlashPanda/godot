# CMakeLists.txt — 子目录：svg（thorvg）模块等效于 SCsub 的逻辑
# 放置位置：与该模块的 SCsub 同级的目录下
# 作用：将 thorvg 第三方源码与本模块源码并入父级目标 `godot`

cmake_minimum_required(VERSION 3.16)

# 确保父级已定义目标 godot（可执行或库）
if (NOT TARGET godot)
    message(FATAL_ERROR "Target 'godot' not found. Define it in parent CMakeLists.txt before add_subdirectory.")
endif()

# 选项与开关（对应 SCsub 中的 env["..."]）
option(MODULE_WEBP_ENABLED       "Enable WebP loader support in ThorVG" ON)      # 对应 env["module_webp_enabled"]
option(GODOT_BUILTIN_LIBPNG      "Use bundled thirdparty/libpng headers" ON)     # 对应 env["builtin_libpng"]
option(GODOT_BUILTIN_LIBWEBP     "Use bundled thirdparty/libwebp headers" ON)    # 对应 env["builtin_libwebp"]

# 目录布局
set(_THORVG_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/thorvg")

# 1) ThorVG 源码清单（与 SCsub 一致；根据是否启用 WebP 附加一项）
set(_thorvg_rel_sources
        # common
        "src/common/tvgCompressor.cpp"
        "src/common/tvgMath.cpp"
        "src/common/tvgStr.cpp"
        # SVG parser
        "src/loaders/svg/tvgSvgCssStyle.cpp"
        "src/loaders/svg/tvgSvgLoader.cpp"
        "src/loaders/svg/tvgSvgPath.cpp"
        "src/loaders/svg/tvgSvgSceneBuilder.cpp"
        "src/loaders/svg/tvgSvgUtil.cpp"
        "src/loaders/svg/tvgXmlParser.cpp"
        "src/loaders/raw/tvgRawLoader.cpp"
        # image loaders
        "src/loaders/external_png/tvgPngLoader.cpp"
        "src/loaders/jpg/tvgJpgd.cpp"
        "src/loaders/jpg/tvgJpgLoader.cpp"
        # renderer common
        "src/renderer/tvgAccessor.cpp"
        # "src/renderer/tvgAnimation.cpp",
        "src/renderer/tvgCanvas.cpp"
        "src/renderer/tvgFill.cpp"
        # "src/renderer/tvgGlCanvas.cpp",
        "src/renderer/tvgInitializer.cpp"
        "src/renderer/tvgLoader.cpp"
        "src/renderer/tvgPaint.cpp"
        "src/renderer/tvgPicture.cpp"
        "src/renderer/tvgRender.cpp"
        # "src/renderer/tvgSaver.cpp",
        "src/renderer/tvgScene.cpp"
        "src/renderer/tvgShape.cpp"
        "src/renderer/tvgSwCanvas.cpp"
        "src/renderer/tvgTaskScheduler.cpp"
        "src/renderer/tvgText.cpp"
        # "src/renderer/tvgWgCanvas.cpp",
        # renderer sw_engine
        "src/renderer/sw_engine/tvgSwFill.cpp"
        "src/renderer/sw_engine/tvgSwImage.cpp"
        "src/renderer/sw_engine/tvgSwMath.cpp"
        "src/renderer/sw_engine/tvgSwMemPool.cpp"
        "src/renderer/sw_engine/tvgSwPostEffect.cpp"
        "src/renderer/sw_engine/tvgSwRaster.cpp"
        "src/renderer/sw_engine/tvgSwRenderer.cpp"
        "src/renderer/sw_engine/tvgSwRle.cpp"
        "src/renderer/sw_engine/tvgSwShape.cpp"
        "src/renderer/sw_engine/tvgSwStroke.cpp"
)

if (MODULE_WEBP_ENABLED)
    list(APPEND _thorvg_rel_sources "src/loaders/external_webp/tvgWebpLoader.cpp")
endif()

# 绝对路径化
set(_thorvg_sources "")
foreach(_rel IN LISTS _thorvg_rel_sources)
    list(APPEND _thorvg_sources "${_THORVG_ROOT}/${_rel}")
endforeach()

# 2) ThorVG 第三方以 OBJECT 库承载（可关闭警告、独立包含目录/宏）
add_library(thorvg_thirdparty OBJECT ${_thorvg_sources})

# 第三方代码关闭编译警告（等价于 SCons 的 disable_warnings()）
if (MSVC)
    target_compile_options(thorvg_thirdparty PRIVATE /w)
else()
    target_compile_options(thorvg_thirdparty PRIVATE -w)
endif()

# 公共头文件（ThorVG 对外头位于 inc），以及内部子目录（与 SCsub 中 CPPPATH 对齐）
target_include_directories(thorvg_thirdparty PRIVATE
        "${_THORVG_ROOT}/inc"
        "${_THORVG_ROOT}/src/common"
        "${_THORVG_ROOT}/src/loaders/svg"
        "${_THORVG_ROOT}/src/renderer"
        "${_THORVG_ROOT}/src/renderer/sw_engine"
        "${_THORVG_ROOT}/src/loaders/raw"
        "${_THORVG_ROOT}/src/loaders/external_png"
        "${_THORVG_ROOT}/src/loaders/jpg"
)

# 可选：WebP Loader 的额外包含目录
if (MODULE_WEBP_ENABLED)
    target_include_directories(thorvg_thirdparty PRIVATE
            "${_THORVG_ROOT}/src/loaders/external_webp"
    )
endif()

# 对应 SCsub：env_svg.Append(CPPDEFINES=["TVG_STATIC"]) 以及 WEBP 支持宏
# 这些宏既影响第三方源码，也影响本模块源码的编译行为
target_compile_definitions(thorvg_thirdparty PRIVATE TVG_STATIC)
if (MODULE_WEBP_ENABLED)
    target_compile_definitions(thorvg_thirdparty PRIVATE THORVG_WEBP_LOADER_SUPPORT)
endif()

# 若选择使用内置第三方库头文件：添加额外包含路径
if (GODOT_BUILTIN_LIBPNG)
    target_include_directories(thorvg_thirdparty PRIVATE
            "${CMAKE_SOURCE_DIR}/thirdparty/libpng"
    )
endif()
if (MODULE_WEBP_ENABLED AND GODOT_BUILTIN_LIBWEBP)
    target_include_directories(thorvg_thirdparty PRIVATE
            "${CMAKE_SOURCE_DIR}/thirdparty/libwebp/src"
    )
endif()

# 3) 本模块源码：并入父级目标 godot
file(GLOB CONFIGURE_DEPENDS SVG_MODULE_LOCAL_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# 将模块的公共头（inc）也提供给 godot，用于编译本模块源码中包含的 thorvg 头
# 同时把必要宏应用到 godot（与 SCons 的 env_svg 一致）
target_include_directories(godot PRIVATE "${_THORVG_ROOT}/inc")
target_compile_definitions(godot PRIVATE TVG_STATIC)
if (MODULE_WEBP_ENABLED)
    target_compile_definitions(godot PRIVATE THORVG_WEBP_LOADER_SUPPORT)
endif()

# 汇入源码到 godot
target_sources(godot PRIVATE
        ${SVG_MODULE_LOCAL_SOURCES}
        $<TARGET_OBJECTS:thorvg_thirdparty>    # 将第三方对象文件合入 godot
)

# 建立依赖关系，保证 thirdparty 改动时触发 godot 重新链接
add_dependencies(godot thorvg_thirdparty)

# 友好提示
message(STATUS "[svg(thorvg)] WebP: ${MODULE_WEBP_ENABLED}, builtin libpng: ${GODOT_BUILTIN_LIBPNG}, builtin libwebp: ${GODOT_BUILTIN_LIBWEBP}")