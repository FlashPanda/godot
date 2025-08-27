# 子目录 CMakeLists：生成 editor/themes/editor_icons.gen.h

cmake_minimum_required(VERSION 3.16)

# 可配置项
set(EDITOR_ICON_GEN "" CACHE FILEPATH "Path to the editor icons generator (script or executable)")
set(EDITOR_MODULE_ICON_DIRS "" CACHE STRING "List of extra module icon directories (relative to source root or absolute)")
# 可选 Python（当 EDITOR_ICON_GEN 是 Python 脚本时可用）
find_package(Python3 COMPONENTS Interpreter QUIET)

if (NOT EDITOR_ICON_GEN)
    message(FATAL_ERROR "EDITOR_ICON_GEN is not set. Please provide a generator that accepts: --output <file> <svgs...>")
endif()

# 1) 收集当前目录下的 *.svg
file(GLOB CONFIGURE_DEPENDS LOCAL_SVGS "${CMAKE_CURRENT_SOURCE_DIR}/*.svg")

# 2) 收集模块目录中的 *.svg（相对路径按源根解析）
set(MODULE_SVGS "")
foreach(icon_dir IN LISTS EDITOR_MODULE_ICON_DIRS)
    if (IS_ABSOLUTE "${icon_dir}")
        file(GLOB CONFIGURE_DEPENDS _svgs "${icon_dir}/*.svg")
    else()
        file(GLOB CONFIGURE_DEPENDS _svgs "${CMAKE_SOURCE_DIR}/${icon_dir}/*.svg")
    endif()
    list(APPEND MODULE_SVGS ${_svgs})
endforeach()

set(ALL_SVGS ${LOCAL_SVGS} ${MODULE_SVGS})

if (ALL_SVGS STREQUAL "")
    message(WARNING "[editor_icons] No SVG icons found. The generated header will still be created but may be empty.")
endif()

# 3) 输出路径（放到构建目录，避免污染源码树）
set(EDITOR_ICONS_OUTPUT "${CMAKE_BINARY_DIR}/editor/themes/editor_icons.gen.h")
set(EDITOR_ICONS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/editor/themes")

# 4) 生成命令
# 若 EDITOR_ICON_GEN 是 Python 脚本，可通过 Python3_EXECUTABLE 调用；否则直接执行
get_filename_component(_gen_ext "${EDITOR_ICON_GEN}" EXT)
set(_invoke_cmd "${EDITOR_ICON_GEN}")
if (_gen_ext STREQUAL ".py")
    if (Python3_Interpreter_FOUND)
        set(_invoke_cmd "${Python3_EXECUTABLE};${EDITOR_ICON_GEN}")
    else()
        # 退回系统命令 python3
        find_program(_PY3 python3)
        if (NOT _PY3)
            find_program(_PY3 python)
        endif()
        if (NOT _PY3)
            message(FATAL_ERROR "Python interpreter not found for Python generator: ${EDITOR_ICON_GEN}")
        endif()
        set(_invoke_cmd "${_PY3};${EDITOR_ICON_GEN}")
    endif()
endif()

add_custom_command(
        OUTPUT "${EDITOR_ICONS_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${EDITOR_ICONS_OUTPUT_DIR}"
        COMMAND ${_invoke_cmd} --output "${EDITOR_ICONS_OUTPUT}" ${ALL_SVGS}
        BYPRODUCTS "${EDITOR_ICONS_OUTPUT}"
        DEPENDS ${ALL_SVGS} "${EDITOR_ICON_GEN}"
        COMMENT "[editor_icons] Generating ${EDITOR_ICONS_OUTPUT}"
        VERBATIM
)

# 5) 自定义目标，供上层依赖
add_custom_target(editor_icons ALL
        DEPENDS "${EDITOR_ICONS_OUTPUT}"
)

# 6) 导出变量，便于上层引用
set(EDITOR_ICONS_HEADER "${EDITOR_ICONS_OUTPUT}" PARENT_SCOPE)
# 上层可以：
# - add_dependencies(godot editor_icons)
# - target_include_directories(godot PRIVATE "${CMAKE_BINARY_DIR}")
# - 或 target_sources(godot PRIVATE "${EDITOR_ICONS_HEADER}")