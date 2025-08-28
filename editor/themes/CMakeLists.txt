# 作为 editor/themes/CMakeLists.txt 使用

# 确保上层已有目标 godot
if(NOT TARGET godot)
    message(FATAL_ERROR "目标 'godot' 未定义。请先在上层定义后再 add_subdirectory(editor/themes)。")
endif()

# 1) 收集本目录下的 .cpp 源文件并加入 godot
file(GLOB LOCAL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
if(LOCAL_SOURCES)
    target_sources(godot PRIVATE ${LOCAL_SOURCES})
endif()

# 2) 生成 builtin_fonts.gen.h
# 依赖 thirdparty/fonts 中的字体文件；当字体变更时自动重新生成
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# 收集四类字体文件并排序（与 SCsub 的 flist.sort() 保持一致）
file(GLOB FONT_TTF  "${CMAKE_SOURCE_DIR}/thirdparty/fonts/*.ttf")
file(GLOB FONT_OTF  "${CMAKE_SOURCE_DIR}/thirdparty/fonts/*.otf")
file(GLOB FONT_WOFF "${CMAKE_SOURCE_DIR}/thirdparty/fonts/*.woff")
file(GLOB FONT_WOFF2 "${CMAKE_SOURCE_DIR}/thirdparty/fonts/*.woff2")

set(FONT_FILES ${FONT_TTF} ${FONT_OTF} ${FONT_WOFF} ${FONT_WOFF2})
list(REMOVE_DUPLICATES FONT_FILES)
list(SORT FONT_FILES)

# 输出到当前二进制目录（build/.../editor/themes/builtin_fonts.gen.h）
set(BUILTIN_FONTS_HEADER "${CMAKE_CURRENT_BINARY_DIR}/builtin_fonts.gen.h")

# 使用 editor_theme_builders.py 中的 make_fonts_header 生成头文件
# 该函数原型为 make_fonts_header(target, source, env)，这里用 Python 一次性调用
add_custom_command(
        OUTPUT "${BUILTIN_FONTS_HEADER}"
        COMMAND ${Python3_EXECUTABLE}
        -c
        "import sys, os; sys.path.insert(0, r'${CMAKE_CURRENT_SOURCE_DIR}');
from editor_theme_builders import make_fonts_header as _m;
_m([sys.argv[1]], sys.argv[2:], None)"
        "${BUILTIN_FONTS_HEADER}" ${FONT_FILES}
        DEPENDS ${FONT_FILES} "${CMAKE_CURRENT_SOURCE_DIR}/editor_theme_builders.py"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating builtin_fonts.gen.h from thirdparty/fonts (*.ttf, *.otf, *.woff, *.woff2)"
        VERBATIM
)

# 聚合目标，便于上层依赖
add_custom_target(editor_builtin_fonts_gen DEPENDS "${BUILTIN_FONTS_HEADER}")

# 让 godot 依赖生成，并将生成头作为源纳入，添加包含目录
add_dependencies(godot editor_builtin_fonts_gen)
target_sources(godot PRIVATE "${BUILTIN_FONTS_HEADER}")

# 为了能以包含路径访问到 editor/themes/builtin_fonts.gen.h，
# 同时加入当前二进制目录与顶层二进制目录（便于按 "editor/themes/..." 形式包含）
target_include_directories(godot PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${CMAKE_BINARY_DIR}"
)