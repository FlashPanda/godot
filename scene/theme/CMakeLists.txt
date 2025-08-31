# 本文件为子目录构建脚本，不在此处设置 cmake_minimum_required()/project()。

# ============ 配置区域 ============
# 1) 目标名：需与上层已创建的目标名一致（add_executable/add_library）。
set(_GODOT_TARGET godot)

# 2) 生成器命令：用于把若干 .svg 生成出 default_theme_icons.gen.h。
#    允许从父级传入（-DDEFAULT_THEME_ICONS_GENERATOR="python3;/path/to/gen.py"），
#    也可在此直接设置为合适的命令行（以分号分隔的列表会被当作命令+参数序列）。
if(NOT DEFINED DEFAULT_THEME_ICONS_GENERATOR)
    message(FATAL_ERROR
            "请通过 -DDEFAULT_THEME_ICONS_GENERATOR=... 指定图标生成器命令。\n"
            "例如：-DDEFAULT_THEME_ICONS_GENERATOR=\"python3;/abs/path/make_default_theme_icons.py\""
    )
endif()

# ============ 源与产物路径 ============
# 本目录下的所有 SVG 资源
file(GLOB ICON_SVGS "${CMAKE_CURRENT_SOURCE_DIR}/*.svg")

# 输出头文件路径：
# 为避免写入源码树，放到构建目录下的 scene/theme/default_theme_icons.gen.h。
set(DEFAULT_THEME_ICONS_OUT_DIR "${CMAKE_BINARY_DIR}/scene/theme")
set(DEFAULT_THEME_ICONS_OUT     "${DEFAULT_THEME_ICONS_OUT_DIR}/default_theme_icons.gen.h")

# ============ 生成规则 ============
# 自定义命令：
# - 依赖所有 SVG，当任一 SVG 变化时会重新生成头文件；
# - 先创建输出目录，再调用生成器；
# - 生成器调用约定：<生成器> --out <输出文件> <输入SVG...>
add_custom_command(
        OUTPUT "${DEFAULT_THEME_ICONS_OUT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DEFAULT_THEME_ICONS_OUT_DIR}"
        COMMAND ${DEFAULT_THEME_ICONS_GENERATOR} --out "${DEFAULT_THEME_ICONS_OUT}" ${ICON_SVGS}
        DEPENDS ${ICON_SVGS}
        BYPRODUCTS "${DEFAULT_THEME_ICONS_OUT}"
        COMMENT "Generating default_theme_icons.gen.h from SVGs"
        VERBATIM
)

# 自定义目标：与 SCons 中的 Alias('default_theme_icons', ...) 对应
add_custom_target(default_theme_icons
        DEPENDS "${DEFAULT_THEME_ICONS_OUT}"
)

# 让主目标在构建前生成该头文件，并能找到它的包含路径
add_dependencies(${_GODOT_TARGET} default_theme_icons)
target_include_directories(${_GODOT_TARGET} PRIVATE "${DEFAULT_THEME_ICONS_OUT_DIR}")

# 备注：
# - 若上层代码通过 #include "default_theme_icons.gen.h" 或 <default_theme_icons.gen.h> 引用该头，
#   上面的 include 目录已覆盖，无需改动源码。
# - 如需把生成产物写入源码树（不推荐），可将 DEFAULT_THEME_ICONS_OUT_DIR 改为 ${CMAKE_SOURCE_DIR}/scene/theme。