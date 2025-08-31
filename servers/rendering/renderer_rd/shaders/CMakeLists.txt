# 本文件为“子目录”的 CMake 构建脚本，不在此处设置 cmake_minimum_required()/project()。
# 目标：复刻 SCons 脚本逻辑：
#  1) 找到本目录下所有 *_inc.glsl（仅作为 include 的 GLSL 片段）
#  2) 找到本目录下所有需要编译的 *.glsl（排除 *_inc.glsl）
#  3) 生成对应的 .gen.h 头文件（输出到当前二进制目录）
#  4) 对于普通 *.glsl 的生成，额外依赖 include 片段与 glsl_builders.py，以便发生变化时会重新生成
#  5) 将生成的头文件加入上层已有目标（默认目标名为 godot），在编译前确保生成
#  6) 递归进入 effects、environment、forward_clustered、forward_mobile、deferred 子目录

# =========== 可配置项（可在上层 CMake 或命令行 -D 覆盖） ===========

# 上层已创建的构建目标名（add_executable()/add_library()）
set(GODOT_TARGET "godot" CACHE STRING "上层已存在的构建目标名称")

# 生成 include 片段（*_inc.glsl）对应头文件的命令（等价于 env.GLSL_HEADER）
# 以“列表”形式传入更稳健，比如：
#   -DGODOT_GLSL_HEADER_TOOL="python;${PROJECT_SOURCE_DIR}/path/to/glsl_builders.py;--glsl-header"
set(GODOT_GLSL_HEADER_TOOL "" CACHE STRING "将 *_inc.glsl 生成 .gen.h 的命令（可为 解释器;脚本;子命令 的列表）")

# 生成普通 *.glsl 对应头文件的命令（等价于 env.RD_GLSL）
# 以“列表”形式传入，比如：
#   -DGODOT_RD_GLSL_TOOL="python;${PROJECT_SOURCE_DIR}/path/to/glsl_builders.py;--rd-glsl"
set(GODOT_RD_GLSL_TOOL "" CACHE STRING "将 *.glsl 生成 .gen.h 的命令（可为 解释器;脚本;子命令 的列表）")

# 指定生成器用于设置输出文件路径的参数名，默认为 -o
set(GODOT_GLSL_OUTFLAG "-o" CACHE STRING "传给生成器、用于指定输出文件路径的参数名（如 -o）")

# 可选：glsl_builders.py 的可能位置（若存在，将作为依赖触发）
set(_CANDIDATE_BUILDER_PATHS
        "${PROJECT_SOURCE_DIR}/glsl_builders.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/glsl_builders.py"
)

# =========== 前置检查 ===========
if(NOT TARGET ${GODOT_TARGET})
    message(FATAL_ERROR "未找到构建目标 ${GODOT_TARGET}。请在上层先创建该目标，或通过 -DGODOT_TARGET=... 指定。")
endif()

if(NOT GODOT_GLSL_HEADER_TOOL)
    message(FATAL_ERROR
            "未设置 GODOT_GLSL_HEADER_TOOL。示例：\n"
            "  -DGODOT_GLSL_HEADER_TOOL=\"python;${PROJECT_SOURCE_DIR}/path/to/glsl_builders.py;--glsl-header\""
    )
endif()

if(NOT GODOT_RD_GLSL_TOOL)
    message(FATAL_ERROR
            "未设置 GODOT_RD_GLSL_TOOL。示例：\n"
            "  -DGODOT_RD_GLSL_TOOL=\"python;${PROJECT_SOURCE_DIR}/path/to/glsl_builders.py;--rd-glsl\""
    )
endif()

