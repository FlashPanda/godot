# 生成 templates.gen.h（由 */*.gd 模板文件汇总生成）

cmake_minimum_required(VERSION 3.16)

# 可配置项：提供实际的模板生成器（脚本或可执行）
# 要求生成器支持形如：--output <file> <inputs...> 的调用方式
set(TEMPLATE_GD_GEN "" CACHE FILEPATH "Path to the .gd template generator (script or executable)")
# 可选：当 TEMPLATE_GD_GEN 是 Python 脚本时可用
find_package(Python3 COMPONENTS Interpreter QUIET)

if (NOT TEMPLATE_GD_GEN)
    message(FATAL_ERROR "TEMPLATE_GD_GEN is not set. Please provide a generator that accepts: --output <file> <gd_templates...>")
endif()

# 1) 收集当前目录下一层子目录中的 *.gd（匹配 */*.gd）
file(GLOB CONFIGURE_DEPENDS GD_TEMPLATES "${CMAKE_CURRENT_SOURCE_DIR}/*/*.gd")

if (GD_TEMPLATES STREQUAL "")
    message(WARNING "[editor_template_gd] No .gd templates found in: ${CMAKE_CURRENT_SOURCE_DIR}/*/")
endif()

# 2) 设定输出到构建目录，避免污染源码
set(TEMPLATE_GD_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(TEMPLATE_GD_OUTPUT      "${TEMPLATE_GD_OUTPUT_DIR}/templates.gen.h")

# 3) 构造调用命令：若生成器是 .py，用 Python 解释器包装；否则直接执行
get_filename_component(_gen_ext "${TEMPLATE_GD_GEN}" EXT)
set(_invoke_cmd "${TEMPLATE_GD_GEN}")
if (_gen_ext STREQUAL ".py")
    if (Python3_Interpreter_FOUND)
        set(_invoke_cmd "${Python3_EXECUTABLE};${TEMPLATE_GD_GEN}")
    else()
        find_program(_PY3 python3)
        if (NOT _PY3)
            find_program(_PY3 python)
        endif()
        if (NOT _PY3)
            message(FATAL_ERROR "Python interpreter not found for Python generator: ${TEMPLATE_GD_GEN}")
        endif()
        set(_invoke_cmd "${_PY3};${TEMPLATE_GD_GEN}")
    endif()
endif()

# 4) 生成命令
add_custom_command(
        OUTPUT "${TEMPLATE_GD_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${TEMPLATE_GD_OUTPUT_DIR}"
        COMMAND ${_invoke_cmd} --output "${TEMPLATE_GD_OUTPUT}" ${GD_TEMPLATES}
        BYPRODUCTS "${TEMPLATE_GD_OUTPUT}"
        DEPENDS ${GD_TEMPLATES} "${TEMPLATE_GD_GEN}"
        COMMENT "[editor_template_gd] Generating ${TEMPLATE_GD_OUTPUT}"
        VERBATIM
)

# 5) 提供别名目标，便于上层 add_dependencies 使用
add_custom_target(editor_template_gd
        DEPENDS "${TEMPLATE_GD_OUTPUT}"
)

# 6) 导出变量，供上层目标引用或加入编译
# 上层可以：
# - add_dependencies(godot editor_template_gd)
# - target_include_directories(godot PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
# - target_sources(godot PRIVATE "${TEMPLATE_GD_HEADER}")
set(TEMPLATE_GD_HEADER "${TEMPLATE_GD_OUTPUT}" PARENT_SCOPE)