# =========== 收集文件 ===========
# 1) include 片段：*_inc.glsl（仅限当前目录）
file(GLOB GLSL_INCLUDE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/*_inc.glsl"
)

# 2) 普通着色器：当前目录 *.glsl，剔除 *_inc.glsl
file(GLOB _ALL_GLSL_IN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/*.glsl")
set(GLSL_SHADER_FILES "${_ALL_GLSL_IN_DIR}")
list(FILTER GLSL_SHADER_FILES EXCLUDE REGEX "_inc\\.glsl$")

# 3) 查找 glsl_builders.py（若存在就加入依赖）
set(GODOT_GLSL_BUILDER_FILE "")
foreach(_p IN LISTS _CANDIDATE_BUILDER_PATHS)
    if(EXISTS "${_p}")
        set(GODOT_GLSL_BUILDER_FILE "${_p}")
        break()
    endif()
endforeach()

# =========== 为 *_inc.glsl 生成头文件（等价于 env.GLSL_HEADER） ===========
set(GENERATED_INC_HEADERS)
foreach(INC_FILE IN LISTS GLSL_INCLUDE_FILES)
    get_filename_component(_name "${INC_FILE}" NAME)                         # e.g. common_inc.glsl
    set(_out_header "${CMAKE_CURRENT_BINARY_DIR}/${_name}.gen.h")            # e.g. <bin>/common_inc.glsl.gen.h

    add_custom_command(
            OUTPUT  "${_out_header}"
            COMMAND ${GODOT_GLSL_HEADER_TOOL} "${INC_FILE}" ${GODOT_GLSL_OUTFLAG} "${_out_header}"
            DEPENDS "${INC_FILE}"
            COMMENT "生成（include 片段）GLSL 头文件: ${_out_header}"
            VERBATIM
    )

    list(APPEND GENERATED_INC_HEADERS "${_out_header}")
endforeach()

# =========== 为普通 *.glsl 生成头文件（等价于 env.RD_GLSL） ===========
set(GENERATED_SHADER_HEADERS)
foreach(GLSL_FILE IN LISTS GLSL_SHADER_FILES)
    get_filename_component(_name "${GLSL_FILE}" NAME)                         # e.g. water.glsl
    set(_out_header "${CMAKE_CURRENT_BINARY_DIR}/${_name}.gen.h")             # e.g. <bin>/water.glsl.gen.h

    if(GODOT_GLSL_BUILDER_FILE)
        # 依赖：自身 + 所有 *_inc.glsl + glsl_builders.py
        add_custom_command(
                OUTPUT  "${_out_header}"
                COMMAND ${GODOT_RD_GLSL_TOOL} "${GLSL_FILE}" ${GODOT_GLSL_OUTFLAG} "${_out_header}"
                DEPENDS "${GLSL_FILE}" ${GLSL_INCLUDE_FILES} "${GODOT_GLSL_BUILDER_FILE}"
                COMMENT "生成（着色器）GLSL 头文件: ${_out_header}"
                VERBATIM
        )
    else()
        # 依赖：自身 + 所有 *_inc.glsl
        add_custom_command(
                OUTPUT  "${_out_header}"
                COMMAND ${GODOT_RD_GLSL_TOOL} "${GLSL_FILE}" ${GODOT_GLSL_OUTFLAG} "${_out_header}"
                DEPENDS "${GLSL_FILE}" ${GLSL_INCLUDE_FILES}
                COMMENT "生成（着色器）GLSL 头文件: ${_out_header}"
                VERBATIM
        )
    endif()

    list(APPEND GENERATED_SHADER_HEADERS "${_out_header}")
endforeach()

# =========== 聚合构建目标 ===========
# 统一的生成目标，便于在主目标编译前触发生成
add_custom_target(glsl_gen ALL
        DEPENDS
        ${GENERATED_INC_HEADERS}
        ${GENERATED_SHADER_HEADERS}
)

# 将生成的头文件接入上层目标，并保证先行生成
target_sources(${GODOT_TARGET} PRIVATE
        ${GENERATED_INC_HEADERS}
        ${GENERATED_SHADER_HEADERS}
)
add_dependencies(${GODOT_TARGET} glsl_gen)

# 让编译器能 #include "<name>.glsl.gen.h"
target_include_directories(${GODOT_TARGET} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# =========== 进入子目录 ===========
# 等价于：
#   SConscript("effects/SCsub")
#   SConscript("environment/SCsub")
#   SConscript("forward_clustered/SCsub")
#   SConscript("forward_mobile/SCsub")
#   SConscript("deferred/SCsub")
foreach(_sub IN ITEMS effects environment forward_clustered forward_mobile deferred)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_sub}")
        add_subdirectory("${_sub}")
    endif()
endforeach()